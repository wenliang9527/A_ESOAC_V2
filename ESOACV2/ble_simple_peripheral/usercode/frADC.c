#include "frADC.h"
#include <string.h>
#include <math.h>

os_timer_t adc_timer;

/* 双通道 LOOP：buf[0]=ADC0(PD4 功率)，buf[1]=ADC1(PD5 NTC)。若实测与硬件对调，交换赋值即可。 */
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

/* Sample both ADC channels (power & NTC) */
void fr_ADC_sample_dual(void)
{
    uint16_t adc_buf[2];
    adc_get_result(ADC_TRANS_SOURCE_PAD, 0x03, adc_buf);
    ESAirdata.AIRpowerADCvalue = adc_buf[0];
    ESAirdata.AIRntcADCvalue = adc_buf[1];
}

void fr_adc_Thread(void *arg)
{
    (void)arg;
    fr_ADC_sample_dual();
}

void fr_ADC_star(void)
{
    fr_ADC_init();
    os_timer_init(&adc_timer, fr_adc_Thread, NULL);
    os_timer_start(&adc_timer, ADC_SAMPLE_PERIOD_MS, 1);
}

/* V2.2: fr_ADC_send - Sample ADC values (debug print removed, now in heartbeat) */
void fr_ADC_send(void)
{
    fr_ADC_sample_dual();
    /* ADC values are now reported via heartbeat protocol frame */
}

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
