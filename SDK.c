/*
 * SDK.c
 *
 *  Author: Artem Solonin
 *  Refrigerator controller
 */ 

//Клавиатура на ПОРТУ B
#define KEYPAD_PORT PORTB
#define KEYPAD_DDR DDRB
#define KEYPAD_PIN PINB

//Внешний кварц на 16 МГц (нужно для delay)
#define F_CPU 16000000UL

//Управляющие выводы для LCH HD44780
#define LCD_RS_1 PORTA | 0b00000001
#define LCD_RS_0 PORTA & 0b11111110
#define  LCD_RW_1 PORTA | 0b00000010
#define  LCD_RW_0 PORTA & 0b11111101
#define  LCD_E_1 PORTA | 0b00000100
#define  LCD_E_0 PORTA & 0b11111011

//PC0 работает с температурным датчиком DS18B20
#define THERM_PORT PORTC
#define THERM_DDR DDRC
#define THERM_PIN PINC
#define THERM_DQ PC0


// Макросы для "дергания ноги" и изменения режима ввода/вывода 
#define THERM_INPUT_MODE() THERM_DDR&=~(1<<THERM_DQ)
#define THERM_OUTPUT_MODE() THERM_DDR|=(1<<THERM_DQ)
#define THERM_LOW() THERM_PORT&=~(1<<THERM_DQ)
#define THERM_HIGH() THERM_PORT|=(1<<THERM_DQ)


//Подключаем необходимые библиотеки
#include <math.h>
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>


int cur_temp=0;//текущая температура
int target_temp=25;//целевая температура, к которой стремится система.
//target_temp может быть переписана с помощью клавиатуры.
//Переменная для управления кулером
char cooling=0;
//Переменная для единичного сигнализирования о превышении температуры
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

//Запись команды в HD44780
void lcd_write_command(char command)
{
	lcd_waiting();
	PORTA = LCD_RS_0;
	PORTA = LCD_RW_0;
	PORTA = LCD_E_1;
	DDRD=0xFF;
	PORTD=command;
	_delay_us(1000);//Ждем, пока LCD запишет к себе команду
	PORTA=LCD_E_0;
	DDRD=0x00;
}

//Запись данных в HD44780
void lcd_write_data(char data)
{
	lcd_waiting();
	PORTA=LCD_RS_1;
	PORTA=LCD_RW_0;
	PORTA=LCD_E_1;
	DDRD=0xFF;
	PORTD=data;
	_delay_us(1000);//Ждем, пока LCD запишет к себе данные
	PORTA=LCD_E_0;
	DDRD=0x00;
}

void lcd_init()
{
	
	lcd_write_command(0b00111000);//ШИНА 8 БИТ, 2 СТРОКИ
	lcd_write_command(0b00000001);//ОЧИСТКА ЭКРАНА
	lcd_write_command(0b00000110);//ИНКРЕМЕНТ АДРЕСА, ЭКРАН НЕ ДВИЖЕТСЯ
	lcd_write_command(0b00001100);//ВКЛЮЧИЛИ ДИСПЛЕЙ
	lcd_write_command(0b00000001);//ОЧИСТИЛИ ДИСПЛЕЙ, УКАЗАТЕЛЬ ВСТАЛ НА DDRAM
	lcd_write_command(0b00010100);//СДВИНУЛИ КУРСОР S/C=0 ВПРАВО R/L=1
}

void lcd_clear()
{
	lcd_write_command(0b00000001);//ОЧИСТКА ЭКРАНА
	lcd_write_command(0b10000000);//переключаемся НА DDRAM, АДРЕС = 0
	lcd_write_command(0b00010100);//СДВИНУЛИ КУРСОР S/C=0 ВПРАВО R/L=1
	
}

//Вывод символа
void lcd_type_char(char symb)
{
	lcd_write_data(symb);
}

//Вывод строки
void lcd_type_str(char *str)
{
	int i=0;
	
	while(str[i] != 0x00)
		{
			lcd_type_char(str[i]);
			i++;
		}
}


//Задаем позицию курсора
void lcd_set_pos(char pos)
{
	lcd_write_command(0b00000010); //сброс адреса DDRAM
	for (char i=0; i<pos; i++)
		lcd_write_command(0b00010100);//сдвиг курсора
}

// сброс датчика
uint8_t therm_reset(){
	uint8_t i;
	// опускаем ногу вниз на 480uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(480);             // замените функцию задержки на свою
	// подымаем линию на 60uS
	THERM_INPUT_MODE();
	_delay_us(60);
	// получаем значение на линии в период 480uS
	i=(THERM_PIN & (1<<THERM_DQ));
	_delay_us(420);
	// возвращаем значение (0=OK, 1=датчик не найден)
	return i;
}

// функция отправки бита
void therm_write_bit(uint8_t bit){
	// опускаем на 1uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(1);
	// если хотим отправить 1, поднимаем линию (если нет, оставляем как есть)
	if(bit) THERM_INPUT_MODE();
	// ждем 60uS и поднимаем линию
	_delay_us(60);
	THERM_INPUT_MODE();
}

// чтение бита
uint8_t therm_read_bit(void){
	uint8_t bit=0;
	// опускаем на 1uS
	THERM_LOW();
	THERM_OUTPUT_MODE();
	_delay_us(1);
	// поднимаем на 14uS
	THERM_INPUT_MODE();
	_delay_us(14);
	// читаем состояние
	if(THERM_PIN&(1<<THERM_DQ)) bit=1;
	// ждем 45 мкс и возвращаем значение
	_delay_us(45);
	return bit;
}

// читаем байт
uint8_t therm_read_byte(void){
	uint8_t i=8, n=0;
	while(i--){
		// сдвигаем в право на 1 и сохраняем следующее значение
		n>>=1;
		n|=(therm_read_bit()<<7);
	}
	return n;
}

// отправляем байт
void therm_write_byte(uint8_t byte){
	uint8_t i=8;
	while(i--){
		// отправляем бит и сдвигаем вправо на 1
		therm_write_bit(byte&1);
		byte>>=1;
	}
}


// команды управления датчиком
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

// читаем температуру с датчика
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
	
	// я вывожу данные на порт
	//PORTA = digit;
	//PORTB = decimal >> 8;
	cur_temp=digit;
	sprintf(buffer, "%+d.%02uC", cur_temp, decimal/100);
}

//Инициализация портов для работы с клавиатурой
void key_init(){
	DDRB = 0x0F;
	PORTB = 0xF0;
}

//Пищалка с частотой ~5000 Гц
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

//Фунция для вывода содержимого регистра PINB
//Необходима для отладки работы с клавиатурой
void show_PINB()
{
	char str[15];
	sprintf(str, "%d", PINB);
	lcd_clear();
	lcd_type_str(str);
	_delay_ms(2000);	
}


//Функция для вывода нажатой кнопки
//Если ни одна кнопка не нажата - выводится 99
int get_key()
{	
	int but = 99;
	//Первый ряд
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
	
	//Второй ряд
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
	
	//Третий ряд
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
	
	//Четвертый ряд
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

//Управление кулером
void manage_temp()
{
	
	//С пмощью этих двух условий создаем искусственный гистерезис
	//Это необходимо для того, чтобы при достижении необходимой температуры
	//Куллер снизил температуру еще на 1 градус во избежание
	//чрезмерно частого включения/выключения при достижении target temperature
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
char curr_temp[15];//храним текущую температуру для вывода на экран
char str[15];//не обязательная переменная для отладки
lcd_init();//инициализация дисплея

int target_temp_t=-50;//переменаая целевого значения температуры на время ввода
char str_target_temp[15];//храним USER температуру
int input=0;//нажатая клавиша
int operation=0;//A-D выбранная операция. Доступна только A - задать температуру

while(1)
	{
		asm("NOP");
		therm_read_temperature(curr_temp);//считываем температуру
		lcd_set_pos(0);//задаем курсор на нулевую позицию
		lcd_clear();//очищаем дисплей
		lcd_type_str(curr_temp);//выводим текущую температуру
		input=get_key();//получаем нажатую клавишу
		//если нажатая клавиша - командная, то отмечаем это в переменной operation
		if (input>9 && input<19)
		{
			operation = input;
		}
		
		//Даем соответствующую команду кулеру
		manage_temp();
		
		//Смотрим, какая пришла команда
		switch (operation)
		{
			//Если команда A, то задаем TARGET температуру
			case 10:
			//Выводим TARGET USER температуру на экран
			sprintf(str_target_temp, "%d", target_temp_t);
			
			//Задаем позицию курсора на вторую строку
			lcd_set_pos(40);
			//Выводим USER значение
			lcd_type_str("Target_t = ");
			lcd_type_str(str_target_temp);
			
			
				input=get_key();
				//Считываем цифровые значения
				if (input>0 && input<10)
					{
						target_temp_t+=input;						
					}
				//"*" - отменить ввод
				if (input==20)
				{
					target_temp_t=-50;
					operation=0;
					lcd_set_pos(40);
					lcd_type_str("Cancelling...");
					buzzer();
					
				}
				//"#" - сохранить введенное значение
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