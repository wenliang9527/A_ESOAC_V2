
#include "LED.h"


uint16_t led_task_id; /**<brief LED任务id */
uint8_t led_status=0;   //LED状态
os_event_t evt={0};    //消息事件结构体





/**\brief LED引脚IO：PA4初始化 */
void LED_gpio_Init(void)
{
    system_set_port_mux(GPIO_PORT_B, GPIO_BIT_1, PORTB1_FUNC_B1); // 复用为IO口
    gpio_set_dir(GPIO_PORT_B, GPIO_BIT_1, GPIO_DIR_OUT); // 输出模式
	  system_set_port_mux(GPIO_PORT_B, GPIO_BIT_2, PORTB1_FUNC_B1); // 复用为IO口
    gpio_set_dir(GPIO_PORT_B, GPIO_BIT_2, GPIO_DIR_OUT); // 输出模式
}

/**<brief LED任务函数 */
static int led_task_func(os_event_t *param)
{
    /* 根据不同的事件id执行相应的操作 */
    switch(param->event_id)
    {
        case LED_ON:
            gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_1, 0);
						gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_2, 1);
            break;
        case LED_OFF:
            gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_1, 1);
						gpio_set_pin_value(GPIO_PORT_B, GPIO_BIT_2, 0);
            break;
    }
    return EVT_CONSUMED;
}
/**<brief 用户任务初始化函数 */
void LED_task_init(void)
{
   led_task_id = os_task_create(led_task_func);
}

//初始化一个软件定时器

os_timer_t LED_timer;
/**\brief 软件定时器中断服务函数 */
static void LED_swtimer_fn(void *param)
{
    led_status ^= 1;            /* LED状态取反 */
    evt.event_id = led_status;  /* 任务事件id赋值 */
    os_msg_post(led_task_id, &evt); /* 向LED任务抛送消息事件 */
}

void user_LEDtimer_init(void)
{
 os_timer_init(&LED_timer,LED_swtimer_fn, NULL);
 os_timer_start(&LED_timer,1000, true); //启动一个 1s 的定时器。
}

void LED_INIT(void)
	{
		LED_gpio_Init();
		LED_task_init();
		user_LEDtimer_init();
		co_printf("LED init  OK\r\n");
	}
	
	
	
	
	
	
	













