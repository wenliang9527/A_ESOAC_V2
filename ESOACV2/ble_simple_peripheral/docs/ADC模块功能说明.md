# ADC（模数转换）模块功能说明

本文档描述 ESOACV2 工程中 ADC 相关软件实现，覆盖芯片驱动层（`driver_adc`）与应用封装层（`frADC`），并与当前代码保持一致，供开发与维护查阅。

---

## 1. 功能概述

### 1.1 驱动层（SAR ADC Driver）

基于 FR801xH 平台 SAR ADC 硬件寄存器（基址 `SAR_ADC_BASE`），提供：

- **输入源（trans source）**：`VBAT`（片内电池分压通路）或 `PAD`（GPIO 模拟引脚）。
- **工作模式**：单通道 **固定模式（FIXED）** 与多通道 **轮询模式（LOOP）**；多通道时 `channels` 低 4 位为通道使能位图。
- **参考电压（reference）**：内部多档基准（约 1.2V～1.5V 配置项）或 **AVDD**（与 IO 供电相关，亦称 IOVDD）。
- **PAD 模拟路由**：可选直通采样、经内部分压后再采样、或经缓冲后再采样；分压有效时需配置分压总阻与分压比。
- **采样时钟**：可选 **24 MHz** 或 **64 kHz** 时钟源，经分频后送入 ADC 核心；驱动注释说明 ADC 核心约每 **13 个时钟周期** 产生一次转换结果。
- **可选中断 + FIFO**：在 FIXED 模式下可配合回调做连续采样（FIFO 半满中断）；本工程业务路径未使用该方式。

数据寄存器中采样结果为 **10 bit 有效数据**（见下文寄存器抽象）。

### 1.2 应用层（frADC）

本工程将 ADC 用于 **双路 PAD 输入（LOOP 多通道轮询）**：

- **PD4 / ADC0（通道 bit0）**：功率前端，`ESAirdata.AIRpowerADCvalue`，经 `Get_Power_Value` 换算功率。
- **PD5 / ADC1（通道 bit1）**：NTC 温度前端，`ESAirdata.AIRntcADCvalue`，经 `Get_Temperature_Value` 换算摄氏温度。
- 参考：**AVDD**（`ADC_REFERENCE_AVDD`）；路由：**PAD 直通采样**（`pad_to_sample = 1`）。
- `fr_ADC_init` 在 `adc_init` 之后调用 **`adc_enable(NULL, NULL, 0)`**，与 `driver_adc.h` 用法示例一致，用于启动转换。

---

## 2. 核心工作流程

### 2.1 驱动初始化：`adc_init`

主要步骤（逻辑顺序）：

1. **参考电压校准（仅首次）**：若尚未校准，则从 **eFuse** 读取；若无效则从 **Flash OTP**（偏移 `0x1000`，魔数 `0x31303030`）读取并计算内部基准与 AVDD 相关毫伏值，再经外部符号 **`adc_ref_internal_trim`** 做比例修正，写入静态变量 `adc_ref_internal`、`adc_ref_avdd`。
2. **环境清零**：`adc_env` 置零。
3. **VBAT 特例**：若 `src == ADC_TRANS_SOURCE_VBAT`，强制 `channels = 0x01`。
4. **PMU 与 ADC 模拟前端**：写 `PMU_REG_ADC_CTRL5` 等；按 `clk_sel` 补全 `clk_div` 缺省位；配置采样时钟、参考、路由；若使能分压则调用 `adc_dividor_config`。
5. **中断控制**：`int_ctl` 整体清零后设置 `length` 字段（代码中为 `0x0E`）；关闭 FIFO 与 `adc_en`。
6. **模式选择**：若 `channels` 中多于 1 个 bit 置位 → **LOOP**，否则 → **FIXED** 并解析出唯一通道号写入 `ch_sel`。

校准与 `adc_env` 初始化、VBAT 通道强制见：

```398:432:ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c
void adc_init(struct adc_cfg_t *cfg)
{
    uint8_t channels = cfg->channels;
    uint32_t data[5];

    if(adc_ref_calib == false)
    {
        adc_ref_calib = true;

        efuse_read(&data[0], &data[1], &data[2]);
        if(data[2] != 0)
        {
            adc_ref_internal = ((data[2] >> 12) & 0xfff);
            adc_ref_avdd = (data[2] & 0xfff);
        }
        else
        {
            flash_OTP_read(0x1000, 5*sizeof(uint32_t), (void *)data);
            if(data[0] == 0x31303030) {
                adc_ref_internal = ((400*1024)/(data[3] / 32) + (800*1024)/(data[4] / 32)) / 2;
                adc_ref_avdd = ((2400*1024)/(data[1] / 32) + (800*1024)/(data[2] / 32)) / 2;
            }
        }
        uint32_t tmp = adc_ref_internal * adc_ref_internal_trim;
        adc_ref_internal = tmp / 80;
        tmp = adc_ref_avdd * adc_ref_internal_trim;
        adc_ref_avdd = tmp / 80;
    }

    memset((void *)&adc_env, 0, sizeof(adc_env));

    if(cfg->src == ADC_TRANS_SOURCE_VBAT) {
        channels = 0x01;
    }
    adc_env.en_channels = channels;
```

时钟、参考、路由、中断与 FIXED/LOOP 模式选择见：

```434:487:ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c
    ool_write(PMU_REG_ADC_CTRL5, ool_read(PMU_REG_ADC_CTRL5) | 0x70);

    if(cfg->clk_sel == ADC_SAMPLE_CLK_64K_DIV13) {
        if((cfg->clk_div & 0x30) == 0 ) {
            cfg->clk_div |= 0x30;
        }
    }
    else {
        if((cfg->clk_div & 0x0f) == 0 ) {
            cfg->clk_div |= 0x0f;
        }
    }
    adc_set_sample_clock(cfg->clk_sel, cfg->clk_div);

    adc_set_reference(cfg->ref_sel, cfg->int_ref_cfg);

    adc_route_config(cfg->src, cfg->route.pad_to_sample, cfg->route.pad_to_div, cfg->route.pad_to_buffer);

    if(cfg->route.pad_to_div) {
        adc_dividor_config(cfg->div_res, cfg->div_cfg);
    }

    /* disable all interrupts */
    *(uint32_t *)&adc_regs->int_ctl = 0;
    
    adc_regs->int_ctl.length = 0x0E;

    /* confirm adc disable and fifo diable */
    adc_regs->ctrl.adc_en = 0;
    adc_regs->ctrl.fifo_en = 0;
    
    if(channels & (channels-1)) {
        /* multi channels */
        adc_regs->ctrl.adc_mode = ADC_TRANS_MODE_LOOP;
        adc_regs->ctrl.ch_en = channels;

        adc_env.mode = ADC_TRANS_MODE_LOOP;
    }
    else {
        /* singal channel */
        uint8_t i;
        
        adc_regs->ctrl.adc_mode = ADC_TRANS_MODE_FIXED;
        
        for(i=0; i<ADC_CHANNELS; i++) {
            if((1<<i) & channels) {
                break;
            }
        }
        adc_regs->ctrl.ch_sel = i;

        adc_env.mode = ADC_TRANS_MODE_FIXED;
    }
}
```

### 2.2 启动转换：`adc_enable`

- 去除掉电、使能 ADC 电源与核心、延时后置位 `adc_en`。
- **FIXED** 模式下等待 `data_valid`。
- 若传入非空 **callback**：仅允许 FIXED 模式；使能 FIFO、`fifo_hfull` 中断并 `NVIC_EnableIRQ(ADC_IRQn)`，用于批量采样完成后回调。

```238:272:ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c
bool adc_enable(void (*callback)(uint16_t *, uint32_t), uint16_t *buffer, uint32_t length)
{
    if((callback != NULL) && (adc_env.mode != ADC_TRANS_MODE_FIXED)) {
        return false;
    }
    
    /* remove ADC PD and enable ADC core */
    ool_write(PMU_REG_ADC_CTRL6, (ool_read(PMU_REG_ADC_CTRL6) & (~(PMU_REG_ADC_PD_CTL_PO|PMU_REG_ADC_PWR_EN|PMU_REG_ADC_PWR_SEL)))  \
                                    | (PMU_REG_ADC_PWR_EN | PMU_REG_ADC_PWR_SEL));
    ool_write(PMU_REG_ADC_CTRL5, ool_read(PMU_REG_ADC_CTRL5) & (~PMU_REG_ADC_PD));
    ool_write(PMU_REG_ADC_CTRL1, ool_read(PMU_REG_ADC_CTRL1) | PMU_REG_ADC_EN);
    
    co_delay_100us(1);

    adc_regs->ctrl.adc_en = 1;

    if(adc_env.mode == ADC_TRANS_MODE_FIXED) {
        while(adc_regs->ctrl.data_valid == 0);
    }
    else {
        /* delay for some time to wait data valid */
    }

    if(callback) {
        adc_env.callback = callback;
        adc_env.buffer = buffer;
        adc_env.length = length;
        
        adc_regs->ctrl.fifo_en = 1;
        adc_regs->int_ctl.fifo_hfull_en = 1;
        NVIC_EnableIRQ(ADC_IRQn);
    }

    return true;
}
```

### 2.3 读取结果：`adc_get_result`

- **VBAT** 或 **FIXED 模式**：直接取 `data[0].data`（10 bit）写入 `buffer`。
- **PAD + LOOP**：根据 `adc_env.en_channels` 对 `adc_regs->data[i]` 做通道顺序整理，再按调用方 `channels` 位掩码依次输出。

```307:341:ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c
void adc_get_result(enum adc_trans_source_t src, uint8_t channels, uint16_t *buffer)
{
#if 1
    if((src == ADC_TRANS_SOURCE_VBAT)
        || (adc_env.mode == ADC_TRANS_MODE_FIXED)) {
        *buffer = adc_regs->data[0].data;
    }
    else if(src == ADC_TRANS_SOURCE_PAD) {
        uint16_t data[ADC_CHANNELS];
        uint8_t first_en_chn, en_chns, prev_en_chan;

        first_en_chn = 0xff;
        en_chns = adc_env.en_channels;

        for(uint8_t i=(ADC_CHANNELS-1); i!=0xff; i--) {
            if(en_chns & (1<<i)) {
                if(first_en_chn == 0xff) {
                    first_en_chn = i;
                }
                else {
                    data[prev_en_chan] = adc_regs->data[i].data;
                }
                prev_en_chan = i;
            }
        }
        data[prev_en_chan] = adc_regs->data[first_en_chn].data;
        
        for(uint8_t i=0; i<ADC_CHANNELS; i++) {
            if(channels & (1<<i)) {
                *buffer++ = data[i];
            }
        }
    }
#endif
}
```

### 2.4 关闭：`adc_disable`

关闭 NVIC 中断、清 `adc_en`，并通过 PMU 寄存器关闭 ADC 使能与掉电控制。

### 2.5 中断服务：`adc_isr`

在 FIFO 半满时批量读取 `fifo_data` 填入用户缓冲区；当累计长度达到 `length` 时关闭 ADC 并调用用户回调。本工程轮询路径不依赖该流程。

### 2.6 应用层调用链

1. **系统启动**：`user_entry_after_ble_init` 中调用 `fr_ADC_star()`，完成 **PD4/PD5** 复用、`adc_init` + `adc_enable`、注册 **10 s** 周期软件定时器；回调 `fr_adc_Thread` 内调用 `fr_ADC_sample_dual()`（`adc_get_result(..., 0x03, buf)`）更新 **`AIRpowerADCvalue` / `AIRntcADCvalue`**。

```155:185:ESOACV2/ble_simple_peripheral/code/proj_main.c
void user_entry_after_ble_init(void)
{
    if (s_user_after_ble_done) {
        return;
    }
    s_user_after_ble_done = 1;

    co_printf("BLE Peripheral\r\n");

    system_sleep_disable();

    /* task, protocol, device_config only (no MQTT until BLE is up) */
    app_task_init_early();

    /* ???????? */
    LED_INIT();
    fr_ADC_star();
    fros_uarttmr_INIT();
    FR_rtcinit();

    simple_peripheral_init();

    ota_gatt_add_service();
    ESAIR_gatt_add_service();

    /* mqtt_config_load, mqtt_handler, periodic timers */
    app_task_init_mqtt_timers();

    /* Start MQTT connection in non-blocking mode to avoid blocking BLE stack callback path. */
    app_task_start_reconnect_timer();
}
```

```8:46:ESOACV2/ble_simple_peripheral/usercode/frADC.c
void fr_ADC_init(void)
{
    struct adc_cfg_t cfg;
    memset((void *)&cfg, 0, sizeof(cfg));

    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_4, PORTD4_FUNC_ADC0);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_5, PORTD5_FUNC_ADC1);

    cfg.src = ADC_TRANS_SOURCE_PAD;
    cfg.ref_sel = ADC_REFERENCE_AVDD;
    cfg.channels = 0x03;
    cfg.route.pad_to_sample = 1;
    cfg.clk_sel = ADC_SAMPLE_CLK_24M_DIV13;
    cfg.clk_div = 0x3f;

    adc_init(&cfg);
    adc_enable(NULL, NULL, 0);
}

static void fr_ADC_sample_dual(void)
{
    uint16_t adc_buf[2];
    adc_get_result(ADC_TRANS_SOURCE_PAD, 0x03, adc_buf);
    ESAirdata.AIRpowerADCvalue = adc_buf[0];
    ESAirdata.AIRntcADCvalue = adc_buf[1];
}

void fr_ADC_star(void)
{
    fr_ADC_init();
    os_timer_init(&adc_timer, fr_adc_Thread, NULL);
    os_timer_start(&adc_timer, ADC_SAMPLE_PERIOD_MS, 1);
}
```

2. **传感器任务**：`sensor_read_timer_func` 中调用 `fr_ADC_send()`（内部同样刷新双路原始值），随后 **`Get_Power_Value(AIRpowerADCvalue)`**、**`Get_Temperature_Value(AIRntcADCvalue)`** 写入 `AIRpowervalue`、`temp_celsius`，并按条件通过 BLE/MQTT 上报；定时器周期为 **`ADC_SAMPLE_PERIOD_MS`（10000）**，与 `fr_ADC_star` 一致。若编译选项 **`FR_ADC_PERIODIC_UART_REPORT`** 为 1，还会在 UART1 上额外发送功率/温度协议帧（`PROTOCOL_SRC_UART`）。

```59:90:ESOACV2/ble_simple_peripheral/usercode/app_task.c
// 传感器读取定时器回调
static void sensor_read_timer_func(void *arg)
{
    // 读取ADC功率值
    fr_ADC_send();

    // 计算功率值（需要根据实际电路调整）
    float power_value = Get_Power_Value(ESAirdata.AIRpowerADCvalue);
    ESAirdata.AIRpowervalue = power_value;

    // 计算温度值
    float temp_value = Get_Temperature_Value(ESAirdata.AIRntcADCvalue);
    ESAirdata.temp_celsius = temp_value;

    co_printf("Power: %.2fW, Temp: %.2fC\r\n", power_value, temp_value);

    /* BLE：有链路时再组帧，通知仍受 ESAIR CCC 约束 */
    if (gap_get_connect_num() > 0) {
        protocol_send_power(0);
        protocol_send_temperature(0);
    }

    /* MQTT：已连接时上云（与 BLE 独立） */
    if (R_atcommand.MLinitflag == ML307AMQTT_OK) {
        mqtt_handler_send_power();
        mqtt_handler_send_temperature();
    }

#if FR_ADC_PERIODIC_UART_REPORT
    protocol_send_power(PROTOCOL_SRC_UART);
    protocol_send_temperature(PROTOCOL_SRC_UART);
#endif
}
```

3. **协议查询**：收到 `CMD_GET_ADC_DATA` 时先 `fr_ADC_send()` 刷新采样，再 **`protocol_send_adc_raw(source)`** 下发 **CMD_GET_ADC_DATA** 应答帧，载荷为 **4 字节小端**：`uint16` PD4/ADC0 原始值 + `uint16` PD5/ADC1 原始值（**与早期仅 `CMD_RESPONSE` 成功码的兼容策略不同，上位机需按新载荷解析**）。

```418:421:ESOACV2/ble_simple_peripheral/usercode/protocol.c
        case CMD_GET_ADC_DATA:
            fr_ADC_send();
            protocol_send_adc_raw(source);
            break;
```

---

## 3. 关键参数与配置

### 3.1 枚举与结构体（`driver_adc.h`）

- **`adc_reference_t`**：`ADC_REFERENCE_INTERNAL`、`ADC_REFERENCE_AVDD`。
- **`adc_internal_ref_t`**：内部参考档位 `1_2 / 1_3 / 1_4 / 1_5`（与寄存器配置域对应）。
- **`adc_trans_source_t`**：`VBAT`、`PAD`。
- **`adc_sample_clk_t`**：`ADC_SAMPLE_CLK_64K_DIV13`、`ADC_SAMPLE_CLK_24M_DIV13`。
- **采样率（头文件注释）**：
  - `clk_sel == ADC_SAMPLE_CLK_24M_DIV13`：`sample_rate = 24M / (1 + clk_div[3:0]) / 13`
  - `clk_sel == ADC_SAMPLE_CLK_64K_DIV13`：`sample_rate = 64K / (1 + clk_div[5:4]) / 13`
- **`clk_div`**：`clk_div[3:0]` 取值 1～15；`clk_div[5:4]` 取值 1～3（见头文件注释）。
- **`struct adc_cfg_t`**：`src`、`route`（`pad_to_sample`、`pad_to_div`、`pad_to_buffer` 同一时间仅应置位其一）、`ref_sel`、`int_ref_cfg`、`div_res`/`div_cfg`（仅 `pad_to_div` 时有效）、`clk_sel`、`clk_div`、`channels`。

```100:130:ESOACV2/ble_simple_peripheral/components/driver/include/driver_adc.h
struct adc_cfg_t { 
    enum adc_trans_source_t src;

    struct adc_cfg_route_t {
        /* only one bit can be set at the same time */
        uint8_t pad_to_sample:1;
        uint8_t pad_to_div:1;
        uint8_t pad_to_buffer:1;
        uint8_t reserved:5;
    } route;

    enum adc_reference_t ref_sel;
    enum adc_internal_ref_t int_ref_cfg;

    enum adc_dividor_total_res_t div_res;
    enum adc_dividor_cfg_t div_cfg;

    /* 
     * sample rate setting:
     * sample_rate = 24M / (1+clk_div[3:0]) / 13 when clk_sel is ADC_SAMPLE_CLK_24M_DIV13
     * sample_rate = 64K / (1+clk_div[5:4]) / 13 when clk_sel is ADC_SAMPLE_CLK_64K_DIV13
     */
    enum adc_sample_clk_t clk_sel;
    uint8_t clk_div;

    uint8_t channels;
};
```

### 3.2 本工程实际配置小结

| 项 | 取值 |
|----|------|
| 引脚 | `GPIO_BIT_4` → `PORTD4_FUNC_ADC0`（功率）；`GPIO_BIT_5` → `PORTD5_FUNC_ADC1`（NTC） |
| `src` | `ADC_TRANS_SOURCE_PAD` |
| `ref_sel` | `ADC_REFERENCE_AVDD` |
| `channels` | `0x03`（bit0+bit1，**LOOP** 多通道） |
| `route` | `pad_to_sample = 1` |
| `clk_sel` | `ADC_SAMPLE_CLK_24M_DIV13` |
| `clk_div` | `0x3f` |
| 启动 | `adc_init` 后 **`adc_enable(NULL, NULL, 0)`** |

---

## 4. 接口定义

### 4.1 驱动 API（`driver_adc.h`）

| 接口 | 说明 |
|------|------|
| `void adc_init(struct adc_cfg_t *cfg)` | 按配置完成校准（首次）、PMU/路由/模式初始化 |
| `bool adc_enable(void (*cb)(uint16_t *, uint32_t), uint16_t *buf, uint32_t len)` | 启动转换；非空回调仅 FIXED 模式合法 |
| `void adc_disable(void)` | 停止转换并低功耗相关关闭 |
| `void adc_get_result(enum adc_trans_source_t src, uint8_t channels, uint16_t *buffer)` | 读取最近一次/当前映射通道结果 |
| `uint16_t adc_get_ref_voltage(enum adc_reference_t ref)` | 返回当前缓存的参考电压（mV） |
| `void adc_set_ref_voltage(uint16_t ref_avdd, uint16_t ref_internal)` | 外部校准后写入参考毫伏值 |

**中断**：`adc_isr` 在启动文件向量表中注册（`ADC_IRQn`），与 `adc_enable` 的 FIFO 中断路径配合。

### 4.2 应用 API（`frADC.h` / `frADC.c`）

| 接口/宏 | 说明 |
|---------|------|
| `fr_ADC_init` | 双引脚复用、`adc_cfg_t`（`channels=0x03`）、`adc_init` + `adc_enable(NULL,NULL,0)` |
| `fr_ADC_star` | `fr_ADC_init` + 初始化并启动 **`ADC_SAMPLE_PERIOD_MS`** 周期定时器 |
| `fr_ADC_send` | `fr_ADC_sample_dual()`：更新 `AIRpowerADCvalue` 与 `AIRntcADCvalue`，调试打印 |
| `Get_Power_Value(uint16_t adc_val)` | 由 ADC 码值换算功率（W），满量程 **`ADC_CODE_FULL_SCALE`（1023）** |
| `Get_Temperature_Value(uint16_t adc_val)` | 由 ADC 码值换算温度（°C）；`adc_val<1` 返回 **`FR_ADC_TEMP_INVALID_C`** |
| `ADC_VREF_MV` / `ADC_CODE_FULL_SCALE` | 参考电压与 10 bit 满量程码（换算一致） |
| `ADC_POWER_VDIV_REF_MV` / `ADC_POWER_I_COEFF` / `ADC_POWER_AC_V` | 功率链路系数（可按原理图标定） |
| `NTC_PULLUP_OHMS` / `NTC_BETA` | NTC 分压与 Beta 参数 |
| `ADC_CODE_MAX_UINT` | 与 10 bit 满量程一致的整数钳位上界（**1023**），用于 `Get_Power_Value` |
| `ADC_SAMPLE_PERIOD_MS` | 采样周期 **10000 ms**（`fr_ADC_star` 与传感器定时器共用） |
| `FR_ADC_PERIODIC_UART_REPORT` | 默认 0；为 1 时传感器周期内经 UART1 追加功率/温度帧 |
| `protocol_send_adc_raw`（[protocol.c](ESOACV2/ble_simple_peripheral/usercode/protocol.c)） | `CMD_GET_ADC_DATA` 应答 4 字节小端双路原始值 |
| `PROTOCOL_SRC_BLE` / `MQTT` / `UART`（[protocol.h](ESOACV2/ble_simple_peripheral/usercode/protocol.h)） | 协议分发来源常量 |

---

## 5. 数据处理逻辑

### 5.1 原始数码与硬件位宽

驱动中数据寄存器字段为 **10 bit**：

```40:43:ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c
struct adc_reg_data_t {
    uint32_t data:10;
    uint32_t reserved:22;
};
```

理论满量程数码为 **0～1023**（具体线性度以数据手册为准）。

### 5.2 电压与功率（`Get_Power_Value`）

当前实现：

1. `adc_val` 钳位到 **`ADC_CODE_MAX_UINT`（1023）**（与 10 bit 一致）。
2. `voltage_mv = code_adc * ADC_VREF_MV / ADC_CODE_FULL_SCALE`（**1023.0f**）。
3. `current_a = (voltage_mv / ADC_POWER_VDIV_REF_MV) * ADC_POWER_I_COEFF`。
4. 返回 `current_a * ADC_POWER_AC_V`。

```67:77:ESOACV2/ble_simple_peripheral/usercode/frADC.c
float Get_Power_Value(uint16_t adc_val)
{
    uint16_t code_adc = adc_val;
    if (code_adc > ADC_CODE_MAX_UINT) {
        code_adc = ADC_CODE_MAX_UINT;
    }

    float voltage_mv = ((float)code_adc * (float)ADC_VREF_MV) / ADC_CODE_FULL_SCALE;
    float current_a = (voltage_mv / ADC_POWER_VDIV_REF_MV) * ADC_POWER_I_COEFF;
    return current_a * ADC_POWER_AC_V;
}
```

**说明**：满量程与驱动 **10 bit** 对齐；功率系数见 `frADC.h` 宏，可按互感/取样电路标定。输入 **`adc_val` 应取自 PD4/ADC0 原始值 `AIRpowerADCvalue`**。

### 5.3 温度（`Get_Temperature_Value`）

采用上拉/分压模型近似 NTC（β 模型），满量程码 **`ADC_CODE_FULL_SCALE`**，电阻 **`NTC_PULLUP_OHMS`**，β **`NTC_BETA`**。若 **`adc_val < 1`**，返回 **`FR_ADC_TEMP_INVALID_C`（-999）**，避免除零。

```56:67:ESOACV2/ble_simple_peripheral/usercode/frADC.c
float Get_Temperature_Value(uint16_t adc_val)
{
    if (adc_val < 1) {
        return FR_ADC_TEMP_INVALID_C;
    }

    float code = ADC_CODE_FULL_SCALE;
    float resistance = (code / (float)adc_val - 1.0f) * NTC_PULLUP_OHMS;
    float inv_t = (1.0f / 298.15f) + (1.0f / NTC_BETA) * logf(resistance / NTC_PULLUP_OHMS);
    float temp_k = 1.0f / inv_t;
    return temp_k - 273.15f;
}
```

输入 **`adc_val` 应取自 PD5/ADC1 原始值 `AIRntcADCvalue`**。

### 5.4 全局数据落点

`Airparameters`（`aircondata.h`）中与 ADC 相关字段包括：**`AIRpowerADCvalue`（PD4/ADC0 原始码）**、**`AIRntcADCvalue`（PD5/ADC1 原始码）**、`AIRpowervalue`（功率）、`temp_celsius`（温度）等，由 `frADC` 采样与 `app_task.c` 中传感器定时器回调更新。

---

## 6. 错误处理机制

### 6.1 驱动层

- **`adc_enable`**：若 `callback != NULL` 且当前非 FIXED 模式，返回 `false`，不进入中断采样流程。
- **`adc_init`**：当 `clk_div` 对应位为 0 时，按源码补默认值（24M 路径补低 4 位、64K 路径补高 2 位），避免因除零或非法分频导致异常配置。
- **ADC 错误中断**：`adc_init` 将 `int_ctl` 清零，未使能 `error_en`；**当前工程无 ADC 硬件错误上报与恢复策略**。
- **`adc_isr`**：仅响应 FIFO 半满；完成指定长度后 `adc_disable` 并调用回调。

### 6.2 应用层

- **`frADC`**：`Get_Temperature_Value` 在 **`adc_val < 1`** 时返回 **`FR_ADC_TEMP_INVALID_C`**，避免除零；上层可据此丢弃或保持上次有效温度。
- **`fr_ADC_send`**：`co_printf` 用于调试输出，不参与协议帧校验。

---

## 7. 使用注意事项

1. **双通道顺序**：`adc_buf[0]` 对应 **ADC0（PD4 功率）**、`adc_buf[1]` 对应 **ADC1（PD5 NTC）**（与当前驱动 LOOP 输出顺序一致）。若实测与硬件对调，在 `fr_ADC_sample_dual` 中交换对 `AIRpowerADCvalue` / `AIRntcADCvalue` 的赋值并保留注释。
2. **参考电压一致性**：配置为 `ADC_REFERENCE_AVDD`，而应用换算使用宏 `ADC_VREF_MV`（3300 mV）；驱动侧 `adc_get_ref_voltage` 可能因校准与 AVDD 实际值不同。**业务物理量换算以当前 `frADC` 宏与公式为准**，若需高精度应做单点/多点标定并统一参考来源。
3. **LOOP 采样率**：`driver_adc.h` 注释指出多通道时默认较低采样率路径；若功率通道动态响应不足，需评估硬件与是否改分时单通道等方案。
4. **扩展更多通道 / VBAT / FIFO 采样**：需重新配置 `struct adc_cfg_t`、`adc_get_result` 的 `channels` 与缓冲区长度，并在使用回调时遵守 FIXED 模式限制。
5. **定时与采样语义**：存在两条 **`ADC_SAMPLE_PERIOD_MS`** 周期路径：`fr_adc_Thread` 与 `sensor_read_timer_func` 均会触发采样。
6. **上位机兼容**：`CMD_GET_ADC_DATA` 应答载荷已改为 **4 字节双路原始 ADC**；依赖旧版“仅 CMD_RESPONSE 成功码”的主机需升级解析逻辑。
7. **内部计划图示**：历史计划中的 mermaid（如 `adc_init` 未连接 `adc_enable`、单通道 D4）**已过期**，以本文档与当前 `frADC.c` 为准。

---

## 附录 A 维护注意（优化后）

| 编号 | 主题 | 说明 |
|------|------|------|
| A1 | `CMD_GET_ADC_DATA` 载荷变更 | 应答为 **CMD_GET_ADC_DATA + 4B 小端原始值**，非仅通用 `CMD_RESPONSE`。 |
| A2 | 无效温度哨兵 | `Get_Temperature_Value` 在 `adc_val<1` 时返回 **-999°C**，业务层勿直接当物理温度展示。 |
| A3 | UART 周期上报 | **`FR_ADC_PERIODIC_UART_REPORT`** 默认 0；开启后每传感器周期经 UART1 多发功率/温度帧，注意总线负载。 |

---

## 附录 B 公式与符号速查

- 驱动采样率：见 `driver_adc.h` 中 `struct adc_cfg_t` 注释（24M 与 64K 两套公式）。
- VBAT 电压估算（驱动头文件示例思路）：`vbat_mv ≈ (result * 4 * ref_mv) / 1024`（示例针对内部参考场景，具体以数据手册为准）。
- 应用功率（当前代码）：`P ≈ ((code/1023)*ADC_VREF_MV / ADC_POWER_VDIV_REF_MV) * ADC_POWER_I_COEFF * ADC_POWER_AC_V`（`code` 钳位 ≤1023，系数见 `frADC.h`）。
- 应用温度（当前代码）：`NTC_PULLUP_OHMS`、`NTC_BETA`、`ADC_CODE_FULL_SCALE` 见 `frADC.h`；参考温度 25°C（298.15 K）。

---

*文档版本与工程源码同步核对路径：`ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c`、`driver_adc.h`、`usercode/frADC.c`、`frADC.h`、`usercode/protocol.c`、`protocol.h`。*
