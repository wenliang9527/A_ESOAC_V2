/* 红外：Timer0+PWM5(PC5) 发送；EXTI(PC3) 学习捕获；主频 48MHz */

#include "frIRConversion.h"

#include "driver_pwm.h"
#include "driver_plf.h"
#include "driver_system.h"
#include "driver_timer.h"
#include "driver_pmu.h"
#include "sys_utils.h"
#include "driver_exti.h"
#include "driver_gpio.h"
#include <math.h>
#include <string.h>
#include "os_mem.h"

#include "os_task.h"
#include "os_msg_q.h"
#include "co_printf.h"
#include "ll.h"
//#include "user_task.h"

/*
 * 硬件资源约定（勿与其它模块复用）：
 * - Timer0：红外发送（微秒节拍）与学习（1ms 节拍）分时独占
 * - PWM5 / PC5：红外载波输出
 * - EXTI_3 / PC3：红外接收解调输入
 */

airconditioner   ESairkey;

#define IR_DBG     FR_DBG_OFF
#define IR_LOG     FR_LOG(IR_DBG)

uint16_t user_ir_task_id = TASK_ID_NONE;
static uint8_t ir_mode = IR_SEND;
TYPEDEFIRPWMTIM IR_PWM_TIM= {0};
TYPEDEFIRLEARNDATA ir_learn_data= {0};

static TYPEDEFIRLEARN *ir_learn  = NULL;
static uint8_t ir_data_check(void);
static uint8_t sys_clk;
static uint8_t sys_clk_cfg;

/* 发送结束时的硬件与状态回收（可从 Timer0 ISR 或 IR 任务调用） */
__attribute__((section("ram_code"))) static void IR_send_complete_hw_cleanup(void)
{
    pmu_set_pin_to_PMU(GPIO_PORT_C, (1 << GPIO_BIT_5));
    pmu_set_port_mux(GPIO_PORT_C, GPIO_BIT_5, PMU_PORT_MUX_GPIO);
    pmu_set_pin_dir(GPIO_PORT_C, BIT(5), GPIO_DIR_IN);
    IR_PWM_TIM.IR_Busy = false;
    timer_stop(TIMER0);
    NVIC_DisableIRQ(TIMER0_IRQn);
#if IR_SLEEP_EN
    system_sleep_enable();
#endif
}


/* 直访 PWM 寄存器 */
struct pwm_ctrl_t
{
    uint32_t en:1;
    uint32_t reserved0:2;
    uint32_t out_en:1;
    uint32_t single:1;
    uint32_t reservaed1:27;
};

struct pwm_elt_t
{
    uint32_t cur_cnt;
    uint32_t high_cnt;
    uint32_t total_cnt;
    struct pwm_ctrl_t ctrl;
};

struct pwm_regs_t
{
    struct pwm_elt_t channel[PWM_CHANNEL_MAX];
};

static struct pwm_regs_t *pwm_ctrl = (struct pwm_regs_t *)PWM_BASE;


/* 直访 Timer0 寄存器 */
struct timer_lvr_t
{
    uint32_t load: 16;
    uint32_t unused: 16;
};

struct timer_cvr_t
{
    uint32_t count: 16;
    uint32_t unused: 16;
};

struct timer_cr_t
{
    uint32_t reserved1: 2;
    uint32_t pselect: 2;
    uint32_t reserved2: 2;
    uint32_t count_mode: 1;
    uint32_t count_enable: 1;
    uint32_t unused: 24;
};

struct timer_icr_t
{
    uint32_t data: 16;
    uint32_t unused: 16;
};

struct timer
{
    struct timer_lvr_t load_value;
    struct timer_cvr_t count_value;
    struct timer_cr_t control;
    struct timer_icr_t interrupt_clear;
};

volatile struct timer *timerp_ir = (volatile struct timer *)TIMER0;


/* 原始字节 -> 引导码/数据位/结束段的微秒时序表 */
void IR_decode(uint8_t *ir_data,uint8_t data_size,TYPEDEFIRPWMTIM *IR_Send_struct)
{
    uint8_t i=0,j=0;
    if((data_size*2+4) > LERANDATACNTMAX)
    {
        co_printf("data_size oversize!\r\n ");
        return ;
    }
    /* 添加引导码（低电平 + 高电平） */
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLEADLOW;
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLEADHIGHT;
    /* 逐字节、逐位编码，低位在前 */
    for(i=0; i<data_size; i++)
    {
        for(j=0; j<8; j++)
        {
            if((ir_data[i]>>j) & 0x01)
            {
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC1LOW;
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC1HIGHT;
            }
            else
            {
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC0LOW;
                IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRLOGIC0HIGHT;
            }
        }
    }
    /* 添加停止位 */
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRSTOPLOW;
    IR_Send_struct->IR_Pwm_State_Date[IR_Send_struct->IR_pwm_Num++] = IRSTOPHIGHT;
}

/* 启动发送：Timer0 节拍 + PC5 载波 */

void IR_start_send(TYPEDEFIRPWMTIM *IR_Send_struct)
{
    /* 学习中不允许发送 */
    if(((ir_learn_data.IR_learn_state & BIT(1)) == BIT(1)))
    {
        co_printf("ir_learn busy!\r\n");
        return;
    }
    /* 正在发送中，拒绝重复启动 */
    if(IR_PWM_TIM.IR_Busy)
    {
        co_printf("IR send busy!\r\n");

        return;
    }

    memcpy(&IR_PWM_TIM,IR_Send_struct,sizeof(TYPEDEFIRPWMTIM));
    IR_PWM_TIM.IR_Busy =true;
    IR_PWM_TIM.IR_pwm_SendNum = 0;
#if IR_SLEEP_EN
    system_sleep_disable();
#endif
    ir_mode = IR_SEND;
    IR_LOG("IR_Busy:%d ir_carrier_fre:%d IR_pwm_SendNum:%d IR_pwm_state:%d\r\n",IR_PWM_TIM.IR_Busy,IR_PWM_TIM.ir_carrier_fre,IR_PWM_TIM.IR_pwm_SendNum,IR_PWM_TIM.IR_pwm_state);
    IR_LOG("IR Send data: IR_pwm_Num%d\r\n",IR_PWM_TIM.IR_pwm_Num);
    for(uint8_t i=0; i < IR_PWM_TIM.IR_pwm_Num; i++)
    {
        IR_LOG("[%d %d],",IR_PWM_TIM.IR_Pwm_State_Date[i],i);
    }
    IR_LOG("IR Send start\r\n");
    /* 计算载波周期和 50% 占空比的高电平计数 */
    IR_PWM_TIM.total_count = system_get_pclk() / IR_PWM_TIM.ir_carrier_fre;
    IR_PWM_TIM.high_count_half = IR_PWM_TIM.total_count * (100-50) / 100;
    /* 启动 Timer0，加载第一个时序间隔 */
    timer_init(TIMER0,IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++],TIMER_PERIODIC);
    NVIC_EnableIRQ(TIMER0_IRQn);
    NVIC_SetPriority(TIMER0_IRQn, 2);

    /* 配置 PC5 为 PWM5 输出，生成红外载波 */
    pmu_set_pin_to_CPU(GPIO_PORT_C,(1<<GPIO_BIT_5));
    system_set_port_mux(GPIO_PORT_C,GPIO_BIT_5,PORTC5_FUNC_PWM5);
    pwm_init(PWM_CHANNEL_5, IR_PWM_TIM.ir_carrier_fre, 50);
    pwm_start(PWM_CHANNEL_5);
    timer_run(TIMER0);

}

/* 关闭循环发送 */
void IR_stop_send(void)
{
    IR_PWM_TIM.loop =false;
}

/* 中断内重载定时周期（µs），小于 100µs 失败 */
__attribute__((section("ram_code")))uint8_t timer_init_count_us_reload(uint32_t timer_addr, uint32_t count_us)
{
    uint16_t count;
    uint8_t prescale;

    if(count_us < 100)
    {
        return false;
    }
    /* 各系统时钟下不同预分频对应的最大周期（微秒） */
    const uint32_t timer_max_period[][TIMER_PRESCALE_MAX] =
    {
        1365, 21800, 349000,    // 48M
        2730, 43600, 699000,    // 24M
        5461, 87300, 1398000,   // 12M
        10922, 174700, 2796000
    };// 6M
    /* 根据目标周期自动选择预分频系数 */
    if(timer_max_period[sys_clk_cfg][TIMER_PRESCALE_256] < count_us)
    {
        count = (timer_max_period[sys_clk_cfg][TIMER_PRESCALE_256] * sys_clk) >> 8;
        prescale = TIMER_PRESCALE_256;
    }
    else if(timer_max_period[sys_clk_cfg][TIMER_PRESCALE_16] < count_us)
    {
        count = (count_us * sys_clk) >> 8;
        prescale = TIMER_PRESCALE_256;

    }
    else if(timer_max_period[sys_clk_cfg][TIMER_PRESCALE_1] < count_us)
    {
        count = (count_us * sys_clk) >> 4;
        prescale = TIMER_PRESCALE_16;
    }
    else
    {
        count = count_us*sys_clk;
        prescale = TIMER_PRESCALE_1;
    }

    volatile struct timer *timerp = (volatile struct timer *)timer_addr;

    timerp->control.pselect = prescale;
    timerp->interrupt_clear.data = 0x01;
    timerp->load_value.load = count;

    return true;
}

/* 由短脉冲统计估算载波，归一到 36/38/56kHz */
__attribute__((section("ram_code"))) static uint32_t calculate_carrier_frequency(uint32_t *data_buf, uint16_t buf_size)
{
    #define CARRIER_PULSE_MIN   700
    #define CARRIER_PULSE_MAX   1429
    #define SYS_CLK_FREQ        48000000
    
    uint32_t cycle_sum = 0;
    uint8_t cycle_cnt = 0;
    uint16_t i;
    
    /* 筛选出载波脉冲（周期在 700~1429 之间，对应 33K~68K） */
    for (i = 0; i < buf_size; i++)
    {
        if ((data_buf[i] > CARRIER_PULSE_MIN) && (data_buf[i] < CARRIER_PULSE_MAX))
        {
            cycle_sum += data_buf[i];
            cycle_cnt++;
        }
    }
    
    if (cycle_cnt == 0)
        return IR_CF_38K;
    
    uint32_t freq = SYS_CLK_FREQ / (cycle_sum / cycle_cnt);
    
    /* 将频率归类到最接近的标准值 */
    if (freq > (IR_CF_56K - 1000))
        return IR_CF_56K;
    else if (freq > (IR_CF_38K - 1000))
        return IR_CF_38K;
    else
        return IR_CF_36K;
}

/* EXTI 缓冲 -> 载波累计时长与间隙，写入 learn->ir_learn_Date */
__attribute__((section("ram_code"))) static void parse_pulse_data(TYPEDEFIRLEARN *learn)
{
    #define CARRIER_PULSE_MIN   700
    #define CARRIER_PULSE_MAX   1429
    #define LONG_PULSE_THRESHOLD 9600
    #define STOP_BIT_TIME       100000
    
    uint16_t i;
    uint32_t carrier_time_us;
    uint32_t gap_time_us;
    uint32_t raw_data;
    
    learn->ir_learn_data_cnt = 0;
    learn->ir_carrier_times = 0;
    
    for (i = 0; i < learn->ir_learn_data_buf_cnt; i++)
    {
        raw_data = learn->ir_learn_data_buf[i];
        
        if ((raw_data > CARRIER_PULSE_MIN) && (raw_data < CARRIER_PULSE_MAX))
        {
            /* 载波脉冲：累计载波个数 */
            learn->ir_carrier_times++;
        }
        else if (raw_data > LONG_PULSE_THRESHOLD)
        {
            /* 长间隔：先保存当前载波时长，再保存间隔时长 */
            if (learn->ir_learn_data_cnt < (LERANDATACNTMAX - 1))
            {
                carrier_time_us = (uint32_t)((learn->ir_carrier_times * 1000000UL) / learn->ir_carrier_fre);
                learn->ir_learn_Date[learn->ir_learn_data_cnt++] = carrier_time_us;
                
                gap_time_us = ((raw_data >> 16) * 1000) + ((raw_data & 0xFFFF) / 48) - 
                              (500000UL / learn->ir_carrier_fre);
                learn->ir_learn_Date[learn->ir_learn_data_cnt++] = gap_time_us;
            }
            learn->ir_carrier_times = 0;
        }
    }
    
    /* 处理末尾残余的载波脉冲 */
    if ((learn->ir_learn_data_cnt < LERANDATACNTMAX) && (learn->ir_carrier_times > 0))
    {
        carrier_time_us = (uint32_t)((learn->ir_carrier_times * 1000000UL) / learn->ir_carrier_fre);
        learn->ir_learn_Date[learn->ir_learn_data_cnt++] = carrier_time_us;
    }
    
    /* 添加停止位标记 */
    if (learn->ir_learn_data_cnt < LERANDATACNTMAX)
        learn->ir_learn_Date[learn->ir_learn_data_cnt++] = STOP_BIT_TIME;
    else
        learn->ir_learn_Date[learn->ir_learn_data_cnt - 1] = STOP_BIT_TIME;
}

/* 学习成功：写入当前键 airbutton[]，关 EXTI/Timer0 */
__attribute__((section("ram_code"))) static bool save_learned_data(TYPEDEFIRLEARN *learn)
{
    timer_stop(TIMER0);
    ext_int_disable(EXTI_3);
    NVIC_DisableIRQ(TIMER0_IRQn);
    ir_learn_data.IR_learn_state |= BIT(0);
    ir_learn_data.ir_learn_data_cnt = learn->ir_learn_data_cnt;
    memcpy(ir_learn_data.ir_learn_Date, learn->ir_learn_Date, 
           learn->ir_learn_data_cnt * sizeof(uint32_t));
    /* 保存到当前学习按键对应的存储槽（与协议读发路径互斥） */
    GLOBAL_INT_DISABLE();
    memcpy(&ESairkey.airbutton[ESairkey.AIPstudyKey], &ir_learn_data,
           sizeof(TYPEDEFIRLEARNDATA));
    if (ESairkey.AIPstudyKey < AIRkeyNumber) {
        ESairkey.keyExistence[ESairkey.AIPstudyKey] = 1;
    }
    GLOBAL_INT_RESTORE();
    IR_LOG("ir learn success! data_cnt = %d\r\n", ir_learn_data.ir_learn_data_cnt);
    return true;
}

/* Timer0 ISR：发送推进 / 学习 1ms 节拍与超时 */
__attribute__((section("ram_code"))) void timer0_isr_ram(void)
{
    timer_clear_interrupt(TIMER0);

    /* 发送 */
    if(ir_mode == IR_SEND)
    {
        /* 下一段时序 */
        if(IR_PWM_TIM.IR_pwm_SendNum < IR_PWM_TIM.IR_pwm_Num)
        {
            /* 加载下一个时序间隔 */
            timer_init_count_us_reload(TIMER0, IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++]);
            
            /* 切换 PWM 载波输出/关断状态 */
            IR_PWM_TIM.IR_pwm_state = !IR_PWM_TIM.IR_pwm_state;
            
            /* 操作 PWM 寄存器前临时关闭 BLE 中断，避免冲突 */
            NVIC_DisableIRQ(BLE_IRQn);
            
            if(!IR_PWM_TIM.IR_pwm_state)
            {
                /* 载波输出：50% 占空比 */
                pwm_ctrl->channel[PWM_CHANNEL_5].total_cnt = IR_PWM_TIM.total_count;
                pwm_ctrl->channel[PWM_CHANNEL_5].high_cnt = IR_PWM_TIM.high_count_half;
            }
            else
            {
                /* 载波关断：100% 占空比（等效低电平） */
                pwm_ctrl->channel[PWM_CHANNEL_5].total_cnt = IR_PWM_TIM.total_count;
                pwm_ctrl->channel[PWM_CHANNEL_5].high_cnt = IR_PWM_TIM.total_count;
            }
            
            NVIC_EnableIRQ(BLE_IRQn);
        }
        else if(IR_PWM_TIM.loop)
        {
            /* 重置发送索引和状态 */
            IR_PWM_TIM.IR_pwm_SendNum = 0;
            IR_PWM_TIM.IR_pwm_state = 0;
            
            timer_init_count_us_reload(TIMER0, IR_PWM_TIM.IR_Pwm_State_Date[IR_PWM_TIM.IR_pwm_SendNum++]);
            
            NVIC_DisableIRQ(BLE_IRQn);
            pwm_ctrl->channel[PWM_CHANNEL_5].total_cnt = IR_PWM_TIM.total_count;
            pwm_ctrl->channel[PWM_CHANNEL_5].high_cnt = IR_PWM_TIM.high_count_half;
            NVIC_EnableIRQ(BLE_IRQn);
        }
        else
        {
            if (user_ir_task_id != TASK_ID_NONE) {
                os_event_t ir_event;
                ir_event.event_id = 0;
                ir_event.src_task_id = TASK_ID_NONE;
                ir_event.param = NULL;
                ir_event.param_len = 0;
                os_msg_post(user_ir_task_id, &ir_event);
            } else {
                /* 无 IR 任务时直接回收，避免 IR_Busy 卡死 */
                IR_send_complete_hw_cleanup();
            }
        }
    }
    else if (ir_mode == IR_LEARN)
    {
        if (ir_learn == NULL)
        {
            co_printf("ir_learn malloc error!\r\n");
            return;
        }

        switch (ir_learn->ir_learn_step)
        {
            case IR_WAIT_STOP:
                /* 等待当前信号结束（持续约 200ms 无中断则认为信号已停） */
                IR_LOG("T");
                ir_learn->ir_timer_cnt++;
                
                if (ir_learn->ir_timer_cnt > 200)
                {
                    ir_learn->ir_learn_step = IR_LEARN_GET_DATA;
                    co_printf("ir_learn->ir_learn_step = IR_LEARN_GET_DATA;\r\n");
                    ir_learn->ir_learn_start = 0;
                }
                break;
                
            case IR_LEARN_GET_DATA:
                /* 采集数据中，超时 300ms 后处理学习结果 */
                ir_learn->ir_timer_cnt++;
                
                if ((ir_learn->ir_timer_cnt >= 300) && ir_learn->ir_learn_start)
                {
                    ir_learn->ir_timer_cnt = 0;
                    IR_LOG("IR learn stop  ir_learn_cnt= %d\r\n", ir_learn->ir_learn_data_buf_cnt);

                    /* 步骤1：估算载波频率 */
                    ir_learn->ir_carrier_fre = calculate_carrier_frequency(ir_learn->ir_learn_data_buf, 100);
                    ir_learn_data.ir_carrier_fre = ir_learn->ir_carrier_fre;

                    /* 步骤2：解析脉冲数据 */
                    parse_pulse_data(ir_learn);

                    /* 步骤3：数据有效性校验 */
                    uint8_t IR_learn_state = ir_data_check();

                    /* 步骤4：校验通过则保存，否则重新学习 */
                    if (IR_learn_state == true)
                    {
                        save_learned_data(ir_learn);
                        ESairkey.EIRlearnStatus = false;
                    }
                    else
                    {
                        IR_LOG("ir learn failed!, again!\r\n");
                        ir_learn->ir_learn_step = IR_WAIT_STOP;
                    }
                    
                    IR_LOG("*****************************************\r\n");
                }
                break;
        }
    }
}

#define IR_EXTI_DBG     FR_DBG_OFF
#define IR_EXTI_LOG     FR_LOG(IR_EXTI_DBG)


/* EXTI_3(PC3)：等待静默清零计数 / 采集中记脉冲间隔 */
volatile struct ext_int_t *const ext_int_reg_ir = (struct ext_int_t *)EXTI_BASE;

__attribute__((section("ram_code"))) void exti_isr_ram(void)
{
    uint32_t status, timer_value = 0;
    /* 读取并清除中断状态 */
    status = ext_int_reg_ir->ext_int_status;
    ext_int_reg_ir->ext_int_status = status;
    /* 检查是否为 EXTI_3 中断（红外接收器引脚） */
    if((status >> EXTI_3) & 0x01)
    {
        if(ir_learn == NULL)
        {
            co_printf("ir_learn malloc error!\r\n");
            return;
        }
        switch(ir_learn->ir_learn_step)
        {
            case IR_WAIT_STOP:
                /* 等待停止阶段：有信号则重置超时计数器 */
                IR_EXTI_LOG("P");
                ir_learn->ir_timer_cnt = 0;
                /* 重载定时器以维持 1ms 周期 */
                timerp_ir->load_value.load = timerp_ir->load_value.load;
                break;
            case IR_LEARN_GET_DATA:
                /* 采集阶段：记录脉冲间隔 */
                IR_EXTI_LOG("D");
                /* 计算距离上次中断的时间 (48MHz 下 48000 = 1ms) */
                timer_value = 48000 - timerp_ir->count_value.count;
                timerp_ir->load_value.load = timerp_ir->load_value.load;
                if(ir_learn->ir_learn_start == 0)
                {
                    /* 首次检测到信号，初始化采集参数 */
                    ir_learn->ir_learn_start = 1;
                    ir_learn->ir_carrier_times = 0;
                    ir_learn->ir_timer_cnt = 0;
                    ir_learn->ir_learn_data_buf_cnt = 0;
                    ir_learn->ir_learn_data_cnt = 0;
                    ir_learn_data.ir_learn_data_cnt = 0;
                    IR_EXTI_LOG("S");
                }
                else
                {
                    /* 存储脉冲间隔数据到缓冲区 */
                    if(ir_learn->ir_learn_data_buf_cnt < LERANDATABUFMAX)
                    {
                        /* 高 16 位：定时器溢出次数；低 16 位：当前定时器值 */
                        ir_learn->ir_learn_data_buf[ir_learn->ir_learn_data_buf_cnt++] = 
                            (ir_learn->ir_timer_cnt << 16) | timer_value;
                        ir_learn->ir_timer_cnt = 0;
                    }
                }
                break;
        }
    }
}


/* 开始学习：malloc 上下文、PC3 下降沿、Timer0 1ms */
uint8_t IR_start_learn(void)
{
    if(((ir_learn_data.IR_learn_state & BIT(1)) == BIT(1)))
    {
        co_printf("ir_learn busy!\r\n");
        return false;
    }
    ir_learn = os_zalloc(sizeof(TYPEDEFIRLEARN));
    if(ir_learn == NULL)
    {
        co_printf("ir_learn malloc error!\r\n");
        return false;
    }
    ir_mode = IR_LEARN;
    memset(&ir_learn_data, 0, sizeof(ir_learn_data));
    ir_learn_data.IR_learn_state |= BIT(1);
    #if IR_SLEEP_EN
    system_sleep_disable();
    #endif
    pmu_set_pin_to_CPU(GPIO_PORT_C, BIT(3));
    system_set_port_mux(GPIO_PORT_C, GPIO_BIT_3, PORTC3_FUNC_C3);
    gpio_set_dir(GPIO_PORT_C, GPIO_BIT_3, GPIO_DIR_IN);
    system_set_port_pull(GPIO_PC3, true);
    ext_int_set_port_mux(EXTI_3, EXTI_3_PC3);
    ext_int_set_type(EXTI_3, EXT_INT_TYPE_NEG);  /* 接收器空闲高，有码低 */
    ext_int_enable(EXTI_3);
    NVIC_SetPriority(TIMER0_IRQn, 0);
    NVIC_SetPriority(EXTI_IRQn, 0);
    timer_init(TIMER0, 1000, TIMER_PERIODIC);  /* 1ms */
    timer_run(TIMER0);
    NVIC_EnableIRQ(EXTI_IRQn);
    NVIC_EnableIRQ(TIMER0_IRQn);
    return true;
}

/* 结束学习：关中断、释放缓冲、可选恢复休眠 */
void IR_stop_learn(void)
{

    ir_learn_data.IR_learn_state &= (~ BIT(1));
    /* 清除协议层的"学习中"标志 */
    ESairkey.EIRlearnStatus = false;
    timer_stop(TIMER0);
    ext_int_disable(EXTI_3);
    NVIC_DisableIRQ(TIMER0_IRQn);
    NVIC_SetPriority(TIMER0_IRQn, 2);
    NVIC_SetPriority(EXTI_IRQn,2);
    if(((ir_learn_data.IR_learn_state & BIT(0)) == BIT(0)))
    {
        co_printf("IR learn success! ir_learn_data.ir_carrier_fre = %d\r\n",ir_learn_data.ir_carrier_fre);
        for(uint8_t i = 0; i < ir_learn_data.ir_learn_data_cnt; i++)
        {
            co_printf(" %d",ir_learn_data.ir_learn_Date[i]);
        }
    }
    else
    {
        co_printf("IR learn failed!\r\n");
    }
    if(ir_learn!=NULL)
    {
        os_free(ir_learn);
    }

#if IR_SLEEP_EN
    system_sleep_enable();
#endif

    co_printf("ir_learn = %x\r\n",ir_learn);
}

/* IR 任务：发送完成时回收 PC5/Timer0 */
static int IR_task_func(os_event_t *param)
{
    switch(param->event_id)
    {
        case 0:
            co_printf("IR Send End \r\n");
            IR_send_complete_hw_cleanup();
            break;
    }

    return EVT_CONSUMED;
}

/* 创建 IR 任务，缓存主频分档供 timer 计算用 */
void IR_init(void)
{
    if(user_ir_task_id == TASK_ID_NONE)
        user_ir_task_id = os_task_create(IR_task_func);
    sys_clk = system_get_pclk_config();
    if(sys_clk == 6)
        sys_clk_cfg = 3;
    else if(sys_clk == 12)
        sys_clk_cfg = 2;
    else if(sys_clk == 24)
        sys_clk_cfg = 1;
    else
        sys_clk_cfg = 0;
}


/* --- 调试用 demo --- */
#include "os_timer.h"
uint8_t ir_testdata[10]= {0x01,0x02,0x03,0x04,0x05,0x06,0x07};
os_timer_t os_timer_IR_test;
void os_timer_IR_test_cb(void *arg)
{
    co_printf("IR_stop_learn!\r\n");
    IR_stop_learn();
}

/* demo：编码 ir_testdata 并发送 */
void IR_test_demo0(void)
{
    TYPEDEFIRPWMTIM IR_send_data = {0};

    IR_send_data.ir_carrier_fre = IR_CARRIER_FRE;
    IR_decode(ir_testdata, (uint8_t)sizeof(ir_testdata), &IR_send_data);
    IR_start_send(&IR_send_data);
}

/* demo：开始学习，10s 定时停止 */
void IR_test_demo1(void)
{
#if 1
    if(IR_start_learn())
    {
        co_printf("IR_start_learn!\r\n");
        os_timer_init(&os_timer_IR_test, os_timer_IR_test_cb, NULL);
        os_timer_start(&os_timer_IR_test, 10000, 0);
    }
    else
    {
        /* 启动失败可能原因：内存分配失败 或 正在学习中 */
        co_printf("IR_start_learn Fail!\r\n");
    }
#endif
}

/* demo：回放最近一次学习数据 */
void IR_test_demo2(void)
{
#if 1
    TYPEDEFIRPWMTIM IR_send_data= {0};
    if(((ir_learn_data.IR_learn_state & BIT(0)) == BIT(0)))
    {
        co_printf("send IR learn data,ir_learn_data.ir_learn_data_cnt=%d\r\n",ir_learn_data.ir_learn_data_cnt);
        IR_send_data.ir_carrier_fre = ir_learn_data.ir_carrier_fre;
        IR_send_data.IR_pwm_Num = ir_learn_data.ir_learn_data_cnt;
        memcpy(IR_send_data.IR_Pwm_State_Date,ir_learn_data.ir_learn_Date,IR_send_data.IR_pwm_Num*sizeof(uint32_t));
        IR_start_send(&IR_send_data);
    }
    else
    {
        co_printf("Please perform IR learn first\r\n");
    }

#endif
}


/* 学习结果合法性：停止位/重复帧等启发式 */
static uint8_t ir_data_check(void)
{
    uint16_t i = 0;
    uint8_t find_stop_bit_flag = 0;      /* 已检测到的停止位数量 */
    uint8_t ir_code_bit_cnt = 0;         /* 停止位对应的有效数据长度 */
    uint8_t IR_learn_state = false;

    for(i = 0; i < ir_learn->ir_learn_data_cnt; i++)
    {
        IR_LOG("<%d %d> ", i, ir_learn->ir_learn_Date[i]);
        
        /* 情况1：停止位（12ms ~ 100ms 之间），多数红外协议的帧间隔 */
        if((ir_learn->ir_learn_Date[i] < 100000) && (ir_learn->ir_learn_Date[i] > 12000))
        {
            find_stop_bit_flag++;
            ir_code_bit_cnt = i + 1;
            
            /* 累计检测到 4 个停止位，判定学习成功 */
            if(find_stop_bit_flag >= 4)
            {
                ir_learn->ir_learn_data_cnt = i + 1;
                IR_learn_state = true;
                IR_LOG("T1\r\n");
                break;
            }
        }
        /* 情况2：异常短脉冲（<100us），数据无效 */
        else if(ir_learn->ir_learn_Date[i] < 100)
        {
            IR_learn_state = false;
            IR_LOG("T2\r\n");
            break;
        }
        /* 情况3：长间隔（>=100ms），信号结束或重复码间隔 */
        else if(ir_learn->ir_learn_Date[i] >= 100000)
        {
            if((i > 6) && (find_stop_bit_flag))
            {
                /* 判断重复码结构 */
                uint8_t ir_data_len1 = i + 1 - ir_code_bit_cnt;     /* 重复码段长度 */
                uint8_t ir_data_len2 = ir_code_bit_cnt / find_stop_bit_flag;  /* 单帧长度 */
                
                IR_LOG("**********************->:%d %d\r\n", ir_data_len2, ir_data_len1);
                
                /* 帧长度一致，或兼容 Gemini-C10 协议格式则保留完整数据 */
                if((ir_data_len1 == ir_data_len2) || ((ir_data_len1 > 10) && (ir_data_len2 == 22)))
                {
                    ir_learn->ir_learn_data_cnt = i + 1;
                }
                else
                {
                    ir_learn->ir_learn_data_cnt = ir_code_bit_cnt;
                }
                
                /* 单帧数据量 >10 才认为有效 */
                if(ir_data_len2 > 10)
                {
                    IR_learn_state = true;
                }
            }
            /* 未检测到停止位但数据量足够，也认为有效 */
            else if((find_stop_bit_flag == 0) && (i > 10))
            {
                IR_learn_state = true;
            }

            IR_LOG("T3\r\n");
            break;
        }
    }
    
    IR_LOG("i = %d\r\n", i);
    return IR_learn_state;
}

/* 协议入口：对 keynumber 开始学习，10s 自动停 */
void ESOAAIR_IRskeystudy(uint8_t keynumber)
{
    ESairkey.EIRlearnStatus = true;
    if(IR_start_learn())
    {
        ESairkey.AIPstudyKey = (studyIRKeypress)keynumber;
        co_printf("IR_start_learn!\r\n");
        /* 10 秒后自动停止学习 */
        os_timer_init(&os_timer_IR_test, os_timer_IR_test_cb, NULL);
        os_timer_start(&os_timer_IR_test, 10000, 0);
    }
    else
    {
        ESairkey.EIRlearnStatus = false;
        co_printf("IR_start_learn Fail!\r\n");
    }
}

/* 协议入口：发送已学习槽 keynumber 的时序 */
void ESOAAIR_IRsend(uint8_t keynumber)
{
    TYPEDEFIRPWMTIM IR_send_data = {0};
    TYPEDEFIRLEARNDATA slot;

    if (keynumber >= AIRkeyNumber) {
        co_printf("IR key out of range\r\n");
        return;
    }

    GLOBAL_INT_DISABLE();
    memcpy(&slot, &ESairkey.airbutton[keynumber], sizeof(slot));
    GLOBAL_INT_RESTORE();

    if ((slot.IR_learn_state & BIT(0)) == BIT(0)) {
        co_printf("send IR learn data,ir_learn_data.ir_learn_data_cnt=%d\r\n",
                  slot.ir_learn_data_cnt);
        IR_send_data.ir_carrier_fre = slot.ir_carrier_fre;
        IR_send_data.IR_pwm_Num = slot.ir_learn_data_cnt;
        memcpy(IR_send_data.IR_Pwm_State_Date, slot.ir_learn_Date,
               IR_send_data.IR_pwm_Num * sizeof(uint32_t));

        IR_start_send(&IR_send_data);
    } else {
        co_printf("Please perform IR learn first\r\n");
    }
}
