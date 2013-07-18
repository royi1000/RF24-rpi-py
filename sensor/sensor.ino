#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <SPI.h>
#include <Wire.h> //BH1750 IIC Mode  Light Sensor
#include <math.h> 
#include <EEPROM.h>
#include <Time.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "DHT.h"
#include "rf_io.h"

/*********   PINS     ******
*****   light sensor   *****
SCL-SCL(analog pin 5)
SDA-SDA(analog pin 4)
*******    RF24    *********
CE-9
CSN-10
MOSI-11
MISO-12
SCK-13
***************************/


#define DHT22_PIN 2
#define PAYLOAD_SIZE 20
#define SENSOR_RH_TEMP 0x50
#define SENSOR_LIGHT 0x51
#define DEVICE_TYPE 0x20
#define MAX_STR_LEN (12*6)
#define UPDATE_INTERVAL 5000
#define WD_TIMEOUT_INTERVAL (1 * 60 * 1000)
const uint16_t MAGIC_CODE = 0xDE12;
// Set up nRF24L01 radio on SPI bus plus pins 9 & 10
int BH1750address = 0x23; //setting i2c address light sensor
RF24 radio(9,10);
DHT dht;


// Radio pipe addresses for the 2 nodes to communicate.
uint64_t pipes[2] = {
  0xF0F0F0F0D2LL,  0xF0F0F0F0E1LL};

typedef enum{ command_init=0xF0, command_init_response=0xF1, sensor_data=0x30}  command_type_e;
typedef enum{ date=0x1, string=0x2, bitmap=0x3, color_string=0x4, sound=0x5, remove_id=0x10, end_tx=0x20}  data_type_e;

uint8_t radio_buf[32];
unsigned long last_update_time = 0;
unsigned int data_len = 0;
uint8_t data[100];
bool inited = false;
byte buff[2];

typedef struct store {
  uint16_t magic_code;
  uint64_t rx_addr;
  uint64_t tx_addr;
} store_t;

typedef struct cmd_message {
  uint8_t  command;
  uint8_t dev_type;
  uint64_t addr;
} cmd_message_t;

typedef struct temp_rh_sensor {
  uint8_t    command;
  uint8_t   sensor_type;
  uint64_t addr;
  float       rh;
  float       temp;
} temp_rh_sensor_message_t;

typedef struct light_sensor_sensor {
  uint8_t   command;
  uint8_t   sensor_type;
  uint64_t addr;
  uint16_t lux;
} light_sensor_message_t;

store_t config_settings;

bool is_timeouted(unsigned long last, unsigned long timeout_interval /*millis*/)
{
  if (((millis() - last) < timeout_interval))
    return false;
  return true;
}

/*** light sensor ****/
int BH1750_Read(int address) //
{
  int i=0;
  Wire.beginTransmission(address);
  Wire.requestFrom(address, 2);
  while(Wire.available()) //
  {
    buff[i] = Wire.read();  // receive one byte
    i++;
  }
  Wire.endTransmission();  
  return i;
}
 
void BH1750_Init(int address) 
{
  Wire.beginTransmission(address);
  Wire.write(0x10);//1lx reolution 120ms
  Wire.endTransmission();
}
/***   light sensor    ***/



bool handle_message() {
  bool ret = false;
  uint8_t cmd = data[0];
  if(cmd==command_init_response) {
    cmd_message_t* msg = (cmd_message_t*)data;
    if(config_settings.magic_code != MAGIC_CODE || msg->addr != config_settings.rx_addr) {
      config_settings.rx_addr = msg->addr;
      config_settings.magic_code = MAGIC_CODE;
      eeprom_write_block((const void*)&config_settings, (void*)0, sizeof(config_settings));
    }
	ret = true;
  }
  data_len = 0;
  return ret;
}

void send_init_packet() {
  cmd_message_t message;
  message.command = command_init;
  message.dev_type = DEVICE_TYPE;
  message.addr = config_settings.rx_addr;
  radio.stopListening();
  Serial.println("sending init packet");
  handle_write(&radio, (void *)&message, sizeof(message));
  radio.startListening(); 
  unsigned long start_wait_time = millis();
  bool timeout = false;
  bool end_transmission = false;
  while(!timeout && !end_transmission) {
    if ((millis() - start_wait_time > 1000)) {
        Serial.println("recive timeout");
      timeout = true;
    }
    else if(radio.available()){
		Serial.println("recive timeout");
		uint8_t payload_size = radio.getPayloadSize();
		radio.read(radio_buf, payload_size);
		if(handle_read(&radio, radio_buf,  payload_size, data, &data_len)){
			end_transmission = handle_message();
		}
        start_wait_time = millis();
    }
  }
  if(end_transmission){
        Serial.println("recived register");
		inited = true;
  }
}

void temp_rh_sensor()
{
  float humidity = dht.getHumidity();
  float temperature = dht.getTemperature();
  Serial.println(dht.getStatusString());
  Serial.print("RH: ");
  Serial.print(humidity);
  Serial.print(" Temp: ");
  Serial.println(temperature);
  temp_rh_sensor_message_t sensor_message;
  sensor_message.command = sensor_data;
  sensor_message.sensor_type = SENSOR_RH_TEMP;
  sensor_message.addr = config_settings.rx_addr;
  sensor_message.rh = humidity;
  sensor_message.temp = temperature;
  handle_write(&radio, (void *)&sensor_message, sizeof(sensor_message));
}

void light_sensor()
{
  uint16_t val=0;
  BH1750_Init(BH1750address);
  delay(200);
 
  Serial.print("Light: ");
  if(2==BH1750_Read(BH1750address))
  {
		val=((buff[0]<<8)|buff[1])/1.2;
		Serial.print(val,DEC);     
		Serial.println("[lx]"); 
		light_sensor_message_t sensor_message;
		sensor_message.command = sensor_data;
		sensor_message.sensor_type = SENSOR_LIGHT;
		sensor_message.addr = config_settings.rx_addr;
		sensor_message.lux = val;
		handle_write(&radio, (void *)&sensor_message, sizeof(sensor_message));
  }
}

void send_sensors_messages()
{
    radio.stopListening();
	temp_rh_sensor();
	delay(300);
	light_sensor();
	radio.startListening(); 
}


void setup()
{
  Serial.begin(9600);
  Wire.begin();
  dht.setup(DHT22_PIN  ); // data pin 2
  Serial.print("\n\rSensor begin ...\n\r");
  randomSeed(analogRead(0));
  eeprom_read_block((void*) &config_settings, (void*) 0, sizeof(config_settings));
  // Setup and configure rf radio
  if (0xDE12 != config_settings.magic_code) {
    config_settings.tx_addr = 0xF0F0F0F0E1LL;//0xF0F0F00000LL | random(0xFFFFLL);
    config_settings.rx_addr = 0xF0F0000000LL | random(0xFFFFFFLL);
  }
  radio.begin();


  // optionally, increase the delay between retries & # of retries
  radio.setRetries(15,15);

  //        radio.enableDynamicPayloads();
  radio.setPayloadSize(PAYLOAD_SIZE);

  radio.openWritingPipe(config_settings.tx_addr);
  radio.openReadingPipe(1, config_settings.rx_addr);

  radio.startListening();
  radio.printDetails();
  Serial.println("init end");
}

void send_data_to_master()
{
	send_sensors_messages();
}

void loop()
{
    if ((millis() - last_update_time > UPDATE_INTERVAL)) {
		if(!inited) {
			send_init_packet();
		}
		if(inited)
		{
			send_data_to_master();
		}
        last_update_time = millis();
	}
}

