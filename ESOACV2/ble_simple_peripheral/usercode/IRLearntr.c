// 红外学习与红外数据发送流程

#include "frIRConversion.h"

void ESOAAIR_Savekey(void)
{
    SpiFlash_Write((void *)&ESairkey, W25QADDR2, sizeof(ESairkey));
}

void ESOAAIR_readkey(void)
{
    uint8_t i;
    SpiFlash_Read((void *)&ESairkey, W25QADDR2, sizeof(ESairkey));
    for (i = 0; i < AIRkeyNumber; i++)
    {
        if (ESairkey.keyExistence[i] == 0)
        {
            co_printf("Error: AIRkeyNumber (%d) Missing\r\n", i);
        }
    }
}
