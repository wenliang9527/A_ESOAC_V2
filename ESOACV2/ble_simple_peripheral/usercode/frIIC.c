#include "frIIC.h"

void iic_master_init(void)
{
    system_set_port_pull(GPIO_PD6, true);
    system_set_port_pull(GPIO_PD7, true);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_6, PORTD6_FUNC_I2C1_CLK);
    system_set_port_mux(GPIO_PORT_D, GPIO_BIT_7, PORTD7_FUNC_I2C1_DAT);
    iic_init(IIC_CHANNEL_1,100,0);
}

/**
 * @brief IIC动态主控读取多个字节数据
 * 
 * 该函数用于通过指定的IIC通道，向从设备发送地址并读取其返回的数据。
 * 数据接收使用结构体struct iic_recv_data进行管理，支持动态长度读取。
 * 
 * @param channel IIC通道选择，可选IIC_CHANNEL_0或IIC_CHANNEL_1
 * @param slave_addr 从设备地址（7位地址，不含读写位）
 * @param recv_data 指向接收数据结构体的指针，包含接收缓冲区和状态标志
 * 
 * @return uint8_t 返回操作结果，true表示成功，false表示失败（如无应答）
 */
uint8_t iic_dynamic_master_read_bytes(enum iic_channel_t channel, uint8_t slave_addr,struct iic_recv_data *recv_data)
{
    volatile struct iic_reg_t *iic_reg;
	uint8_t tmp;

    // 根据通道选择对应的寄存器基地址
    if(channel == IIC_CHANNEL_0)
    {
        iic_reg = IIC0_REG_BASE;
    }
    else
    {
        iic_reg = IIC1_REG_BASE;
    }

    // 发送从机地址 + 写标志 + 启动信号
    iic_reg->data = slave_addr | IIC_TRAN_START;
    while(iic_reg->status.trans_emp != 1); // 等待传输完成
    co_delay_10us(10); // 延时等待响应
    if(iic_reg->status.no_ack == 1) // 检查是否收到应答
    {
        return false;
    }

    // 发送从机地址 + 读标志 + 启动信号
    iic_reg->data = slave_addr | 0x01 | IIC_TRAN_START;
		
    // 循环接收数据直到接收完成标志被置位
    while(!recv_data->finish_flag)
    {
        // 发送空数据以产生时钟，准备接收数据
        iic_reg->data = 0x00;
        while(iic_reg->status.trans_emp != 1); // 等待发送完成
		while(iic_reg->status.rec_emp != 1) // 等待接收到数据
        {
			tmp = iic_reg->data;	
		
            // 如果尚未开始接收有效数据，则先接收长度字段		
			if(!recv_data->start_flag)
			{			
				recv_data->recv_length_buff[recv_data->recv_cnt++] = tmp;
				// 接收完两个字节后解析数据长度
				if(recv_data->recv_cnt == 2)
				{
					recv_data->length = ((uint16_t)recv_data->recv_length_buff[0] & 0xff) |
										(((uint16_t)recv_data->recv_length_buff[1] << 8)& 0xff00);
					recv_data->start_flag = 1; // 设置开始接收标志
					recv_data->recv_cnt = 0; // 重置计数器
				}
			}
			else
			{
                // 正常接收数据内容
				recv_data->buff[recv_data->recv_cnt++] = tmp;
				// 当接收数据接近尾声时设置完成标志
				if(recv_data->recv_cnt >= (recv_data->length-1))
				{
					recv_data->finish_flag = 1;
				}
			}
			
        }
    }

    // 发送停止信号
    iic_reg->data = IIC_TRAN_STOP;
    while(iic_reg->status.bus_atv == 1); // 等待总线空闲
	while(iic_reg->status.rec_emp != 1) // 等待最后可能的数据接收完成
	{
		if(recv_data->recv_cnt < recv_data->length)
		{
			recv_data->buff[recv_data->recv_cnt++] = iic_reg->data;
		}
		else
		{
			tmp = iic_reg->data; // 丢弃多余数据
		}
	}
    return true;
}