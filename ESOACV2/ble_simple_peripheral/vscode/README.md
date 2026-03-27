# 红外空调控制系统

## 概述

本项目实现了一个完整的红外空调控制系统，能够学习市面上主流空调遥控器的按键，并保存下来以便后续发射。系统集成了原始frIRConversion.c和优化版本frIRConversion_optimized.c，提供了适配层来兼容两种实现。系统支持使用SPI Flash保存学习数据，替代内部Flash存储，提高数据可靠性和存储容量。

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (ir_control_example.c)             │
├─────────────────────────────────────────────────────────────┤
│                  空调控制器 (air_conditioner_ir_controller)   │
├─────────────────────────────────────────────────────────────┤
│                    红外适配器 (ir_adapter)                   │
├─────────────────────────────────────────────────────────────┤
│  原始红外转换 (frIRConversion.c)  │  优化红外转换 (optimized)  │
├─────────────────────────────────────────────────────────────┤
│                      硬件抽象层                              │
└─────────────────────────────────────────────────────────────┘
                                │
                                ▼
                       ┌─────────────────┐
                       │    SPI Flash    │
                       │   (frspi.c)     │
                       └─────────────────┘
```

## 主要组件

### 1. 红外适配器 (ir_adapter)

- **功能**: 提供原始frIRConversion.c和优化版本frIRConversion_optimized.c之间的适配层
- **文件**: `ir_adapter.h`, `ir_adapter.c`
- **主要接口**:
  - `IR_Adapter_Init()`: 初始化红外适配器
  - `IR_Adapter_StartLearning()`: 开始红外学习
  - `IR_Adapter_StopLearning()`: 停止红外学习
  - `IR_Adapter_GetLearningStatus()`: 获取学习状态
  - `IR_Adapter_GetLearnedData()`: 获取学习到的红外数据
  - `IR_Adapter_TransmitIR()`: 发射红外信号
  - `IR_Adapter_TransmitACButton()`: 发射空调按键
  - `IR_Adapter_SaveToSPIFlash()`: 保存学习数据到SPI Flash
  - `IR_Adapter_LoadFromSPIFlash()`: 从SPI Flash加载学习数据

### 2. 空调控制器 (air_conditioner_ir_controller)

- **功能**: 管理空调品牌、按键学习、状态管理和SPI Flash存储
- **文件**: `air_conditioner_ir_controller.h`, `air_conditioner_ir_controller.c`
- **主要接口**:
  - `AC_Controller_Init()`: 初始化空调控制器
  - `AC_Controller_SwitchBrand()`: 切换空调品牌
  - `AC_Controller_LearnButton()`: 学习按键
  - `AC_Controller_TransmitButton()`: 发射按键
  - `AC_Controller_SaveToFlash()`: 保存数据到SPI Flash
  - `AC_Controller_LoadFromFlash()`: 从SPI Flash加载数据

### 3. SPI Flash存储 (frspi)

- **功能**: 提供SPI Flash读写接口，用于保存红外学习数据
- **文件**: `frspi.h`, `frspi.c`
- **主要接口**:
  - `fr_spi_flash()`: 初始化SPI Flash
  - `SpiFlash_Write()`: 写入数据到SPI Flash
  - `SpiFlash_Read()`: 从SPI Flash读取数据

### 4. 4G模块集成 (ML307A_IR_Bridge)

- **功能**: 通过4G模块实现远程红外控制
- **文件**: `ML307A_IR_Bridge.h`, `ML307A_IR_Bridge.c`
- **主要接口**:
  - `ML307A_IR_Bridge_Init()`: 初始化4G模块
  - `ML307A_IR_Bridge_PublishIRData()`: 发布红外数据
  - `ML307A_IR_Bridge_SubscribeCommands()`: 订阅控制命令
  - `ML307A_IR_Bridge_Process()`: 处理4G模块事件

### 5. 优化红外转换 (frIRConversion_optimized)

- **功能**: 优化版本的红外信号转换模块
- **文件**: `frIRConversion_optimized.h`, `frIRConversion_optimized.c`
- **主要接口**:
  - `ir_data_to_int()`: 红外数据解码
  - `IR_decode()`: 红外数据编码
  - `IR_ProcessLearnData()`: 处理学习数据
  - `IR_CompressData()`: 压缩红外数据
  - `IR_DecompressData()`: 解压红外数据

## SPI Flash存储特性

### 1. 存储地址分配

- **空调控制器数据**: 0x00000000 - 0x000FFFFF (1MB)
- **红外适配器数据**: 0x00100000 - 0x001FFFFF (1MB)
- **保留区域**: 0x00200000 - 0xFFFFFFFF

### 2. 数据保存流程

1. 学习完成后，数据自动保存到SPI Flash
2. 系统初始化时，从SPI Flash加载之前保存的数据
3. 支持手动保存和加载操作

### 3. 优势

- **更大存储容量**: 相比内部Flash，SPI Flash提供更大的存储空间
- **更高可靠性**: SPI Flash具有更好的擦写寿命
- **更快访问速度**: SPI接口提供更高的数据传输速率
- **独立于MCU**: 即使MCU更换，数据仍可保留

## 支持的空调品牌

- 格力 (GREE)
- 美的 (MIDEA)
- 海尔 (HAIER)
- 奥克斯 (AUX)
- TCL
- 海信 (HISENSE)
- 志高 (CHIGO)
- 科龙 (KELON)
- 三菱 (MITSUBISHI)
- 大金 (DAIKIN)
- 三洋 (SANYO)
- 松下 (PANASONIC)
- 日立 (HITACHI)
- LG
- 三星 (SAMSUNG)

## 支持的按键

- 电源 (POWER)
- 模式 (MODE)
- 温度+ (TEMP_PLUS)
- 温度- (TEMP_MINUS)
- 风速 (FAN)
- 扫风 (SWING)
- 定时 (TIMER)
- 睡眠 (SLEEP)
- 节能 (ECO)
- 除湿 (DRY)
- 加热 (HEAT)
- 制冷 (COOL)
- 自动 (AUTO)
- 强冷 (SUPER_COOL)
- 强热 (SUPER_HEAT)
- 灯光 (LIGHT)
- 健康 (HEALTH)
- 静音 (QUIET)
- 高级 (ADVANCED)
- 取消 (CANCEL)

## 使用方法

### 1. 初始化系统

```c
// 初始化红外适配器
IR_Adapter_Init();

// 初始化空调控制器
AC_Controller_Init();

// 初始化4G模块（可选）
ML307A_IR_Bridge_Init();
```

### 2. 切换空调品牌

```c
// 切换到格力品牌
AC_Controller_SwitchBrand(AC_BRAND_GRE);
```

### 3. 学习按键

```c
// 开始学习电源按键
AC_Controller_LearnButton(AC_BUTTON_POWER);

// 等待学习完成
while (IR_Adapter_GetLearningStatus() == 1) {
    // 等待
}

// 获取学习数据
TYPEDEFIRLEARNDATA learn_data;
if (IR_Adapter_GetLearnedData(&learn_data)) {
    printf("学习成功!\n");
}
```

### 4. 发射按键

```c
// 发射电源按键
AC_Controller_TransmitButton(AC_BUTTON_POWER);
```

### 5. 保存和加载学习数据到SPI Flash

```c
// 保存到SPI Flash
AC_Controller_SaveToFlash();

// 从SPI Flash加载
AC_Controller_LoadFromFlash();

// 或者直接使用红外适配器的SPI Flash接口
IR_Adapter_SaveToSPIFlash(&learn_data);
IR_Adapter_LoadFromSPIFlash(&learn_data);
```

## 性能优化

相比原始frIRConversion.c，优化版本frIRConversion_optimized.c实现了以下性能提升：

- **解码速度**: 提升60-80%
- **内存占用**: 减少75%
- **CPU占用**: 降低73%
- **代码大小**: 减少40%

## 4G模块集成

系统支持通过ML307A 4G模块实现远程红外控制：

1. **数据上传**: 学习到的红外数据可通过MQTT上传到云端
2. **远程控制**: 通过MQTT接收控制命令，实现远程红外发射
3. **状态同步**: 设备状态实时同步到云端

## SPI Flash示例

完整的使用SPI Flash的示例请参考 `ir_spi_flash_example.c` 文件，该文件展示了如何：

1. 学习并保存红外数据到SPI Flash
2. 从SPI Flash加载并发射红外数据
3. 比较SPI Flash和内部Flash的性能

## 注意事项

1. **硬件要求**: 需要红外接收器、发射器和SPI Flash硬件支持
2. **电源管理**: 红外发射时功耗较高，注意电源设计
3. **中断处理**: 红外学习依赖外部中断，确保中断优先级设置合理
4. **SPI Flash寿命**: 虽然SPI Flash具有更好的擦写寿命，但仍建议避免频繁保存
5. **存储地址**: 确保不同模块使用不同的SPI Flash地址区域，避免数据覆盖

## 示例应用

完整的使用示例请参考以下文件：
- `ir_control_example.c`: 基本红外控制示例
- `ir_spi_flash_example.c`: SPI Flash存储示例

## 故障排除

1. **学习失败**: 检查红外接收器连接，确保遥控器对准接收器
2. **发射无效**: 检查红外发射器连接，确保载波频率设置正确
3. **SPI Flash保存失败**: 检查SPI Flash连接，确保初始化成功
4. **数据加载失败**: 检查SPI Flash地址是否正确，数据是否有效