
#include "EmonLib.h"         
#include <avr/eeprom.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Ethernet.h>


//Configurações da eeprom para armazernar a ultima leitura
#define eeprom_read_to(dst_p, eeprom_field, dst_size) eeprom_read_block(dst_p, (void *)offsetof(__eeprom_data, eeprom_field), MIN(dst_size, sizeof((__eeprom_data*)0)->eeprom_field))
#define eeprom_read(dst, eeprom_field) eeprom_read_to(&dst, eeprom_field, sizeof(dst))
#define eeprom_write_from(src_p, eeprom_field, src_size) eeprom_write_block(src_p, (void *)offsetof(__eeprom_data, eeprom_field), MIN(src_size, sizeof((__eeprom_data*)0)->eeprom_field))
#define eeprom_write(src, eeprom_field) { typeof(src) x = src; eeprom_write_from(&x, eeprom_field, sizeof(x)); }
#define MIN(x,y) ( x > y ? y : x )

struct __eeprom_data {
  double flash_kwhtotal;
  double flash_kwhultimo;
  float flash_tensao;
};

EnergyMonitor emon1;      // Cria instancia da biblioteca emonlib

//Configura Display
LiquidCrystal display(9, 8, 7, 6, 5, 4);


//Configura Ethernet
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

//IP ou endereco servidor
char server[] = "192.168.1.13";   

//ip local
IPAddress ip(192,168,1,108);
IPAddress gateway(192,168,1,254);     //Define o gateway
IPAddress subnet(255, 255, 255, 0); //Define a máscara de rede   
IPAddress myDns(8,8,8,8);

// Initialize the Ethernet client library
// with the IP address and port of the server 
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client; 


//Cria variaveis globais
float tensao;
String leitura_serial;
String id_equipamento;
double kwhTotal, kwh_ultimo;

double minCorrente;
unsigned long ltmillis, tmillis, timems, previousMillis;

unsigned long tempo_monit = 1000; //tempo a cada gravaçao em milisegundos
char charBuf[30];


void setup()
{  
  Serial.begin(9600);
  display.begin(16, 2);
  Ethernet.begin(mac, ip, myDns, gateway, subnet);
   
  tensao = 127.0;
  id_equipamento = "00";
  
  emon1.current(0, 66.606); // Calibrado conform a montagem do circuito com resistores de 33 ohms (100 / 0.05)/ 33
  
  
  eeprom_read(kwhTotal, flash_kwhtotal);
  eeprom_read(kwh_ultimo, flash_kwhultimo);
  eeprom_read(tensao, flash_tensao);
  
  leitura_serial = "";
  
  previousMillis = millis();

  //kwhTotal = 0;
  //kwh_ultimo = 0;

  delay(1000);
  
}

void loop()
{
  minCorrente = 0.05;
    
  //Tempo que o circuito esta ligado.
  ltmillis = tmillis;
  tmillis = millis();
  timems = tmillis - ltmillis;
  double Irms = emon1.calcIrms(1480);  // Calculate Irms only
  
  if (Irms < minCorrente) {
      Irms = 0;
  }
  
  double pot = Irms*tensao;
  
  //Calculate todays number of kwh consumed.
  kwhTotal = kwhTotal + (((Irms*tensao)/1000.0) * 1.0/3600.0 * (timems/1000.0));
  kwh_ultimo = kwh_ultimo + (((Irms*tensao)/1000.0) * 1.0/3600.0 * (timems/1000.0));
  
  //converte double em string
  dtostrf(kwhTotal, 8, 7, charBuf); 
  
  display.setCursor(0,0);
  display.print("Kw/h: ");
  display.print(charBuf);
  display.print(" ");
  
  
   //converte double em string
  dtostrf(Irms, 5, 3, charBuf); 
 
  display.setCursor(0,1);
  display.print("Id: ");
  display.print(id_equipamento);
  display.print(" I: ");
  display.print(charBuf);
  display.print(" ");
  
  if (client.available()) {
    char c = client.read();
  }
  
  if (!client.connected()) {
    client.stop();
  }
  
  
  //grava na memoria a conform o tempo de monitoramento segundos e envia pro banco de dados
  if (((millis() - previousMillis)>tempo_monit) && (Irms > 0) && (id_equipamento != "00"))
  {
    eeprom_write(kwhTotal, flash_kwhtotal);
    eeprom_write(kwh_ultimo, flash_kwhultimo);
    eeprom_write(tensao, flash_tensao);
  
    dtostrf(kwh_ultimo, 8, 7, charBuf); 
    if (client.connect(server, 8080)) {
      grava_bd(charBuf, String((millis() - previousMillis), DEC), Irms, id_equipamento);
      previousMillis=millis();
      kwh_ultimo =0;
      Serial.println("Enviado Servidor");
    } else {
      Serial.println("Erro na conexão com servidor");
    }
  }
  
  
  if (Serial.available() > 0) {
    // le valor pela serial
    char inChar = Serial.read();
    // Type the next ASCII value from what you received:
    leitura_serial += String(inChar);
    
    if(String(inChar) == ";"){    
      le_serial(leitura_serial);
    }
    if(leitura_serial.length() > 25){
      leitura_serial = "";
    }
    
  }
  
  delay(800);
}

void le_serial(String leitura){
  
  if (leitura.indexOf("id=") >= 0) {
    id_equipamento = leitura.substring(leitura.indexOf("id=")+3, leitura.length()-1);
  }
  if (leitura.indexOf("tensao=") >= 0) {
    String tensao_str = leitura.substring(leitura.indexOf("tensao=")+7, leitura.length()-1);
    tensao = StrToFloat(tensao_str);
  }
  
  if (leitura.indexOf("reset") >= 0) {
     tensao = 127.0;
     id_equipamento = "00";
     kwhTotal = 0;
     kwh_ultimo = 0;
     Serial.println("-- Reset --");
  }
  
  leitura_serial = "";
  Serial.print("Id : ");
  Serial.println(id_equipamento);
  Serial.print("Tensao: ");
  Serial.println(tensao);
}

float StrToFloat(String str){
  char carray[str.length() + 1]; //determina o tamanho do array
  str.toCharArray(carray, sizeof(carray)); 
  return atof(carray);
}

void grava_bd(String leitura, String tempo, double corrente, String id){
  
  client.print("GET //ConsumoEnergia/publico/leituraArduino.jsf?leitura=");
  client.print(leitura);
  client.print("&tempo=");
  client.print(tempo);
  client.print("&corrente=");
  client.print(corrente);
  client.print("&equipamento=");
  client.print(id);
  client.println(" HTTP/1.1");
  client.println("Host: www.google.com");
  client.println("Connection: close");
  client.println();
  client.stop();
  
  //emon1.serialprint();
  Serial.print("Leitura: ");
  Serial.print(leitura);
  Serial.print(" Tempo: ");
  Serial.print(tempo);
  Serial.print(" Corrente: ");
  Serial.print(corrente);
  Serial.print(" Id_equip: ");
  Serial.println(id);
}
