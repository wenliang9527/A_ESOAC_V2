#include "aircondata.h"
#include "co_printf.h"

Airparameters ESAirdata;
TCommDataPacket AIRresdata;
//判断现在的年份是不是闰年
int IS_temp_year(uint16_t year){
   if(year%4==0){
	    if(year%100!=0){
			    return 1;
			}else{
			    return 0;
			}
	 
	 }else{
	   if(year%400==0){
		    return 1;
		 }else{
		    return 0;
		 }
	 }
}

void Time_syn_Operation()
{
	ESAirdata.EStime.ES_Second++;
	if(ESAirdata.EStime.ES_Second>60)
		{
			ESAirdata.EStime.ES_Second=1;
			ESAirdata.EStime.ES_Minutes++;
		}
		if(ESAirdata.EStime.ES_Minutes>60)
		{
			ESAirdata.EStime.ES_Minutes=1;
			ESAirdata.EStime.ES_Hours++;
		}
		if(ESAirdata.EStime.ES_Hours>24)
			{
				ESAirdata.EStime.ES_Hours=1;
				ESAirdata.EStime.ES_Day++;
			}
			if(ESAirdata.EStime.ES_Day>7)
				{
					ESAirdata.EStime.ES_Day=1;
					ESAirdata.EStime.ES_Week++;
				}
			if(ESAirdata.EStime.ES_Day>30)
			{
				ESAirdata.EStime.ES_Day=1;
				ESAirdata.EStime.ES_Moon++;
			}
			if(ESAirdata.EStime.ES_Moon>12)
			{
				ESAirdata.EStime.ES_Moon=1;
				ESAirdata.EStime.ES_Second++;
				
			}
}
__attribute__((section("ram_code"))) void rtc_isr_ram(uint8_t rtc_idx)
{
    if(rtc_idx == RTC_A)
    {
       
    }
    if(rtc_idx == RTC_B)
    {
       Time_syn_Operation();
    }
}

void FR_rtcinit(void)
{
    co_printf("rtc demo\r\n");
    rtc_init();
    //rtc_alarm(RTC_A,1);
    rtc_alarm(RTC_B,1000);
}



void Timestebreak(TCommDataPacket buf1,Airparameters buf2)
{
	buf1.FrameBuf[0]=0x5a;
	buf1.FrameBuf[1]=0x7a;
	buf1.FrameBuf[2]=0x08;
	buf1.FrameBuf[3]=0x00;
	buf1.FrameBuf[4]=0x53;
	buf1.FrameBuf[5]=0x4a;
	
	buf1.FrameBuf[6]=buf2.EStime.ES_Year>>8;
	buf1.FrameBuf[7]=buf2.EStime.ES_Year;
	buf1.FrameBuf[8]=buf2.EStime.ES_Moon;
	buf1.FrameBuf[9]=buf2.EStime.ES_Day;
	buf1.FrameBuf[10]=buf2.EStime.ES_Hours;
	buf1.FrameBuf[11]=buf2.EStime.ES_Minutes;
	buf1.FrameBuf[12]=buf2.EStime.ES_Second;
	buf1.FrameBuf[13]=buf2.EStime.ES_Week;
	buf1.FrameBuf[14]=ESOACSum(buf1);
}












