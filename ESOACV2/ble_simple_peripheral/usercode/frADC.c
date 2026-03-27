#include "frADC.h"
#include <string.h>
#include <math.h>

os_timer_t adc_timer;

void fr_ADC_init(void)
{
    struct adc_cfg_t cfg;
    memset((void *)&cfg, 0, sizeof(cfg));

    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_4, PORTD4_FUNC_ADC0);

    cfg.src = ADC_TRANS_SOURCE_PAD;
    cfg.ref_sel = ADC_REFERENCE_AVDD;
    cfg.channels = 0x01;
    cfg.route.pad_to_sample = 1;
    cfg.clk_sel = ADC_SAMPLE_CLK_24M_DIV13;
    cfg.clk_div = 0x3f;

    adc_init(&cfg);
}

void fr_adc_Thread(void *arg)
{
    uint16_t result;
    adc_get_result(ADC_TRANS_SOURCE_PAD, 0x01, &result);
    ESAirdata.AIRpowerADCvalue = result;
}

void fr_ADC_star(void)
{
    fr_ADC_init();
    os_timer_init(&adc_timer, fr_adc_Thread, NULL);
    os_timer_start(&adc_timer, 10000, 1);
}

void fr_ADC_stop(void)
{
    os_timer_stop(&adc_timer);
}

void fr_ADC_send(void)
{
    uint16_t result;
    adc_get_result(ADC_TRANS_SOURCE_PAD, 0x01, &result);
    ESAirdata.AIRpowerADCvalue = result;
    co_printf("READ ADCdata: 0x%2X%2X\r\n", result, result >> 8);
    co_printf("AIRpowervalue: %d\r\n", ESAirdata.AIRpowervalue);
}

float Get_Temperature_Value(uint16_t adc_val)
{
    float resistance = (4095.0 / adc_val - 1) * 10000.0;
    float temp = 1.0 / (1.0 / 298.15 + (1.0 / 3950.0) * log(resistance / 10000.0));
    return temp - 273.15;
}

float Get_Power_Value(uint16_t adc_val)
{
    float voltage_mv = (adc_val * ADC_VREF_MV) / 4095.0;
    float current_a = (voltage_mv / 3300.0) * 20.0;
    return current_a * 220.0;
}
