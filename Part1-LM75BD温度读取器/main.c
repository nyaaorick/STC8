#include <STC8H.H>
#include <intrins.h>
#include <stdio.h>

#define FOSC  11059200UL 							//频率-11.0592MHz



// 时钟11.0592MHz
// 串口1-模式1  P4.3-RXD  P4.4-TXD
// 定时器T2波特率发生器 115200
// IIC主机模式P1.4  P1.5
// 定时器T3定时中断1s --> 读取温度数据发送到串口

unsigned char temp_reg[2];				//温度寄存器读取
xdata unsigned short temp_value;				//十六位存储温度
xdata double temperature_c;				//温度计算结果
xdata char uart_send_buf[20];			//串口输出字符
bit timer1s_flag = 0;			//定时器1s标志位
xdata char uart_buf_wptr; 				//写指针
bit uart_busy_flag;		//串口忙碌位
bit iic_busy_flag;			//IIC忙碌位

//GPIO引脚初始化
void GPIO_Init()
{
	//设置准双向口
	//串口引脚P4.3  P4.4
	P4M0 = 0x00;
	P4M1 = 0x00;
	//IIC引脚P1.4  P1.5
	P1M0 = 0x00;
	P1M1 = 0x00;
}

#define BRT   (65536 - (FOSC / 115200 + 2) / 4) 	//波特率-115200
//串口1初始化
void UartInit()
{
	 SCON = 0x50;
		T2L = 0xe8;                                 //65536-11059200/115200/4=0FFE8H
		T2H = 0xff;
		AUXR = 0x15;                                //启动定时器
		ES = 1;                                     //使能串口中断
		EA = 1;
		//SBUF = 0x5a;                                //发送测试数据
	
	uart_busy_flag = 0;
	uart_buf_wptr = 0x00;
}
//IIC初始化 SCL:P1.5, SDA:P1.4
void IIC_Init()
{
	P_SW2 |= 0x80;		//使能访问XFR
	I2CCFG = 0xe0;		//使能I2C主机模式
	I2CMSST = 0x00;		//状态寄存器清零
	iic_busy_flag = 0;
}
//Timer3初始化---1s
void Timer3_Init(void)
{
	P_SW2 |= 0x80;		//使能访问XFR
	TM3PS = 0x0F;		//预分频
	T4T3M &= 0xFD;		//Timer3 --- 12T模式
	T3L = 0x00;			//设置定时初始值
	T3H = 0x1F;			//设置定时初始值
	IE2 |= 0x20;		//使能定时器3中断
	T4T3M |= 0x08;		//Timer3 run enable
}

//串口1 中断服务函数
void UartIsr() interrupt 4
{
	if(TI)			//发
	{
		TI = 0;
		uart_busy_flag = 0;
	}
}
//删减代码冗余部分
//IIC中断
void I2C_Isr() interrupt 24 
{
    if (I2CMSST & 0x40)
    {
        I2CMSST &= ~0x40;		//清中断标志
        iic_busy_flag = 0;
    }
}
//Timer3中断---1s
void Timer3_Isr(void) interrupt 19
{
	timer1s_flag = 1;
}

//串口
void UartSend(char dat);		//发送单个字符
void UartSendStr(char *p);		//发送字符串
//IIC处理
void Start(void);
void SendData(char dat);
void RecvACK(void);
char RecvData(void);
void SendACK(void);
void SendNAK(void);
void Stop();
//延时
void delay_ms(unsigned char ms);

void main()
{
	GPIO_Init();		//引脚
	UartInit();			//串口1
	IIC_Init();			//IIC
	Timer3_Init();		//Timer3
	
	UartSendStr("Hello\r\n"); // 增加发送HELLO
	
	EA = 1;				//总中断
	while(1)
	{
		if(timer1s_flag)
		{
			Start();				//开始
			SendData(0x90);			//写命令
			RecvACK();
			SendData(0x00);			//发送存储地址
			RecvACK();
			Start();				//开始
			SendData(0x91);			//读命令
			RecvACK();
			
			temp_reg[0] = RecvData();	//高八位
			SendACK();				//继续接收数据
			temp_reg[1] = RecvData();	//低三位
			SendNAK();				//停止接收数据
			Stop();					//停止
			
			temp_value = (unsigned short)temp_reg[0] << 8 | temp_reg[1];
			temp_value = temp_value >> 5;			//转为有效值11位的温度值
			if(temp_value & 0x0400)
			{
				temperature_c = -(~(temp_value & 0x03FF) + 1) * 0.125;
			}
			else
			{
				temperature_c = temp_value * 0.125;
			}
			sprintf(uart_send_buf, "temp: %.3f C\r\n", temperature_c);
			UartSendStr(uart_send_buf);
			
			timer1s_flag = 0;
		}
		delay_ms(10);
	}
}

//发送单个字符
void UartSend(char dat)
{
	while(uart_busy_flag);	//忙碌则等待
	uart_busy_flag = 1;
	SBUF = dat;
}
//发送字符串
void UartSendStr(char *p)
{
	while(*p)
		UartSend(*p++);
}


void Start()
{
    iic_busy_flag = 1;
    I2CMSCR = 0x81;			//发送START命令
    while (iic_busy_flag);
}

void SendData(char dat)
{
    I2CTXD= dat;			//写数据到数据缓冲区
    iic_busy_flag = 1;
    I2CMSCR= 0x82;			//发送SEND命令
    while (iic_busy_flag);
}

void RecvACK()
{
    iic_busy_flag = 1;
    I2CMSCR= 0x83;			//发送读ACK命令
    while (iic_busy_flag);
}

char RecvData()
{
    iic_busy_flag = 1;
    I2CMSCR= 0x84;			//发送RECV命令
    while (iic_busy_flag);
    return I2CRXD;
}

void SendACK()
{
    I2CMSST= 0x00;			//设置ACK信号
    iic_busy_flag = 1;
    I2CMSCR= 0x85;			//发送ACK命令
    while (iic_busy_flag);
}

void SendNAK()
{
    I2CMSST= 0x01;			//设置NAK信号
    iic_busy_flag = 1;
    I2CMSCR= 0x85;			//发送ACK命令
    while (iic_busy_flag);
}

void Stop()
{
    iic_busy_flag = 1;
    I2CMSCR = 0x86;			//发送STOP命令
    while (iic_busy_flag);
}


void delay_ms(unsigned char ms)
{
	unsigned int i;
	do{
		i = FOSC / 10000;
		while(--i);
	}while(--ms);
}
