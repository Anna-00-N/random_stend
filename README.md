```markdown
random_stend.cpp
```
### Генератор случайных стендов
Ниже показано **подключение библиотек**, **объявление переменных** для TCP-сервера и переменных таймеров:
```c++
#include <Arduino.h>
#include <vector>
#include <ctime>
#include <cstdlib> // для rand и srand
#include <cmath>
#include <esp_system.h>
#include <WiFi.h> //для подключения к wi-fi
#include "SPIFFS.h" //файловая система esp32
#include <WiFiClient.h>
#include <WiFiServer.h>

using namespace std;

int process = 0; // Маркер запуска и остановки процесса

WiFiClient client; //Объявляем объект клиента для установки связи с сервером
const char *ssid = "ESP32"; //имя сервера
const char *password = "12345678"; //пароль сервера
IPAddress local_IP(192,168,4,22); //стат.данные для раздачи
IPAddress gateway(192,168,4,9);
IPAddress subnet(255,255,255,0);
WiFiServer server(8080); 



unsigned long tn1 = 0; // таймеры для обновления неподчиненных выходных значений
unsigned long tn2 = 0;
unsigned long tn3 = 0;
unsigned long tn4 = 0;
unsigned long tn5 = 0;
unsigned long tn6 = 0;

unsigned long tn1_lim = 0; // их ограничители
unsigned long tn2_lim = 0;
unsigned long tn3_lim = 0;
unsigned long tn4_lim = 0;
unsigned long tn5_lim = 1000;
unsigned long tn6_lim = 100;
double time1=0.0, time2=0.0, time3=0.0
```
Функция **initSPIFFS** нужна для инициализации файловой системы  ESP32 - после её объявления в мониторе порта могут выводиться русские буквы:
```c++
void initSPIFFS()  //инициализация файловой системы esp32

{

  if (!SPIFFS.begin(true)) //если не всё ок

  {

    Serial.println("An error has occurred while mounting SPIFFS"); //вывести сообщение об ошибке

    delay(10000); //подождать 10 с.

    ESP.restart(); //перезагрузить

  }

  Serial.println("SPIFFS mounted successfully"); //сообщение об успехе

}

```
