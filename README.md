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
Структура **SystemParams** и класс **SystemController** нужны для генерации **случайного сигнала**, сигнал формируется не в виде шума, а **в виде отрезков формирования переходных процессов** со случайным установочным значением и параметра ПИ-регулятор, для добавления плавности используется экспоненциальное сглаживание:
```c++
struct SystemParams {
  float gain;
  float feedback;
  float pi_Kp;
  float pi_Ki;
};

class SystemController {
public:
  // Конструктор
  SystemController(float max) { MAX_OUTPUT = max; }

  // Инициализация
  void begin() {
    randomSeed(analogRead(0));
    // Инициализация параметров
    currentParams = generateRandomParams();
    targetParams = currentParams;
    smoothedParams = currentParams;

    // Инициализация буфера
    for (int i = 0; i < FILTER_SIZE; i++) {
      outputBuffer[i] = 0;
    }
    currentTime = 0.0;
    segmentTimeRemaining = 0.0;
  }

  // Обновление — вызывается каждые 100 мс
  float update() {
      // Генерация длины сегмента
      if (segmentTimeRemaining <= 0.0) {
        float segmentDuration = random(10, 70) / 10.0; // 1-7 сек
        segmentTimeRemaining = segmentDuration;
        targetParams = generateRandomParams();
      }

      // Плавное изменение параметров
      smoothedParams.gain = (1 - 0.02) * smoothedParams.gain + 0.02 * targetParams.gain;
      smoothedParams.feedback = (1 - 0.02) * smoothedParams.feedback + 0.02 * targetParams.feedback;
      smoothedParams.pi_Kp = (1 - 0.02) * smoothedParams.pi_Kp + 0.02 * targetParams.pi_Kp;
      smoothedParams.pi_Ki = (1 - 0.02) * smoothedParams.pi_Ki + 0.02 * targetParams.pi_Ki;
      float setpoint = 50.0;

      // Регулятор
      float controlSignal = piController(setpoint, lastMeasurement, integral, smoothedParams, 0.1);
      controlSignal = constrain(controlSignal, -MAX_OUTPUT, MAX_OUTPUT);

      // Модель системы
      float measurement = systemModel(controlSignal, smoothedParams.feedback, smoothedParams.gain);

      // Плавный переход
      float output = (1 - 0.1) * lastOutput + 0.1 * measurement;

      // Ограничение
      if (output < 0.0) output = 0.0;

      // Фильтрация
      outputBuffer[bufferIndex] = output;
      bufferIndex = (bufferIndex + 1) % FILTER_SIZE;
      float filteredOutput = 0.0;
      for (int i = 0; i < FILTER_SIZE; i++) {
        filteredOutput += outputBuffer[i];
      }
      filteredOutput /= FILTER_SIZE;

      // Ограничение финального выхода
      float outputLimited = constrain(filteredOutput, 0, MAX_OUTPUT);

      // Обновление состояния
      lastOutput = output;
      lastMeasurement = measurement;
      currentTime += 0.1; // 100 мс
      segmentTimeRemaining -= 0.1;
      // Вывод
      return outputLimited;
  }

    float get_time(){
        return currentTime;
    }

private:
  // Константы
  static const unsigned long intervalMs = 100;
  static const int FILTER_SIZE = 10;
  //static constexpr float MAX_OUTPUT = 100.0;
  float MAX_OUTPUT = 100.0;

  // Переменные
  float currentTime;
  float segmentTimeRemaining;

  // Параметры
  SystemParams currentParams, targetParams, smoothedParams;
  float integral = 0.0;
  float lastMeasurement = 0.0;
  float lastOutput = 0.0;
  float outputBuffer[FILTER_SIZE];
  int bufferIndex = 0;

  // Методы
  SystemParams generateRandomParams() {
    SystemParams p;
    p.gain = random(1, 50) / 10.0;
    p.feedback = random(1, 50) / 10.0;
    p.pi_Kp = random(1, 50) / 10.0;
    p.pi_Ki = random(1, 50) / 10.0;
    return p;
  }

  float systemModel(float input, float feedback, float gain) {
    return gain * input + feedback;
  }

  float piController(float setpoint, float measurement, float &integral, const SystemParams &params, float dt) {
    float error = setpoint - measurement;
    integral += error * dt;
    return params.pi_Kp * error + params.pi_Ki * integral;
  }
};

// Создаём глобальный объект
vector<SystemController> systemControllers;
```
