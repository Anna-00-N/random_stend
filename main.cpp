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

int process = -1; // Маркер запуска и остановки процесса

WiFiClient client; //Объявляем объект клиента для установки связи с сервером
bool client_active = false; // флаг, указывающий, активен ли клиент
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
unsigned long tn5_lim = 500;
unsigned long tn6_lim = 100;
double time1=0.0, time2=0.0, time3=0.0;

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
    randomSeed(esp_random());
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


// Замещение пробелов на '_'
String replaceSpaces(String input) {
  for (int i = 0; i < input.length(); i++) {
    if (input.charAt(i) == ' ') {
      input.setCharAt(i, '_');
    }
  }
  return input;
}

// Вторая функция: применяет первую ко всем строкам в векторе
vector<String> replaceSpacesVector(vector<String> input) {
    for (int i = 0; i < input.size(); i++) {
        input[i] = replaceSpaces(input[i]);
    }
    return input;
}
//------------------------------
// Классы

// 0. Checkbox (CB)
class CB {
public:
    String name;
    vector<String> options;

    CB() {}
    CB(const String& n, const vector<String>& opts) : name(replaceSpaces(n)), options(replaceSpacesVector(opts)) {}
};

// 1. List
class List {
public:
    String name;
    vector<String> options;

    List() {}
    List(const String& name0, const vector<String>& options0)
        : name(replaceSpaces(name0)), options(replaceSpacesVector(options0)) {}
};

// 2. Polosa
class Polosa {
public:
    String name;
    double min_val;
    double max_val;
    double step;

    Polosa() {}
    Polosa(const String& name0, double min0, double max0, double step0)
        : name(replaceSpaces(name0)), min_val(min0), max_val(max0), step(step0) {}
};

// 3. OutString
class OutString {
public:
    String label;
    vector<String> vars;

    OutString() {}
    OutString(const String& label0, const vector<String>& vars0) : label(replaceSpaces(label0)), vars(replaceSpacesVector(vars0)) {}
};

// 4. OutPolosa
class OutPolosa {
public:
    String name;
    double min_val;
    double max_val;
    double step;

    OutPolosa() {}
    OutPolosa(const String& name0, double min0, double max0, double step0)
        : name(replaceSpaces(name0)), min_val(min0), max_val(max0), step(step0) {}
};

// 5. Graph
class Graph {
public:
    String name;

    Graph() {}
    Graph(const String& name0)
        : name(replaceSpaces(name0)) {}
};

// Глобальные контейнеры
vector<CB> cbOptions;
vector<List> lists;
vector<Polosa> polosas;
vector<OutString> outStrings;
vector<OutPolosa> outPolosas;
vector<Graph> graphs;

// Функция инициализации данных
void init_all() {
    // Инициализация генератора случайных чисел
    // 0. CB - добавляем все названия
    cbOptions.emplace_back("Светодиоды", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Вентилятор", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Обогреватель", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Звуковой сигнал", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Автоматический режим", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Режим энергосбережения", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Влагомер", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Датчик движения", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Камера", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Bluetooth", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Авторегулировка", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Ночной режим", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Зарядка", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Защита от замыканий", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Датчик температуры", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Избежание перезагрузки", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Инвертор", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Оптический датчик", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Защита воздушных потоков", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Контроль доступа", vector<String>{"Разрешен", "Запрещен"});
    cbOptions.emplace_back("Функция уведомлений", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Автоблокировка", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Режим резервного питания", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Настройка времени", vector<String>{"Установлено", "Не установлено"});
    cbOptions.emplace_back("Режим безопасности", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Виртуальный режим", vector<String>{"Активен", "Не активен"});
    cbOptions.emplace_back("Обратная связь", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Функция отложенного запуска", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Режим обратного отсчета", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Параметр низкого режима", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Параметр высоких температур", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Системная проверка", vector<String>{"Активна", "Не активна"});
    cbOptions.emplace_back("Параметр режима резервирования", vector<String>{"Активен", "Не активен"});
    cbOptions.emplace_back("Система пожарозащиты", vector<String>{"Активна", "Не активна"});
    cbOptions.emplace_back("Датчик задымлённости", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Режим таймера", vector<String>{"Активен", "Деактивен"});
    cbOptions.emplace_back("Режим кондиционирования", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Автоматическая настройка", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Режим турбо", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Защита от перегрева", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Режим паролирования", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Функция автоматического сброса", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Режим настройки по времени", vector<String>{"Активен", "Не активен"});
    cbOptions.emplace_back("Режим автоматического запуска", vector<String>{"Вкл", "Выкл"});
    cbOptions.emplace_back("Обратная связь по сети", vector<String>{"Доступна", "Недоступна"});
    cbOptions.emplace_back("Режим уменьшенного потребления", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Индикатор заряда", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Режим совместимости", vector<String>{"Включен", "Выключен"});
    cbOptions.emplace_back("Функция автоматической дифференциации", vector<String>{"Включена", "Выключена"});
    cbOptions.emplace_back("Режим интеллектуального управления", vector<String>{"Включен", "Выключен"});

    

    // 1. Списки
    lists.emplace_back("Тип устройства", vector<String>{
        "Датчик", "Актуатор", "Контроллер", "Камера", "Процессор", "Дисплей", "Блок питания", 
        "Модуль связи", "Мотор", "Регенератор", "Терморегулятор", "Сенсор", "Реле", 
        "Преобразователь", "Модуль хранения", "Модуль питания", "Блок управления", "Кнопка", 
        "Датчик давления", "Батарея"});
    lists.emplace_back("Режим отображения", vector<String>{
        "Темный", "Светлый", "Авто", "Инверсия", "Режим чтения", "Режим кино", "Режим энергосбережения",
        "Фото", "День", "Ночь", "Дисплей на улице", "Дисплей внутри", "Режим обучения", 
        "Режим ожидания", "Тест", "Обновление", "Режим показа", "Режим редактирования", "Режим демонстрации"});
    lists.emplace_back("Канал передачи данных", vector<String>{
        "Канал 1", "Канал 2", "Канал 3", "Канал 4", "Канал 5"});
    lists.emplace_back("Тип сообщения", vector<String>{
        "Текст", "Аудио", "Видео", "Команда", "Уведомление", "Лог", "Ошибка"});
    lists.emplace_back("Тип пользователя", vector<String>{
        "Гость", "Пользователь", "Администратор", "Модератор", "Разработчик"});
    lists.emplace_back("Тип уведомлений", vector<String>{
        "Всплывающее окно", "Звук", "Вибрация", "E-mail", "СМС", "push-уведомления"});
    lists.emplace_back("Режим обработки данных", vector<String>{
        "Реальный", "Тест", "Обучение", "Демонстрация", "Имитированный"});
    lists.emplace_back("Тип сенсора", vector<String>{
        "Оптический лазерный", "Индуктивный", "Магниторезистивный", "Ультразвуковой", "Тепловой"
    });
    lists.emplace_back("Тип двигателя", vector<String>{
        "Электрический", "Дизельный", "Гибридный", "Гидравлический", "Пневматический"
    });
    lists.emplace_back("Режим отображения", vector<String>{
        "Темный", "Светлый", "Авто", "Режим уроков", "Экспертный"
    });
    lists.emplace_back("Тип системы связи", vector<String>{
        "Wi-Fi", "LTE", "Bluetooth", "Радиосвязь", "Ethernet"
    });
    lists.emplace_back("Тип управления", vector<String>{
        "Покадровое", "Голосовое", "Жестовое", "Автоматическое", "Ручное"
    });
    lists.emplace_back("Тип датчика урожая", vector<String>{
        "Оптический", "Ультразвуковой", "Гравитационный", "Магнитный", "Инфракрасный"
    });
    lists.emplace_back("Режим мониторинга", vector<String>{
        "Реальный", "Тестовый", "Обучающий", "Демонстрационный", "Исторический"
    });
    lists.emplace_back("Тип панели управления", vector<String>{
        "Сенсорная", "Комбинированная", "Кнопочная", "Механическая", "Виртуальная"
    });
    lists.emplace_back("Тип системы охлаждения", vector<String>{
        "Воздушное", "Жидкостное", "Газовое", "Гибридное", "Компрессионное"
    });
    lists.emplace_back("Тип системы питания", vector<String>{
        "Основное", "Резервное", "Солнечное", "Автоматическое", "Портативное"
    });
    lists.emplace_back("Режим обработки данных", vector<String>{
        "Автоматический", "Ручной", "По расписанию", "Реальный", "Обучающий"
    });
    lists.emplace_back("Тип системы навигации", vector<String>{
        "GPS", "ГЛОНАСС", "Гибридная", "Инфракрасная", "Лидар"
    });
    lists.emplace_back("Тип системы фильтрации", vector<String>{
        "Механическая", "Электростатическая", "Магнитная", "Пневматическая", "Вакуумная"
    });
    lists.emplace_back("Режим калибровки", vector<String>{
        "Автоматическая", "Ручная", "Плановая", "На основе подсказок", "Реактивная"
    });
    lists.emplace_back("Тип системы звукового оповещения", vector<String>{
        "Зуммер", "Громкоговоритель", "Вибрация", "Интерком", "Мигалка"
    });
    lists.emplace_back("Тип системы отображения", vector<String>{
        "Проектор", "Дисплей", "Графическая панель", "Реальный экран", "Дисплей AR"
    });
    lists.emplace_back("Режим работы двигателя", vector<String>{
        "Полевой", "Обслуживание", "Ремонт", "Тест", "Подзарядка"
    });
    lists.emplace_back("Тип системы безопасности", vector<String>{
        "Шифрование", "Аутентификация", "Брандмауэр", "Резервные копии", "Обнаружение вторжений"
    });
    lists.emplace_back("Режим загрузки обновлений", vector<String>{
        "Автоматический", "Ручной", "Запланированный", "Обучающий", "Экстренный"
    });
    lists.emplace_back("Тип системы мониторинга", vector<String>{
        "Онлайн", "Оффлайн", "Интерактивный", "Автоматический", "Ручной"
    });
    lists.emplace_back("Тип системы коммуникации", vector<String>{
        "Мобильная", "Кабельная", "Беспроводная", "Спутниковая", "Интерфейс IoT"
    });
    lists.emplace_back("Режим управления скоростью", vector<String>{
        "Автоматический", "Ручной", "Гибкий", "Экстренный", "Ограниченный"
    });
    lists.emplace_back("Тип системы очистки", vector<String>{
        "Механическая", "Пневматическая", "Магнитная", "Газовая", "Электростатическая"
    });
    lists.emplace_back("Режим защиты", vector<String>{
        "Автоматическая", "Ручная", "Профилактическая", "Обучающая", "Антивирусная"
    });
    lists.emplace_back("Тип системы логирования", vector<String>{
        "Локальный", "Облачный", "Гибридный", "Реальный", "Исторический"
    });
    lists.emplace_back("Режим сброса настроек", vector<String>{
        "Автоматический", "Ручной", "По расписанию", "По событию", "Обратной связи"
    });
    lists.emplace_back("Тип системы адаптации", vector<String>{
        "Искусственный интеллект", "Машинное обучение", "Генетические алгоритмы", "Реинфорсмент", "Байесовские сети"
    });
    lists.emplace_back("Тип системы безопасности данных", vector<String>{
        "Шифрование AES", "Сертификаты SSL", "Двухфакторная аутентификация", "Бэкапы", "Ограничение доступа"
    });
    lists.emplace_back("Режим диагностики", vector<String>{
        "Автоматическая", "Ручная", "Глубокая", "Поверхностная", "Реальная"
    });
    lists.emplace_back("Тип системы подачи топлива", vector<String>{
        "Автоматическая", "Ручная", "Премиум", "Экономичная", "Регенерация"
    });
    lists.emplace_back("Режим работы в условиях плохой погоды", vector<String>{
        "Погодостойкий", "Защищенный", "Автоматический", "Ручной", "Режим ожидания"
    });
    lists.emplace_back("Тип системы связи с оператором", vector<String>{
        "Видеосвязь", "Аудиосвязь", "Чат", "Обратный звонок", "Сообщения"
    });
    lists.emplace_back("Режим тестового запуска", vector<String>{
        "Реальный", "Имитационный", "Обучающий", "Минимальный", "Полнонедельный"
    });
    lists.emplace_back("Тип системы программного обеспечения", vector<String>{
        "Встроенное", "Облачное", "Локальное", "Модульное", "Обновляемое"
    });
    lists.emplace_back("Режим загрузки программ", vector<String>{
        "Автоматическая", "Ручная", "По расписанию"
    });
    lists.emplace_back("Тип системы охлаждения двигателя", vector<String>{
        "Воздушное", "Жидкостное", "Газовое"
    });
    lists.emplace_back("Режим тестирования систем", vector<String>{
        "Полный цикл", "Минимальный", "Диагностический", "Параллельный"
    });
    lists.emplace_back("Тип системы питания", vector<String>{
        "Бензиновая", "Электрическая", "Комбинированная"
    });
    lists.emplace_back("Режим обучения оператора", vector<String>{
        "Автоматический", "Ручной", "Интерактивный"
    });
    lists.emplace_back("Тип системы навигации", vector<String>{
        "Геопозиционирование", "Инфракрасная", "Лидар"
    });
    lists.emplace_back("Режим комплексной диагностики", vector<String>{
        "Автоматический", "Ручной", "Реальный"
    });
    lists.emplace_back("Тип системы автоматического управления", vector<String>{
        "Искусственный интеллект", "Машинное обучение", "Правила и сценарии"
    });
    lists.emplace_back("Режим восстановления системы", vector<String>{
        "Автоматический", "Ручной", "Режим резервного копирования"
    });



    // 2. Полосы прокрутки
    polosas.emplace_back("Настройка уровня звука уведомлений, %", 0, 100, 1);
    polosas.emplace_back("Мощность нагрева, кВт", 0.1, 10, 0.1);
    polosas.emplace_back("Скорость подачи материала, кг/ч", 0, 200, 1);
    polosas.emplace_back("Объем копии, л", 0.1, 10, 0.1);
    polosas.emplace_back("Настройка режима вибрации, Гц", 0, 500, 5);
    polosas.emplace_back("Длина сегмента, м", 0.5, 100, 0.5);
    polosas.emplace_back("Режим работы привода, номер", 1, 5, 1);
    polosas.emplace_back("Интенсивность ультрафиолетового излучения, мВт/м²", 0, 300, 5);
    polosas.emplace_back("Частота циклона, Гц", 0, 2000, 20);
    polosas.emplace_back("Параметр охлаждающей жидкости, °C", 10, 50, 1);
    polosas.emplace_back("Коэффициент сжатия, ед", 5, 20, 1);
    polosas.emplace_back("Количество ступеней фильтрации, шт", 1, 5, 1);
    polosas.emplace_back("Настройка времени задержки, мс", 0, 1000, 10);
    polosas.emplace_back("Кол-во оборотов, об/мин", 0, 3000, 50);
    polosas.emplace_back("Источники питания, число", 1, 4, 1);
    polosas.emplace_back("Режим интенсивной работы, переключатель", 0, 1, 1);
    polosas.emplace_back("Время перехода, сек", 1, 60, 1);
    polosas.emplace_back("Температура датчика, °C", -20, 80, 1);
    polosas.emplace_back("Объем газа, м³", 0.1, 10, 0.1);
    polosas.emplace_back("Настройка уровня шума, дБ", 50, 100, 1);
    polosas.emplace_back("Позиция компонента, мм", 0, 500, 1);
    polosas.emplace_back("Нагрузочная амплитуда, мм", 0, 5, 0.1);
    polosas.emplace_back("Настройка режима поляризации, номер", 1, 3, 1);
    polosas.emplace_back("Количество циклов, шт", 1, 50, 1);
    polosas.emplace_back("Длительность обработочного цикла, сек", 10, 600, 10);
    polosas.emplace_back("Параметр фокусировки, мм", 100, 1000, 10);
    polosas.emplace_back("Объем используемой жидкости, мл", 50, 1000, 50);
    polosas.emplace_back("Параметр светового излучения, мВт/м²", 0, 1000, 10);
    polosas.emplace_back("Настройка чувствительности сенсора, ед", 0, 100, 1);
    polosas.emplace_back("Длина волны, нм", 400, 700, 10);
    polosas.emplace_back("Степень выхода, %", 0, 100, 1);
    polosas.emplace_back("Время активации, сек", 1, 300, 1);
    polosas.emplace_back("Параметр диаметра, мм", 1, 20, 0.1);
    polosas.emplace_back("Интенсивность электромагнитного поля, мТл", 0, 50, 1);
    polosas.emplace_back("Объем воска, мл", 1, 100, 1);
    polosas.emplace_back("Длина установки, м", 0.5, 10, 0.5);
    polosas.emplace_back("Линейный коэффициент, ед", 1, 10, 1);
    polosas.emplace_back("Настройка скорости пылесоса, об/мин", 0, 1500, 50);
    polosas.emplace_back("Параметр времени наладки, сек", 1, 300, 1);
    polosas.emplace_back("Параметр мощности вентилятора, Вт", 50, 3000, 50);
    polosas.emplace_back("Высота позиционирования, мм", 0, 1000, 10);
    polosas.emplace_back("Длина волны цвета, нм", 400, 700, 10);
    polosas.emplace_back("Время обработки, мин", 1, 180, 1);
    polosas.emplace_back("Параметр циклического режима, №", 1, 10, 1);
    polosas.emplace_back("Уровень защиты от пыли, %", 0, 100, 1);
    polosas.emplace_back("Настройка интенсивности вибрации, м/с²", 0, 20, 0.5);
    polosas.emplace_back("Настройка частоты импульсов, Гц", 1, 2000, 50);
    polosas.emplace_back("Объем контейнера, л", 0.5, 50, 1);
    polosas.emplace_back("Параметр уровня электромагнитного излучения, мВт/м²", 0, 500, 10);
    polosas.emplace_back("Время работы нагревателя, сек", 10, 3600, 60);


    
    // 3. Выводимые строки - название параметра и варианты описания (должно быть несколько)
    // СОЗДАТЬ 50 уникальных записей
    outStrings.emplace_back("Тип смазочного масла", vector<String>{"Минеральное", "Полностью синтетическое", "Полусинтетическое"});
    outStrings.emplace_back("Максимальная скорость подачи, мм/мин", vector<String>{"100", "200", "300", "400"});
    outStrings.emplace_back("Тип охлаждающей жидкости", vector<String>{"Антифриз", "Дистиллированная вода", "Минеральная вода"});
    outStrings.emplace_back("Класс точности сборки", vector<String>{"Класс А", "Класс Б", "Класс В"});
    outStrings.emplace_back("Режим работы станка", vector<String>{"Автоматический", "Полуавтоматический", "Ручной"});
    outStrings.emplace_back("Толщина пластин", vector<String>{"0.5 мм", "1 мм", "2 мм"});
    outStrings.emplace_back("Тип привода", vector<String>{"Электрический", "Гидравлический", "Пневматический"});
    outStrings.emplace_back("Степень износа детали", vector<String>{"Новая", "Значительная изношенность", "Средняя изношенность"});
    outStrings.emplace_back("Температурный режим обработки", vector<String>{"Низкотемпературный", "Нормальный", "Высокотемпературный"});
    outStrings.emplace_back("Тип резцедержателя", vector<String>{"Цилиндрический", "Кегельный", "Патронный"});
    outStrings.emplace_back("Диаметр вращающейся детали, мм", vector<String>{"50", "100", "150", "200"});
    outStrings.emplace_back("Форма профиля", vector<String>{"Круглый", "Квадратный", "Многоугольный"});
    outStrings.emplace_back("Толщина слоя нанесения, мм", vector<String>{"0.1", "0.5", "1"});
    outStrings.emplace_back("Тип инструмента", vector<String>{"Фреза", "Торцевая", "Фреза с пластиковой вставкой"});
    outStrings.emplace_back("Длина шпинделя, мм", vector<String>{"50", "100", "150"});
    outStrings.emplace_back("Параметр точности позиционирования", vector<String>{"0.01 мм", "0.05 мм", "0.1 мм"});
    outStrings.emplace_back("Степень автоматизации", vector<String>{"Нет автоматизации", "Частичная", "Полная"});
    outStrings.emplace_back("Тип соединения деталей", vector<String>{"Сварка", "Клёпка", "Механическое соединение"});
    outStrings.emplace_back("Максимальный крутящий момент, Нм", vector<String>{"50", "100", "150", "200"});
    outStrings.emplace_back("Тип рабочего стола", vector<String>{"Вибрационный", "Поворотный", "Статичный"});
    outStrings.emplace_back("Контроль зазорных расстояний", vector<String>{"Автоматический", "Ручной"});
    outStrings.emplace_back("Энергопотребление станка, кВт", vector<String>{"1 кВт", "2 кВт", "3 кВт"});
    outStrings.emplace_back("Диапазон регулировки скорости, об/мин", vector<String>{"0-500", "0-1000", "0-1500"});
    outStrings.emplace_back("Тип системы управления", vector<String>{"ЧПУ", "Механическая", "Пневматическая"});
    outStrings.emplace_back("Тип датчика положения", vector<String>{"Оптический", "Индуктивный", "Конденсаторный"});
    outStrings.emplace_back("Длина кабеля управления, м", vector<String>{"2", "5", "10"});
    outStrings.emplace_back("Тип крепления инструмента", vector<String>{"Гайка", "Ключевой слот", "Пневмозажим"});
    outStrings.emplace_back("Класс защиты электроприбора", vector<String>{"IP54", "IP65", "IP67"});
    outStrings.emplace_back("Время работы без обслуживания, ч", vector<String>{"50", "100", "200"});
    outStrings.emplace_back("Количество рабочих циклов ", vector<String>{"100", "500", "1000"});
    outStrings.emplace_back("Тип покрытия детали", vector<String>{"Цинковое покрытие", "Анодирование", "Порошковая покраска"});
    outStrings.emplace_back("Тип контроля качества", vector<String>{"Визуальный", "Лазерный", "Ультразвуковой"});
    outStrings.emplace_back("Масса станка, кг", vector<String>{"500", "1000", "1500"});
    outStrings.emplace_back("Высота установки, м", vector<String>{"1.5", "2.0", "2.5"});
    outStrings.emplace_back("Калибровка инструмента", vector<String>{"Автоматическая", "Ручная"});
    outStrings.emplace_back("Конструкция оправки", vector<String>{"Штифтовая", "Пазовая", "Клёпочная"});
    outStrings.emplace_back("Тип тормозной системы", vector<String>{"Магнитная", "Гидравлическая", "Электромагнитная"});
    outStrings.emplace_back("Максимальный размер детали, мм", vector<String>{"200", "500", "1000"});
    outStrings.emplace_back("Контроль температуры в зоне обработки", vector<String>{"Обязательный", "Опциональный"});
    outStrings.emplace_back("Тип рулонного материала", vector<String>{"Сталь", "Алюминий", "Медь"});
    outStrings.emplace_back("Энергосберегающий режим", vector<String>{"Включен", "Выключен"});
    outStrings.emplace_back("Тип привода подачи", vector<String>{"Ременной", "Зубчатый", "Гидравлический"});
    outStrings.emplace_back("Объем автоматической смазки, мл", vector<String>{"50", "100", "150"});
    outStrings.emplace_back("Параметр амплитуды вибрации, мм", vector<String>{"0.1", "0.5", "1.0"});
    outStrings.emplace_back("Длина патрона, мм", vector<String>{"50", "75", "100"});
    outStrings.emplace_back("Время охлаждения, мин", vector<String>{"1", "2", "5"});
    outStrings.emplace_back("Размер резьбы, мм", vector<String>{"M6", "M10", "M20"});
    outStrings.emplace_back("Степень автоматизации контроля", vector<String>{"Ручной", "Пошаговый", "Автоматический"});
    outStrings.emplace_back("Тип подачи, шпиндель/планетарный", vector<String>{"Шпиндель", "Планетарный"});
    outStrings.emplace_back("Режим регулировки скорости", vector<String>{"Плавный", "Шаговый"});



    // 4. Выходные полосы
    // СОздать 50 уникальных записи - название параметра, мин.знач (0), макс.знач(200), шаг(1), ЧИСЛА ЗАМЕНИТЬ
    outPolosas.emplace_back("Яркость дисплея, %", 0, 100, 1);
    outPolosas.emplace_back("Громкость, %", 0, 100, 1);
    outPolosas.emplace_back("Температура в салоне, °C", 10, 30, 1);
    outPolosas.emplace_back("Уровень масла, %", 0, 100, 1);
    outPolosas.emplace_back("Давление воздуха, кПа", 80, 300, 10);
    outPolosas.emplace_back("Скорость вентилятора, об/мин", 0, 5000, 100);
    outPolosas.emplace_back("Уровень топлива, %", 0, 100, 1);
    outPolosas.emplace_back("Скорость езды, км/ч", 0, 25, 0.5);
    outPolosas.emplace_back("Диаметр шины, мм", 600, 2000, 50);
    outPolosas.emplace_back("Длина жатки, м", 1, 15, 0.5);
    outPolosas.emplace_back("Ширина обработки, м", 1, 12, 0.5);
    outPolosas.emplace_back("Высота сцепки, м", 0.5, 3, 0.1);
    outPolosas.emplace_back("Частота вращения, Гц", 0, 60, 1);
    outPolosas.emplace_back("Интенсивность освещенности, люкс", 0, 100000, 1000);
    outPolosas.emplace_back("Скорость работы, км/ч", 0, 40, 0.5);
    outPolosas.emplace_back("Время работы, ч", 0, 24, 0.5);
    outPolosas.emplace_back("Продолжительность цикла, мин", 1, 120, 1);
    outPolosas.emplace_back("Магнитное поле, мТ", 0, 500, 10);
    outPolosas.emplace_back("Мощность, кВт", 0, 100, 0.5);
    outPolosas.emplace_back("Вибрация, м/с²", 0, 20, 0.2);
    outPolosas.emplace_back("Текущий уровень, А", 0, 50, 1);
    outPolosas.emplace_back("Потребление энергии, кВт-ч", 0, 50, 0.5);
    outPolosas.emplace_back("Размер зерна, мм", 2, 12, 0.2);
    outPolosas.emplace_back("Значение pH", 0, 14, 0.5);
    outPolosas.emplace_back("Длина кабеля, м", 1, 100, 1);
    outPolosas.emplace_back("Высота проема, м", 1, 5, 0.2);
    outPolosas.emplace_back("Расход воды, л/мин", 0, 50, 1);
    outPolosas.emplace_back("Высота уборки, м", 0.2, 2, 0.1);
    outPolosas.emplace_back("Масса, кг", 50, 2000, 10);
    outPolosas.emplace_back("Мощность датчика, Вт", 0, 10, 0.1);
    outPolosas.emplace_back("Частота, Гц", 50, 60, 1);
    outPolosas.emplace_back("Температура окружающей среды, °C", -50, 50, 5);
    outPolosas.emplace_back("Время отклика, мс", 0, 100, 1);
    outPolosas.emplace_back("Интенсивность шума, дБ", 0, 120, 1);
    outPolosas.emplace_back("Количество импульсов, в минуту", 0, 300, 1);
    outPolosas.emplace_back("Влажность воздуха, %", 0, 100, 1);
    outPolosas.emplace_back("Длина кабеля датчика, м", 1, 10, 0.5);
    outPolosas.emplace_back("Объем резервуара, л", 1, 500, 10);
    outPolosas.emplace_back("Расход топлива, л/ч", 0, 20, 0.1);
    outPolosas.emplace_back("Давление, бар", 0, 10, 0.2);
    outPolosas.emplace_back("Потребляемая мощность, Вт", 10, 500, 10);
    outPolosas.emplace_back("Время реакции, мс", 0, 500, 5);
    outPolosas.emplace_back("Интенсивность пульсаций, Гц", 0, 1000, 10);
    outPolosas.emplace_back("Длина волны, нм", 200, 800, 10);
    outPolosas.emplace_back("Объем воздуха, м³/ч", 0, 20, 0.5);
    outPolosas.emplace_back("Температура охлаждающей жидкости, °C", -30, 15, 5);
    outPolosas.emplace_back("Электрическая мощность, кВт", 0, 50, 0.5);
    outPolosas.emplace_back("Длительность цикла, сек", 1, 300, 5);
    outPolosas.emplace_back("Степень сжатия", 5, 25, 1);
    outPolosas.emplace_back("Длина кабеля питания, м", 1, 30, 1);

    
    // 5. Графики - просто разные абстрактные названия графиков
    // 50 уникальных названий
    graphs.emplace_back("График с ID 1739");
    graphs.emplace_back("График с ID 48216");
    graphs.emplace_back("График с ID 9054");
    graphs.emplace_back("График с ID 26731");
    graphs.emplace_back("График с ID 1048");
    graphs.emplace_back("График с ID 559827");
    graphs.emplace_back("График с ID 8342");
    graphs.emplace_back("График с ID 62215");
    graphs.emplace_back("График с ID 3917");
    graphs.emplace_back("График с ID 14489");
    graphs.emplace_back("График с ID 7824");
    graphs.emplace_back("График с ID 51236");
    graphs.emplace_back("График с ID 2891");
    graphs.emplace_back("График с ID 90173");
    graphs.emplace_back("График с ID 46721");
    graphs.emplace_back("График с ID 3158");
    graphs.emplace_back("График с ID 10423");
    graphs.emplace_back("График с ID 6442");
    graphs.emplace_back("График с ID 85591");
    graphs.emplace_back("График с ID 71234");
    graphs.emplace_back("График с ID 4890");
    graphs.emplace_back("График с ID 12678");
    graphs.emplace_back("График с ID 50216");
    graphs.emplace_back("График с ID 3984");
    graphs.emplace_back("График с ID 17459");
    graphs.emplace_back("График с ID 6654");
    graphs.emplace_back("График с ID 2410");
    graphs.emplace_back("График с ID 78192");
    graphs.emplace_back("График с ID 5237");
    graphs.emplace_back("График с ID 63190");
    graphs.emplace_back("График с ID 8192");
    graphs.emplace_back("График с ID 45920");
    graphs.emplace_back("График с ID 1483");
    graphs.emplace_back("График с ID 67514");
    graphs.emplace_back("График с ID 3087");
    graphs.emplace_back("График с ID 91156");
    graphs.emplace_back("График с ID 15720");
    graphs.emplace_back("График с ID 6894");
    graphs.emplace_back("График с ID 82415");
    graphs.emplace_back("График с ID 2379");
    graphs.emplace_back("График с ID 56342");
    graphs.emplace_back("График с ID 4980");
    graphs.emplace_back("График с ID 7516");
    graphs.emplace_back("График с ID 6124");
    graphs.emplace_back("График с ID 95081");
    graphs.emplace_back("График с ID 13456");
    graphs.emplace_back("График с ID 8452");
    graphs.emplace_back("График с ID 3907");
    graphs.emplace_back("График с ID 71024");
    graphs.emplace_back("График с ID 48109");

}



// Шаблон функции получения случайного элемента с удалением
template<typename T>
T getAndRemoveRandomFromVector(vector<T>& vec) {
    T t0;
    if (vec.empty()) return t0;
    int index = rand() % vec.size();
    t0 = vec[index];
    vec.erase(vec.begin() + index);
    return t0;
}

// Перечисление типов элементов UI
enum e { button, checkbox, list, str, polosa, caption, graphic, graphicd, polosa_out };

// Класс el (элемент пользовательского интерфейса)
class el {
public:
    String name;
    vector<String> subs;     // Подчинённые элементы
    String action;           // Действие
    String when;             // Когда выполнять
    e type;                  // Тип элемента
    int is_out = 0;          // Является ли выходным параметром
    int in_end = 0;          // Переносить в конец
    double maxTime = 0;
    double maxParam = 0;
    vector<String> params;   // Передаваемые параметры
    vector<String> zn;       // Значения для списков
    vector<String> kods;     // Коды для значений
    double min = 0;
    double max = 100;
    double step = 1;
    
    String value; // Значение
    String firstValue; // Значение по умолчанию
    el() {}

    void print() {
        Serial.print(" ● ");
        switch (type) {
            case button: {
                String s = name + " " + "Кнопка" + " " + action;
                for (int i=0; i<subs.size(); i++) {
                    s += " " + String(subs[i].c_str());
                }
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case str: {
                String s = name + " Строка " + action + (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case checkbox: {
                String s = name + " " + "Флажок" + " " + action + " " + when;
                for (int i=0; i<subs.size(); i++) {
                    s += " " + String(subs[i].c_str());
                }
                s +=  (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case caption: {
                String s = "out " + name + " Надпись " + when +  (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case list: {
                String s = name + " " + "Список" + " " + action + " " + when;
                for (int i=0; i<zn.size(); i++) {
                    s += " " + String(zn[i].c_str());
                    if (action == "ЗначениеКод" && i<kods.size()) s += " " + String(kods[i].c_str());
                }
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case polosa: {
                String s = name + " ПолосаПрокрутки " + "Мин " + String(min) + " Макс " + String(max) + " Шаг " + String(step) + " " + when;
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                break;
            }
            case polosa_out: {
                String s = "out " + name + " ПолосаПрокрутки " + "Мин " + String(min) + " Макс " + String(max) + " Шаг " + String(step) + " " + when;
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                // Добавление доверительного интервала
                double min_int = min+(max-min)*(rand()%50/100.0);
                double max_int = min_int+(max-min_int)*((50+rand()%50)/100.0);
                String s2 = "interval " + name + " " + String(min_int) + " " + String(max_int);
                Serial.println(s2);
                client.println(s2);
                break;
            }
            case graphic: {
                String s = "out " + name + " " + "График" + " " + when + " " + "МаксВремя " + String(maxTime) + " " + "МаксПарам " + String(maxParam) + " Параметры";
                for (int i=0; i<subs.size(); i++) {
                    s += " " + String(subs[i].c_str());
                }
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                // Добавление доверительного интервала
                for (int i=0; i<subs.size(); i++) {
                    String n = subs[i].c_str();
                    double min_int = maxParam*(rand()%50/100.0);
                    double max_int = min_int+(maxParam-min_int)*((50+rand()%50)/100.0);
                    String s2 = "interval " + n + " " + String(min_int) + " " + String(max_int);
                    Serial.println(s2);
                    client.println(s2);
                }
                break;
            }
            case graphicd: {
                String s = "out " + name + " " + "ДГрафик" + " " + when + " " + "МаксПарам " + String(maxParam) + " Параметры";
                for (int i=0; i<subs.size(); i++) {
                    s += " " + String(subs[i].c_str());
                }
                s += (in_end ? " end" : "");
                Serial.println(s);
                client.println(s);
                // Добавление доверительного интервала
                for (int i=0; i<subs.size(); i++) {
                    String n = subs[i].c_str();
                    double min_int = maxParam*(rand()%50/100.0);
                    double max_int = min_int+(maxParam-min_int)*((50+rand()%50)/100.0);
                    String s2 = "interval " + n + " " + String(min_int) + " " + String(max_int);
                    Serial.println(s2);
                    client.println(s2);
                }
                break;
            }
            default:
                Serial.println("Неизвестный тип элемента");
        }
    }
};

// ------------------------------
// Класс группы элементов
class group {
public:
    vector<el> elements;

    void print() {
        for (int i=0; i<elements.size(); i++) {
            elements[i].print();
        }
    }
};

// Функции создания групп
group start_stop() {
    vector<el> elements;
    el temp;
    temp.type = e::button;
    temp.name = "Старт";
    temp.action = "НачалоПроцесса";
    temp.in_end = 1;
    elements.push_back(temp);

    el temp2;
    temp2.type = e::button;
    temp2.name = "Стоп";
    temp2.action = "ПрерываниеПроцесса";
    temp2.in_end = 1;
    elements.push_back(temp2);

    group g;
    g.elements = elements;
    return g;
}

group strcommand() {
    vector<el> elements;
    el temp;
    temp.type = e::str;
    temp.name = "Команда";
    temp.action = "Команда";
    temp.in_end = 1;
    elements.push_back(temp);

    el temp2;
    temp2.type = e::button;
    temp2.name = "Выполнить";
    temp2.action = "ОтправкаКомандыИзСтроки";
    temp2.subs.push_back(temp.name);
    temp2.in_end = 1;
    elements.push_back(temp2);

    group g;
    g.elements = elements;
    return g;
}

group flagmoment(int i) {
    CB cb0 = getAndRemoveRandomFromVector(cbOptions);
    String s = cb0.name;
    vector<el> elements;

    el temp;
    temp.type = e::caption;
    temp.name = "Состояние_параметра_" + s;
    temp.when = "Постоянно";
    temp.is_out = 1;
    temp.subs = cb0.options;

    el temp2;
    temp2.type = e::checkbox;
    temp2.name = s;
    temp2.action = "БинарноеУправление";
    temp2.when = "Моментально";
    temp2.subs.push_back(temp.name);

    temp.firstValue = cb0.options[1];
    temp2.firstValue = "0";
    elements.push_back(temp);
    elements.push_back(temp2);

    group g;
    g.elements = elements;
    return g;
}

group flagafter(int i) {
    CB cb0 = getAndRemoveRandomFromVector(cbOptions);
    String s = cb0.name;
    vector<el> elements;

    el temp;
    temp.type = e::caption;
    temp.name = "Состояние_параметра_" + s;
    temp.when = "ВПроцессе";
    temp.is_out = 1;
    temp.subs = cb0.options;

    el temp2;
    temp2.type = e::checkbox;
    temp2.name = s;
    temp2.action = "БинарноеУправление";
    temp2.when = "ПоСтарту";
    temp2.subs.push_back(temp.name);

    temp.firstValue = cb0.options[1];
    temp2.firstValue = "0";

    elements.push_back(temp);
    elements.push_back(temp2);

    
    group g;
    g.elements = elements;
    return g;
}

group onecaption(int i) {
    OutString os = getAndRemoveRandomFromVector(outStrings);
    String s = os.label;
    vector<el> elements;
    el temp;
    temp.type = e::caption;
    temp.name = s;
    temp.subs = os.vars;
    int j = rand() % 3;
    if (j == 0) temp.when = "Постоянно";
    else if (j == 1) temp.when = "ВПроцессе";
    else if (j == 2) temp.when = "ВКонце";

    temp.is_out = 1;
    temp.firstValue = os.vars[0];
    elements.push_back(temp);

    
    group g;
    g.elements = elements;
    return g;
}

group listmoment(int i) {
    String firstValue = "", firstvalue2 = "";
    List l = getAndRemoveRandomFromVector(lists);
    String s = l.name;
    vector<el> elements;
    el temp;
    temp.type = e::caption;
    temp.name = "Состояние_параметра_" + s;
    temp.when = "Постоянно";
    temp.is_out = 1;

    el temp2;
    temp2.type = e::list;
    temp2.name = s;

    int t = rand() % 2;
    int k = rand() % 9 + 2;
    temp2.zn = l.options;
    if (t == 0) {
        temp2.action = "Значение";
        firstValue = l.options[0];
        firstvalue2 = l.options[0];
    } else {
        temp2.action = "ЗначениеКод";
        for (int j=1; j<=temp2.zn.size(); j++) {
            int kod = round(rand() % 1000);
            temp2.kods.push_back(String(kod));
        }
        firstValue = l.options[0];
        firstvalue2 = temp2.kods[0];
    }
    temp2.when = "Моментально";
    temp2.subs.push_back(temp.name);

    temp.firstValue = firstvalue2;
    temp2.firstValue = firstValue;
    elements.push_back(temp);
    elements.push_back(temp2);

    group g;
    g.elements = elements;
    return g;
}

group listafter(int i) {
    String firstValue = "", firstvalue2 = "";
    List l = getAndRemoveRandomFromVector(lists);
    String s = l.name;
    vector<el> elements;
    el temp;
    temp.type = e::caption;
    temp.name = "Состояние_параметра_" + s;
    temp.when = "ВПроцессе";
    temp.is_out = 1;

    el temp2;
    temp2.type = e::list;
    temp2.name = s;

    int t = rand() % 2;
    int k = rand() % 9 + 2;
    temp2.zn = l.options;
    if (t == 0) {
        temp2.action = "Значение";
        firstValue = l.options[0];
        firstvalue2 = l.options[0];
    } else {
        temp2.action = "ЗначениеКод";
        for (int j=1; j<=temp2.zn.size(); j++) {
            int kod = round(rand() % 1000);
            temp2.kods.push_back(String(kod));
        }
        firstValue = l.options[0];
        firstvalue2 = temp2.kods[0];
    }
    temp2.when = "ПоСтарту";
    temp2.subs.push_back(temp.name);

    temp.firstValue = firstvalue2;
    temp2.firstValue = firstValue;
    elements.push_back(temp);
    elements.push_back(temp2);

    group g;
    g.elements = elements;
    return g;
}

group nullparams() {
    vector<el> elements;
    el temp;
    temp.type = e::button;
    temp.name = "Сброс_параметров";
    temp.action = "ПараметрыПоУмолчанию";
    elements.push_back(temp);

    group g;
    g.elements = elements;
    return g;
}

group newpolosa(int i) {
    Polosa p = getAndRemoveRandomFromVector(polosas);
    String s = p.name;
    vector<el> elements;
    el temp;

    int t = rand() % 5;
    if (t > 0) {
        temp.name = s;
        temp.type = e::polosa;
        if (rand() % 2 == 0)
            temp.when = "Моментально";
        else
            temp.when = "ПоСтарту";
    } else {
        temp.name = "Состояние_параметра_" + s;
        temp.type = e::polosa_out;
        temp.is_out = 1;
        int t2 = rand() % 3;
        if (t2 == 0) temp.when = "Постоянно";
        else if (t2 == 1) temp.when = "ВПроцессе";
        else temp.when = "ВКонце";
    }
    temp.min = p.min_val;
    temp.max = p.max_val;
    temp.step = p.step;
    
    temp.firstValue = String(temp.min);

    elements.push_back(temp);

    group g;
    g.elements = elements;
    return g;
}

group newgraphic(vector<el> els, int i, int n) {
    Graph g0 = getAndRemoveRandomFromVector(graphs);
    String s = g0.name;
    vector<el> elements;
    el temp;
    temp.type = e::graphic;
    temp.name = s;
    temp.is_out = 1;
    if (n == 1)
        temp.when = "Постоянно";
    else
        temp.when = "ВПроцессе";

    int maxVal = 0;
    for (int j = 0; j < els.size(); j++) {
        if (els[j].max > maxVal) maxVal = els[j].max;
    }
    temp.maxParam = maxVal;
    temp.maxTime = 10 + rand() % 50;

    for (int j=0; j<els.size(); j++) {
        temp.subs.push_back(els[j].name);
    }

    elements.push_back(temp);

    group g;
    g.elements = elements;
    return g;
}

group newDgraphic(vector<el> els, int i) {
    Graph g0 = getAndRemoveRandomFromVector(graphs);
    String s = g0.name;
    vector<el> elements;
    el temp;
    temp.type = e::graphicd;
    temp.name = s;
    temp.when = "ВПроцессе";
    temp.is_out = 1;

    int maxVal = 0;
    for (int j=0; j<els.size(); j++) {
        if (els[j].max > maxVal) maxVal = els[j].max;
    }
    temp.maxParam = maxVal;

    for (int j=0; j<els.size(); j++) {
        temp.subs.push_back(els[j].name);
    }

    elements.push_back(temp);

    group g;
    g.elements = elements;
    return g;
}


vector<group> groups; // Группы случайных значений
// Для теста в Arduino реализуем 'main()' в виде функции initAllAndRun()
void initAllAndRun() {
    init_all();

    group g = start_stop();
    groups.push_back(g);

    g = strcommand();
    groups.push_back(g);

    int k = 1+round(rand() % 4);
    int i = 1, i0 = 0;
    for (i=1; i <= i0 + k; i++) {
        g = flagmoment(i);
        groups.push_back(g);
    }

    k = 1+round(rand() % 4);
    i0 = i - 1;
    for (; i <= i0 + k; i++) {
        g = flagafter(i);
        groups.push_back(g);
    }

    k = 1+round(rand() % 4);
    i0 = i - 1;
    for (; i <= i0 + k; i++) {
        g = onecaption(i);
        groups.push_back(g);
    }

    k = 1+round(rand() % 4);
    i0 = i - 1;
    for (; i <= i0 + k; i++) {
        g = listmoment(i);
        groups.push_back(g);
    }

    k = 1+round(rand() % 4);
    i0 = i - 1;
    for (; i <= i0 + k; i++) {
        g = listafter(i);
        groups.push_back(g);
    }

    g = nullparams();
    groups.push_back(g);

    k = 1+round(rand() % 4);
    i0 = i - 1;
    for (; i <= i0 + k; i++) {
        g = newpolosa(i);
        groups.push_back(g);
    }

    // Собираем "моменты" и "графики"
    vector<el> moment;
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.when == "Моментально" && elm.type == e::polosa)
                moment.push_back(elm);
        }
    }
    int k0 = 1;
    if (!moment.empty()) {
        g = newgraphic(moment, k0++, 1);
        groups.push_back(g);
    }


    // Создаём новые графики
    // Параметры для новых графиков
    vector<el> paramEls;
    for (int i=0; i<1+rand()%4; i++) {
        el e;
        Polosa p = getAndRemoveRandomFromVector(polosas);
        String s = p.name;
        e.name = s;
        e.max = p.max_val;
        paramEls.push_back(e);
    }
    if (!paramEls.empty()) {
        g = newgraphic(paramEls, k0++, 2);
        groups.push_back(g);
    }

    paramEls.clear();
    for (int i=0; i<1+rand()%4; i++) {
        el e;
        Polosa p = getAndRemoveRandomFromVector(polosas);
        String s = p.name;
        e.name = s;
        e.max = p.max_val;
        paramEls.push_back(e);
    }
    if (!paramEls.empty()) {
        g = newDgraphic(paramEls, k0);
        groups.push_back(g);
    }

    // Выводим "входные параметры"
    Serial.println("Входные параметры:");
    client.println("Входные параметры:");
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out != 1 && elm.in_end != 1) {
                elm.print();
            }
        }
    }

    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out != 1 && elm.in_end == 1) {
                elm.print();
            }
        }
    }

    // Далее вывод выходных
    Serial.println("Выходные параметры:");
    client.println("Выходные параметры:");
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out == 1)
                elm.print();
        }
    }
    Serial.println("По умолчанию:");
    client.println("По умолчанию:");
}


void do_null(){ // Параметры по умолчанию
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out != 1 && elm.in_end != 1) {
                if(elm.type!=button&&elm.type!=graphic&&elm.type!=graphicd&&elm.type!=str){
                    elm.value = elm.firstValue;
                    client.println(elm.name+" "+elm.value);
                    Serial.println(elm.name+" "+elm.value);
                }
            }
        }
    }
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out != 1 && elm.in_end == 1) {
                if(elm.type!=button&&elm.type!=graphic&&elm.type!=graphicd&&elm.type!=str){
                    elm.value = elm.firstValue;
                    client.println(elm.name+" "+elm.value);
                    Serial.println(elm.name+" "+elm.value);
                }
            }
        }
    }
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.is_out == 1)
                if(elm.type!=button&&elm.type!=graphic&&elm.type!=graphicd&&elm.type!=str){
                    elm.value = elm.firstValue;
                    client.println(elm.name+" "+elm.value);
                    Serial.println(elm.name+" "+elm.value);
                }
        }
    }
}

void check(String s){ // Анализ входной строки
    int spaceIndex = s.indexOf(' '); // ищем позицию пробела
    String s1, s2;
    if (spaceIndex != -1) {
        s1 = s.substring(0, spaceIndex); // первое слово
        s2 = s.substring(spaceIndex + 1); // второе слово
    } else {
        // если пробела нет, то вся строка - одно слово
        s1 = s;
        s2 = "";
    }
    //--------------------------------------
    //--------------------------------------
    // ПОЛУЧЕНИЕ ЗНАЧЕНИЙ ОТ КЛИЕНТА
    //--------------------------------------
    //--------------------------------------
    
    for (auto& g : groups) {
        for (auto& elm : g.elements) {
            if (elm.name==s1) {
                //ЕСЛИ ТИП ЭЛЕМЕНТА КНОПКА
                client.println(String(elm.type));
//----------------------------------------
                if(elm.type== button){
                    if(elm.name=="Старт"){
                        process = 1; 
                        Serial.println("Процесс запущен");
                        for (auto& g : groups) 
                            for (auto& elm : g.elements)
                            if((elm.type==graphic||elm.type==graphicd)&&elm.when!="Постоянно"){
                                client.println(elm.name+" null");
                                Serial.println(elm.name+" null");
                            }
                    }
                    else if(elm.name == "Стоп"){
                        process = 0;  
                        Serial.println("Процесс остановлен"); 
                        client.println("Стоп");
                    }
                    else if(elm.name =="Выполнить"){
                       // Тут якобы выполняется команда
                        Serial.println("Выполнена команда "+s2);        
                    }
                    else if(elm.name=="Сброс_параметров"){
                        // СБРОС ПАРАМЕТРОВ ДО УМОЛЧАНИЯ
                        do_null();
                        Serial.println("Установлены параметры по умолчанию");  
                    }
                }         
//----------------------------------------
//----------------------------------------
                //ЕСЛИ ТИП ЭЛЕМЕНТА ФЛАЖОК
                if(elm.type== checkbox){
                    String s3 = elm.subs[0];
                    // Поиск подчиненного элемента
                    String s01, s02, s0;
                    for (auto& g2 : groups)
                        for (auto& elm2 : g2.elements) 
                        if (elm2.name==s3){
                            s01 = elm2.subs[1];
                            s02 = elm2.subs[0];
                            if(s2.startsWith("0")) s0 = s01; 
                            else s0 = s02;
                            // Если немоментально, копирование из elm должно производиться по старту!!!
                            if(elm.when=="Моментально"){
                            elm2.value = s0;
                            client.println(elm2.name+" "+elm2.value);
                            Serial.println(elm2.name+" "+elm2.value);
                            }
                            elm2.value = s0;
                        }
                }                 
//----------------------------------------
//----------------------------------------
                //ЕСЛИ ТИП ЭЛЕМЕНТА СПИСОК
                if(elm.type== list){
                    String s3 = elm.subs[0];
                    
                    String s0 = s2;
                    if(elm.action=="ЗначениеКод")
                    for (int i=0; i<elm.zn.size(); i++) {
                     if(s2.startsWith(String(elm.kods[i].c_str())))
                     s0 = String(elm.zn[i].c_str());
                    }
                    
                    // Поиск подчиненного элемента
                    for (auto& g2 : groups)
                        for (auto& elm2 : g2.elements){
                            if(elm2.name == s3){
                            // Если немоментально, копирование из elm должно производиться по старту!!!
                                if(elm.when=="Моментально"){
                                elm2.value = s0;
                                client.println(elm2.name+" "+elm2.value);
                                Serial.println(elm2.name+" "+elm2.value);
                                }
                                elm2.value = s0;
                            }
                    }
                }          
//----------------------------------------
//----------------------------------------
                //ЕСЛИ ТИП ЭЛЕМЕНТА ПОЛОСА
                if(elm.type== polosa){
                    elm.value = s2;
                }      
                // РОДИТЕЛЬСКИЕ ГРАФИКИ САМИ ОБНАРУЖАТ ПОЛОСУ ПОСТОЯННО ИЛИ В ПРОЦЕССЕ
//----------------------------------------
//----------------------------------------
                
                
                
                
                
            }
        }
    }
}


void setup() {
    Serial.begin(115200); //открываем монитор вывода
    uint32_t seed = esp_random();
    srand(seed);
    initSPIFFS(); //инициализируем файловую систему esp32
    
    
    Serial.println("\n\n\n");
    Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Готово!" : "Ошибка!");
    Serial.println(WiFi.softAP(ssid,password, 1, 0, 1) ? ("Открыта сеть: "+local_IP) : ("Ошибка!")); 
    server.begin(); // запуск сервера
    Serial.println(ssid);
    Serial.println(password);

    
}



void loop() {
    client = server.available();
    if (client) {
        Serial.println("Клиент подключился");
        process = -1;
        //Инициализация
        Serial.println("\n\n\n");
        init_all(); // Инициализация рандомайзера
        initAllAndRun(); // вызов логики
        for (auto& g : groups) 
            for (auto& elm : g.elements)
                if(elm.type==graphic||elm.type==graphicd){
                    int flag=0;
                    // Не родительский
                    for (auto& g2 : groups)
                        for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm.subs) if(sub==elm2.name) flag++;
                    if(flag==0)
                        for (auto& sub : elm.subs){
                            if(sub[sub.length()-1]=='%') systemControllers.push_back(SystemController(100.0));
                            else {
                                systemControllers.push_back(elm.maxParam);
                            }
                        }
                }

        tn1 = millis();
        tn1_lim = (5+rand()%9)*500;
        tn2 = millis();
        tn2_lim = (5+rand()%9)*500;
        tn3 = millis();
        tn3_lim = (5+rand()%9)*500;
        tn4 = millis();
        tn4_lim = (5+rand()%9)*500;
        tn5 = millis();
        tn6 = millis();
        time1 = 0.0;
        time2 = 0.0;
        time3 = 0.0;
        do_null();
        Serial.println("Старт");
        
        String requestData = "";
        while (client.connected()) {
            
            if (client.available()) {
                start: //Метка для goto
                while (client.available()) {
                    char c = client.read();
                    requestData += c; 
                }
                // Найдем последний символ новой строки
                int lastNewLineIndex = requestData.lastIndexOf('\n');
                // Теперь найдем индексы, чтобы вырезать последнюю непустую строку
                int  secondLastNewLineIndex = requestData.lastIndexOf('\n', lastNewLineIndex - 1);
                
                // Проверяем, что между строками есть данные
                if (lastNewLineIndex != -1 && secondLastNewLineIndex != -1) {
                    String lastLine = requestData.substring(secondLastNewLineIndex + 1, lastNewLineIndex);
                    // Удаляем последнюю строку из requestData, чтобы избежать дублирования
                    requestData = requestData.substring(0, lastNewLineIndex + 1); // Сохраняем до последней новой строки
                    
                    
                    
                    // ОПРЕДЕЛЕНИЕ ЭЛЕМЕНТА И ЧТО С НИМ ДЕЛАТЬ
                    String s = lastLine.c_str(); // исходная строка
                    Serial.println(s);
                    check(s);
                    
                    
                }
                else{ // Если строка пока одна
                    String s = requestData.c_str(); // исходная строка
                    Serial.println(s);
                    check(s);
                    
                }
            }
            

    //--------------------------------------
    //--------------------------------------
    // ПЕРЕДАЧА ДАННЫХ КЛИЕНТУ
    //--------------------------------------
    //--------------------------------------
    
//----------------------------------------
// ПОСТОЯННО

                // Надписи Выходные
                // Неподчиненные Постоянно
            if(millis()-tn1>=tn1_lim){
                tn1 = millis();
                tn1_lim = (5+rand()%9)*500;
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА НАДПИСЬ
                        if((elm.type== caption)&&elm.when=="Постоянно"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                int n = elm.subs.size();
                                int m = rand()%n;
                                String s0 = elm.subs[m];
                                elm.value = s0;
                                Serial.println(elm.name+" "+elm.value);
                                client.println(elm.name+" "+elm.value);
                            }
                        }
                }
                
            }
//----------------------------------------
//----------------------------------------
                // Полосы Выходные
                // Неподчиненные Постоянно
            if(millis()-tn3>=tn3_lim){
                tn3 = millis();
                tn3_lim = (5+rand()%9)*500;
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА ПОЛОСА
                        if((elm.type== polosa_out)&&elm.when=="Постоянно"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                double min=elm.min, max=elm.max, step=elm.step;
                                int n = (max-min)/step;
                                double m = (rand()%n)*step+min;
                                String s0 = String(m);
                                elm.value = s0;
                                Serial.println(elm.name+" "+elm.value);
                                client.println(elm.name+" "+elm.value);
                            }
                        }
                }
                
            }
//----------------------------------------
                // Графики
                // Постоянно (они у нас только родительские)
            if(millis()-tn5>=tn5_lim){
                time1 += tn5_lim/1000.0;
                tn5 = millis();
                for (auto& g : groups) 
                    for (auto& elm : g.elements)
                      if(elm.type==graphic&&elm.when=="Постоянно"){
                            // Просто выписываем значения для графика клиенту
                            String s0;
                            s0 = elm.name + " " + String(time1);
                            for (auto& sub : elm.subs){
                                for (auto& g2 : groups)
                                    for (auto& elm2 : g2.elements) 
                                        if(sub==elm2.name) s0+=" "+elm2.value;
                            }
                            Serial.println(s0);
                            client.println(s0);
                      }
                
            }
//----------------------------------------
// В КОНЦЕ
            if(process==0){
                process = -1;
                // Неподчиненная выходная НАДПИСЬ
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА НАДПИСЬ
                        if((elm.type== caption)&&elm.when=="ВКонце"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                int n = elm.subs.size();
                                int m = rand()%n;
                                String s0 = elm.subs[m];
                                elm.value = s0;
                                Serial.println(elm.name+" "+elm.value);
                                client.println(elm.name+" "+elm.value);
                            }
                        }
                }
                // Неподчинённая выходная ПОЛОСА
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА ПОЛОСА
                        if((elm.type== polosa_out)&&elm.when=="ВКонце"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                double min=elm.min, max=elm.max, step=elm.step;
                                int n = (max-min)/step;
                                double m = (rand()%n)*step+min;
                                String s0 = String(m);
                                elm.value = s0;
                                Serial.println(elm.name+" "+elm.value);
                                client.println(elm.name+" "+elm.value);
                            }
                        }
                }
                
            }
//----------------------------------------
// ПРОЦЕСС ЗАПУЩЕН
            if (process>=1){
                if (client.available()){
                    goto start;
                }
 //----------------------------------------
                // В ПРОЦЕССЕ
                
                // Графики неродительские (у нас они только в процессе)
                if(millis()-tn6>tn6_lim){
                    int k = 0;
                    tn6 = millis();
                    for (auto& g : groups) for (auto& elm : g.elements)
                        if((elm.type==graphic||elm.type==graphicd)&&elm.when=="ВПроцессе"){
                            int f = 0;
                            String s0 = elm.name + " ";
                            for (auto& sub : elm.subs){
                                float m = systemControllers[k].update();
                                if(f==0){
                                    f = -1;
                                    s0 += String(systemControllers[k].get_time())+" ";
                                    if(elm.type==graphicd) s0 += String(systemControllers[k].get_time())+" ";
                                }
                                s0 += String(m)+" ";
                                k++;
                            }
                            Serial.println(s0);
                            client.println(s0);
                        }
                }
                
                // Надписи Выходные
                // Неподчиненные ВПроцессе
            if(millis()-tn2>=tn2_lim){
                tn2 = millis();
                tn2_lim = (5+rand()%9)*500;
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА НАДПИСЬ
                        if((elm.type== caption)&&elm.when=="ВПроцессе"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                int n = elm.subs.size();
                                int m = rand()%n;
                                String s0 = elm.subs[m];
                                elm.value = s0;
                            }
                            Serial.println(elm.name+" "+elm.value);
                            client.println(elm.name+" "+elm.value);
                        }
                }
                
            }
            //----------------------------------------
                // Полосы Выходные
                // Неподчиненные ВПроцессе
            if(millis()-tn4>=tn4_lim){
                tn4 = millis();
                tn4_lim = (5+rand()%9)*500;
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА ПОЛОСА
                        if((elm.type== polosa_out)&&elm.when=="ВПроцессе"){
                            int flag=0;
                            // Не подчинено
                            for (auto& g2 : groups)
                            for (auto& elm2 : g2.elements) 
                            for (auto& sub : elm2.subs) if(sub==elm.name) flag = 1;
                            
                            if(flag==0){
                                double min=elm.min, max=elm.max, step=elm.step;
                                int n = (max-min)/step;
                                double m = (rand()%n)*step+min;
                                String s0 = String(m);
                                elm.value = s0;
                            }
                            Serial.println(elm.name+" "+elm.value);
                            client.println(elm.name+" "+elm.value);
                        }
                }
                
            }
//----------------------------------------
//----------------------------------------
                // Установка парметров 
                // ПО СТАРТУ
                if(process==1){
//----------------------------------------
                for (auto& g : groups) 
                    for (auto& elm : g.elements){
                        //ЕСЛИ ТИП ЭЛЕМЕНТА ФЛАЖОК или СПИСОК
                        if((elm.type== checkbox||elm.type==list)&&elm.when=="Моментально"){
                            String s = elm.subs[0];
                            // Поиск подчиненного элемента
                            for (auto& g2 : groups)
                                for (auto& elm2 : g2.elements) 
                                if (elm2.name==s){
                                    elm2.value = elm.value;
                                    Serial.println(elm2.name+" "+elm2.value);
                                    client.println(elm2.name+" "+elm2.value);
                                }
                                // Копируем из записанного в родительский элемент значения
                        }
                        
                        
                        
                    }
                // Обнуление для графиков
                for(auto &c: systemControllers)
                    c.begin();
                tn6 = millis();
//----------------------------------------
                    process = 2;
                }                       
            }
        }
        Serial.println("Клиент отключился");
    }
}
