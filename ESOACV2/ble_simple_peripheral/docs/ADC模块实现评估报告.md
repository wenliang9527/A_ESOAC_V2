# ADC 模块功能实现评估报告

本文档基于当前仓库实现（`frADC.c`、`app_task.c`、`protocol.c`、`driver_adc.c` 等）对 ADC 相关能力进行评估，供评审与后续改进参考。

**修订说明（优化后）**：换算已统一 **10 bit 满量程 1023**；温度在 `adc_val<1` 时返回哨兵值；`CMD_GET_ADC_DATA` 应答含 **4 字节双路原始 ADC**；可选 **`FR_ADC_PERIODIC_UART_REPORT`** 周期经 UART1 发送温功率协议帧。

---

## 1. 评估范围与结论摘要

| 能力项 | 结论 | 说明 |
|--------|------|------|
| 双路模拟量采集并转为数字量 | **已具备（架构上满足）** | PD4/ADC0、PD5/ADC1，LOOP 双通道，`adc_init` + `adc_enable` + `adc_get_result` |
| 按公式换算 °C / W | **已实现；满量程与 10 bit 已对齐，计量精度仍依赖标定** | 使用 `ADC_CODE_FULL_SCALE`（1023）及 `frADC.h` 中功率/NTC 宏；系数需与原理图一致 |
| UART / BLE / MQTT 发送温功率数据 | **BLE/MQTT 周期上报；UART 可选周期 + 协议查询增强** | 默认 `protocol_send_power/temperature(0|1)`；**`FR_ADC_PERIODIC_UART_REPORT=1`** 时周期走 `PROTOCOL_SRC_UART`；**`protocol_send_adc_raw`** 下发双路原始值 |

**总体判断**：模块**具备**完成“采集—换算—对外提供温功率量”的**功能闭环**；计量级精度仍建议产线标定 `ADC_VREF_MV` 与功率/NTC 宏。

---

## 2. 需求 1：模拟信号采集与数字 ADC 值

### 2.1 实现要点

- **硬件接口**：`fr_ADC_init` 将 **PD4** 复用为 `PORTD4_FUNC_ADC0`，**PD5** 复用为 `PORTD5_FUNC_ADC1`（见 `components/driver/include/driver_iomux.h`）。
- **配置**：`ADC_TRANS_SOURCE_PAD`、`ADC_REFERENCE_AVDD`、`pad_to_sample = 1`、`channels = 0x03`（双通道 **LOOP** 轮询）。
- **启动**：`adc_init` 后调用 `adc_enable(NULL, NULL, 0)`，与驱动示例一致，有利于转换可靠启动。
- **读数**：`fr_ADC_sample_dual()` 中 `adc_get_result(ADC_TRANS_SOURCE_PAD, 0x03, adc_buf)`，分别写入 `ESAirdata.AIRpowerADCvalue`、`ESAirdata.AIRntcADCvalue`。

### 2.2 技术可行性

- **可行**：双路前端分离，与“功率一路、NTC 一路”的硬件划分一致。
- **驱动层数据位宽**：`driver_adc.c` 中 `adc_reg_data_t.data` 为 **10 bit**；有效量化约 0～1023（具体以数据手册为准）。

### 2.3 性能与指标（采集侧）

- **分辨率**：约 10 bit；参考为 AVDD 时，LSB 对应电压约为 \(V_{\mathrm{ref}}/1024\) 量级（理论）。
- **采样率**：双通道 LOOP 时，驱动/头文件注释提示多通道路径相对单通道可能偏低；当前业务以 **`ADC_SAMPLE_PERIOD_MS`（10000 ms）** 定时触发读数为主，**远慢于 ADC 硬件极限**，对稳态功率与环境温度足够，对快速功率瞬变则偏粗。
- **通道对应**：代码约定 `adc_buf[0]`→功率、`adc_buf[1]`→NTC；若 PCB 与驱动映射不一致，需交换赋值（源码已注释说明）。

### 2.4 风险与建议

| 风险 | 影响 | 建议 |
|------|------|------|
| 通道顺序与 PCB 不一致 | 功率/温度数据源反接 | 联调时用已知电压或短路/开路确认后固定赋值顺序 |
| 参考电压实际值与宏 `ADC_VREF_MV` 偏差 | 比例误差 | 单点或多点用标准表校准 `ADC_VREF_MV` 或改用 `adc_get_ref_voltage` 参与换算 |
| NTC 前端非简单 10k 分压 | 温度公式失配 | 按原理图修改 `NTC_PULLUP_OHMS`、`NTC_BETA` |

---

## 3. 需求 2：ADC 值换算为 °C 与 W

### 3.1 功率（W）— `Get_Power_Value`

实现（`usercode/frADC.c`）要点：

- `adc_val` 钳位到 ≤1023。
- `voltage_mv = code * ADC_VREF_MV / ADC_CODE_FULL_SCALE`（**1023.0f**）。
- `current_a = (voltage_mv / ADC_POWER_VDIV_REF_MV) * ADC_POWER_I_COEFF`。
- `P = current_a * ADC_POWER_AC_V`（系数见 `frADC.h`）。

输入应为 **PD4/ADC0** 的 `AIRpowerADCvalue`。

### 3.2 温度（°C）— `Get_Temperature_Value`

- 若 **`adc_val < 1`**：返回 **`FR_ADC_TEMP_INVALID_C`（-999）**，避免除零。
- 否则：`ADC_CODE_FULL_SCALE`、`NTC_PULLUP_OHMS`、`NTC_BETA` 参与 β 模型换算。

输入应为 **PD5/ADC1** 的 `AIRntcADCvalue`（`app_task.c` 中已区分）。

### 3.3 “精确转换”评估

- **公式已实现**：是。
- **与硬件一致性**：满量程与 **10 bit** 已对齐；功率/温度系数以宏集中定义，便于标定。
- **数值稳定性**：极端低端 NTC 码值返回哨兵温度，**不应**直接当物理温度展示。

### 3.4 建议

- 产线标定 `ADC_VREF_MV`、`ADC_POWER_*`、`NTC_*`；可选 Flash/NVM 配置。
- 业务层对 **-999°C** 做过滤或保持上次有效值。

---

## 4. 需求 3：UART、蓝牙、MQTT 发送处理后的数据

### 4.1 数据处理后的数据源

`app_task.c` 中定时回调：`fr_ADC_send()` → `Get_Power_Value` / `Get_Temperature_Value` → 写入 `ESAirdata.AIRpowervalue`、`ESAirdata.temp_celsius`，再上报。

### 4.2 蓝牙（BLE）

- 条件：`gap_get_connect_num() > 0` 时调用 `protocol_send_power(0)`、`protocol_send_temperature(0)`。
- `protocol.c` 中 `source == PROTOCOL_SRC_BLE` 时经 `ESAIR_gatt_report_notify` 发出**二进制协议帧**（含 `CMD_GET_POWER` / `CMD_GET_TEMP` 及 4 字节 float 载荷）。

**结论**：BLE **具备**稳定上报能力，依赖链路连接与 GATT 通知使能（CCC 等由服务层约束，代码注释已提及）。

### 4.3 MQTT

- 条件：`R_atcommand.MLinitflag == ML307AMQTT_OK` 时 `mqtt_handler_send_power/temperature()` → 内部 `protocol_send_power(PROTOCOL_SRC_MQTT)` / `protocol_send_temperature(PROTOCOL_SRC_MQTT)` → `ML307A_MQTTPublish`。
- 未连接时有 `co_printf` 告警路径。

**结论**：MQTT **具备**在模组已连网且 MQTT OK 时的发布能力；稳定性受 4G 模组、Broker、Topic 配置及重连策略影响（工程内另有重连定时逻辑）。

### 4.4 UART

- **协议帧 UART**：`protocol_send_buffer` 在 `source == PROTOCOL_SRC_UART` 时 `uart_write(UART1, buffer, len)`。
- **周期上报（可选）**：编译宏 **`FR_ADC_PERIODIC_UART_REPORT`** 为 **1** 时，`sensor_read_timer_func` 在 BLE/MQTT 之外追加 `protocol_send_power/temperature(PROTOCOL_SRC_UART)`。默认 **0**。
- **原始 ADC 查询**：`CMD_GET_ADC_DATA` 处理中调用 **`protocol_send_adc_raw`**，载荷为 **4 字节小端**双路 `uint16` 原始值（**与旧版仅 CMD_RESPONSE 成功码不兼容，上位机需适配**）。
- **调试输出**：`co_printf` 不等同于协议帧。

**结论**：UART **已具备**协议级周期上报（可选开关）与双路原始值应答能力。

### 4.5 三种方式稳定性简评

| 通道 | 稳定性依赖 | 备注 |
|------|------------|------|
| BLE | 连接间隔、丢包、通知开关 | 已按连接数门控发送 |
| MQTT | 蜂窝链路、MQTT 会话、发布结果 | 发布失败有日志；可加强重试/队列 |
| UART | 线材、波特率、对端解析 | 可选周期上报时注意总线负载 |

---

## 5. 综合风险清单与优先级建议

1. **中**：功率/温度宏与真实前端未产线标定 → “W/°C”仍为工程估算直至校准。  
2. **中**：`CMD_GET_ADC_DATA` 载荷变更 → 老版上位机需升级。  
3. **低**：**`FR_ADC_TEMP_INVALID_C`** 若未在 UI 过滤，可能误显 **-999°C**。  
4. **低**：双通道 LOOP 采样率低于单通道时对 10 s 周期**几乎无影响**。

---

## 6. 总结

- **能力 1（双路采集）**：当前实现与 PD4 功率、PD5 NTC 的划分**一致**，技术**可行**。  
- **能力 2（换算）**：满量程与 10 bit **已对齐**；**精确**仍依赖标定与宏参数。  
- **能力 3（三种通信）**：**BLE 与 MQTT** 周期上报处理后的 float；**UART** 支持同帧发送，且可通过宏启用周期上报；**`protocol_send_adc_raw`** 提供双路原始 ADC。

---

## 7. 相关源码路径（核对用）

| 模块 | 路径 |
|------|------|
| 应用 ADC 封装 | `ESOACV2/ble_simple_peripheral/usercode/frADC.c`、`frADC.h` |
| 传感器任务与上报触发 | `ESOACV2/ble_simple_peripheral/usercode/app_task.c` |
| 协议组帧与 BLE/MQTT/UART 分发 | `ESOACV2/ble_simple_peripheral/usercode/protocol.c`、`protocol.h` |
| MQTT 侧封装 | `ESOACV2/ble_simple_peripheral/usercode/mqtt_handler.c` |
| SAR ADC 驱动 | `ESOACV2/ble_simple_peripheral/components/driver/driver_adc.c`、`include/driver_adc.h` |
| 全局空调/传感器数据 | `ESOACV2/ble_simple_peripheral/usercode/aircondata.h` |

---

*报告版本：与上述路径当前实现对照整理；后续代码变更时请同步修订本报告。*
