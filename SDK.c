/*
 * SDK.c
 *
 *  Author: Artem Solonin
 *  Refrigerator controller
 */ 

//���������� �� ����� B
#define KEYPAD_PORT PORTB
#define KEYPAD_DDR DDRB
#define KEYPAD_PIN PINB

//������� ����� �� 16 ��� (����� ��� delay)
#define F_CPU 16000000UL

//����������� ������ ��� LCH HD44780
#define LCD_RS_1 PORTA | 0b00000001
#define LCD_RS_0 PORTA & 0b11111110
#define  LCD_RW_1 PORTA | 0b00000010
#define  LCD_RW_0 PORTA & 0b11111101
#define  LCD_E_1 PORTA | 0b00000100
#define  LCD_E_0 PORTA & 0b11111011

//PC0 �������� � ������������� �������� DS18B20
#define THERM_PORT PORTC
#define THERM_DDR DDRC
#define THERM_PIN PINC
#define THERM_DQ PC0


// ������� ��� "�������� ����" � ��������� ������ �����/������ 
#define THERM_INPUT_MODE() THERM_DDR&=~(1<<THERM_DQ)
#define THERM_OUTPUT_MODE() THERM_DDR|=(1<<THERM_DQ)
#define THERM_LOW() THERM_PORT&=~(1<<THERM_DQ)
#define THERM_HIGH() THERM_PORT|=(1<<THERM_DQ)


//���������� ����������� ����������
#include <math.h>
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>


int cur_temp=0;//������� �����������
int target_temp=25;//������� �����������, � ������� ��������� �������.
//target_temp ����� ���� ���������� � ������� ����������.
//���������� ��� ���������� �������
char cooling=0;
//���������� ��� ���������� ���������������� � ���������� �����������
char buzzer_signal=1;


void lcd_waiting()
{
	DDRD=0x00;
	PORTD=0xFF;
	PORTA=LCD_RS_0;
	PORTA=LCD_RW_1;
	PORTA=LCD_E_1;
	_delay_us(2);
	PORTA=LCD_E_0;
/*
	if ((PIND & (1<< 7)) == 0)
		lcd_waiting();
*/	
}

//������ ������� � HD44780
void lcd_write_command(char command)
{
	lcd_waiting();
	PORTA = LCD_RS_0;
	PORTA = LCD_RW_0;
	PORTA = LCD_E_1;
	DDRD=0xFF;
	PORTD=command;
	_delay_us(1000);//����, ���� LCD ������� � ���� �������
	PORTA=LCD_E_0;
	DDRD=0x00;
}

//������ ������ � HD44780
void lcd_write_data(char data)
{
	lcd_waiting();
	PORTA=LCD_RS_1;
	PORTA=LCD_RW_0;
	PORTA=LCD_E_1;
	DDRD=0xFF;
	PORTD=data;
	_delay_us(1000);//����, ���� LCD ������� � ���� ������
	PORTA=LCD_E_0;
	DDRD=0x00;
}

void lcd_init()
{
	
	lcd_write_command(0b00111000);//���� 8 ���, 2 ������
	lcd_write_command(0b00000001);//������� ������
	lcd_write_command(0b00000110);//��������� ������, ����� �� ��������
	lcd_write_command(0b00001100);//�������� �������
	lcd_write_command(0b00000001);//�������� �������, ��������� ����� �� DDRAM
	lcd_write_command(0b00010100);//�������� ������ S/C=0 ������ R/L=1
}

void lcd_clear()
{
	lcd_write_command(0b00000001);//������� ������
	lcd_write_command(0b10000000);//������������� �� DDRAM, ����� = 0
	lcd_write_command(0b00010100);//�������� ������ S/C=0 ������ R/L=1
	
}

//����� �������
void lcd_type_char(char symb)
{
	lcd_write_data(symb);
}

//����� ������
void lcd_type_str(char *str)
{
	int i=0;
	
	while(str[i] != 0x00)
		{
			lcd_type_char(str[i]);
			i++;
		}
}


//������ ������� �������
void lcd_set_pos(char pos)
{
	lcd_write_command(0b00000010); //����� ������ DDRAM
	for (char i=0; i<pos; i++)
		lcd_write_command(0b00010100);//����� �������
}

// ����� �������
uint8_t therm_reset(){
	uint8_t i;
	// �������� ���� ���� �� 480uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(480);             // �������� ������� �������� �� ����
	// �������� ����� �� 60uS
	THERM_INPUT_MODE();
	_delay_us(60);
	// �������� �������� �� ����� � ������ 480uS
	i=(THERM_PIN & (1<<THERM_DQ));
	_delay_us(420);
	// ���������� �������� (0=OK, 1=������ �� ������)
	return i;
}

// ������� �������� ����
void therm_write_bit(uint8_t bit){
	// �������� �� 1uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(1);
	// ���� ����� ��������� 1, ��������� ����� (���� ���, ��������� ��� ����)
	if(bit) THERM_INPUT_MODE();
	// ���� 60uS � ��������� �����
	_delay_us(60);
	THERM_INPUT_MODE();
}

// ������ ����
uint8_t therm_read_bit(void){
	uint8_t bit=0;
	// �������� �� 1uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(1);
	// ��������� �� 14uS
	THERM_INPUT_MODE();
	_delay_us(14);
	// ������ ���������
	if(THERM_PIN&(1<<THERM_DQ)) bit=1;
	// ���� 45 ��� � ���������� ��������
	_delay_us(45);
	return bit;
}

// ������ ����
uint8_t therm_read_byte(void){
	uint8_t i=8, n=0;
	while(i--){
		// �������� � ����� �� 1 � ��������� ��������� ��������
		n>>=1;
		n|=(therm_read_bit()<<7);
	}
	return n;
}

// ���������� ����
void therm_write_byte(uint8_t byte){
	uint8_t i=8;
	while(i--){
		// ���������� ��� � �������� ������ �� 1
		therm_write_bit(byte&1);
		byte>>=1;
	}
}


// ������� ���������� ��������
#define THERM_CMD_CONVERTTEMP 0x44
#define THERM_CMD_RSCRATCHPAD 0xbe
#define THERM_CMD_WSCRATCHPAD 0x4e
#define THERM_CMD_CPYSCRATCHPAD 0x48
#define THERM_CMD_RECEEPROM 0xb8
#define THERM_CMD_RPWRSUPPLY 0xb4
#define THERM_CMD_SEARCHROM 0xf0
#define THERM_CMD_READROM 0x33
#define THERM_CMD_MATCHROM 0x55
#define THERM_CMD_SKIPROM 0xcc
#define THERM_CMD_ALARMSEARCH 0xec

#define THERM_DECIMAL_STEPS_12BIT 625 //.0625

// ������ ����������� � �������
void therm_read_temperature(char *buffer){
	uint8_t temperature[2];
	int8_t digit;
	uint16_t decimal;
	
	therm_reset();
	therm_write_byte(THERM_CMD_SKIPROM);
	therm_write_byte(THERM_CMD_CONVERTTEMP);
	
	while(!therm_read_bit());
	
	therm_reset();
	therm_write_byte(THERM_CMD_SKIPROM);
	therm_write_byte(THERM_CMD_RSCRATCHPAD);
	
	temperature[0]=therm_read_byte();
	temperature[1]=therm_read_byte();
	therm_reset();
	
	digit=temperature[0]>>4;
	digit|=(temperature[1]&0x7)<<4;
	
	decimal=temperature[0]&0xf;
	decimal*=THERM_DECIMAL_STEPS_12BIT;
	
	// � ������ ������ �� ����
	//PORTA = digit;
	//PORTB = decimal >> 8;
	cur_temp=digit;
	sprintf(buffer, "%+d.%02uC", cur_temp, decimal/100);
}

//������������� ������ ��� ������ � �����������
void key_init(){
	DDRB = 0x0F;
	PORTB = 0xF0;
}

//������� � �������� ~5000 ��
void buzzer()
{
	PORTC|=0b00000010;
	DDRC|=0b00000010;
	for(int i=0; i<5000;i++)
	{
		PORTC|=0b00000010;
		_delay_us(100);
		PORTC|=0b00000000;
		_delay_us(100);
	}
	DDRC&=0b11111101;
	PORTC&=0b11111101;
}

//������ ��� ������ ����������� �������� PINB
//���������� ��� ������� ������ � �����������
void show_PINB()
{
	char str[15];
	sprintf(str, "%d", PINB);
	lcd_clear();
	lcd_type_str(str);
	_delay_ms(2000);	
}


//������� ��� ������ ������� ������
//���� �� ���� ������ �� ������ - ��������� 99
int get_key()
{	
	int but = 99;
	//������ ���
	PORTB = 0b11111110;
	if(PINB!=PORTB)
	{
		switch(PINB)
		{
			case 238:
				but=1;
			break;
			case 222:
				but=2;
			break;
			case 190:
				but=3;
			break;
			case 126:
				but=10;
			break;
		}
	}
	
	//������ ���
	PORTB = 0b11111101;
	if(PINB!=PORTB)
	{
		switch(PINB)
		{
			case 237:
			but=4;
			break;
			case 221:
			but=5;
			break;
			case 189:
			but=6;
			break;
			case 125:
			but=11;
			break;
		}
	}
	
	//������ ���
	PORTB = 0b11111011;
	if(PINB!=PORTB)
	{
		switch(PINB)
		{
			case 235:
			but=7;
			break;
			case 219:
			but=8;
			break;
			case 187:
			but=9;
			break;
			case 123:
			but=12;
			break;
		}
	}
	
	//��������� ���
	PORTB = 0b11110111;
	if(PINB!=PORTB)
	{
		switch(PINB)
		{
			case 231:
			but=20;
			break;
			case 215:
			but=0;
			break;
			case 183:
			but=21;
			break;
			case 119:
			but=13;
			break;
		}
	}
	return but;	
}

//���������� �������
void manage_temp()
{
	
	//� ������ ���� ���� ������� ������� ������������� ����������
	//��� ���������� ��� ����, ����� ��� ���������� ����������� �����������
	//������ ������ ����������� ��� �� 1 ������ �� ���������
	//��������� ������� ���������/���������� ��� ���������� target temperature
	if (cur_temp>=(target_temp+1))
	{
		cooling=1;
		if(buzzer_signal)
		buzzer();
		buzzer_signal=0;
		
			DDRC|=0b11000000;
			PORTC|=0b11000000;
	}
	
	if (cur_temp<(target_temp-1))
	{
		cooling=0;
		buzzer_signal=1;
		PORTC&=0b00111111;
		DDRC&=0b00111111;
	}
}

int main(void)
{
DDRD=0x00;
DDRA=0b00000111;

//DDRC=0x00;
//PORTC=0x00;

key_init();
char curr_temp[15];//������ ������� ����������� ��� ������ �� �����
char str[15];//�� ������������ ���������� ��� �������
lcd_init();//������������� �������

int target_temp_t=-50;//���������� �������� �������� ����������� �� ����� �����
char str_target_temp[15];//������ USER �����������
int input=0;//������� �������
int operation=0;//A-D ��������� ��������. �������� ������ A - ������ �����������

while(1)
	{
		asm("NOP");
		therm_read_temperature(curr_temp);//��������� �����������
		lcd_set_pos(0);//������ ������ �� ������� �������
		lcd_clear();//������� �������
		lcd_type_str(curr_temp);//������� ������� �����������
		input=get_key();//�������� ������� �������
		//���� ������� ������� - ���������, �� �������� ��� � ���������� operation
		if (input>9 && input<19)
		{
			operation = input;
		}
		
		//���� ��������������� ������� ������
		manage_temp();
		
		//�������, ����� ������ �������
		switch (operation)
		{
			//���� ������� A, �� ������ TARGET �����������
			case 10:
			//������� TARGET USER ����������� �� �����
			sprintf(str_target_temp, "%d", target_temp_t);
			
			//������ ������� ������� �� ������ ������
			lcd_set_pos(40);
			//������� USER ��������
			lcd_type_str("Target_t = ");
			lcd_type_str(str_target_temp);
			
			
				input=get_key();
				//��������� �������� ��������
				if (input>0 && input<10)
					{
						target_temp_t+=input;						
					}
				//"*" - �������� ����
				if (input==20)
				{
					target_temp_t=-50;
					operation=0;
					lcd_set_pos(40);
					lcd_type_str("Cancelling...");
					buzzer();
					
				}
				//"#" - ��������� ��������� ��������
				if (input==21)
				{
					target_temp=target_temp_t;
					target_temp_t=-50;
					operation=0;
					lcd_set_pos(40);
					lcd_type_str("Saving...");
					buzzer();		
				}
			break;
		}
		
		_delay_ms(10);
	}
}