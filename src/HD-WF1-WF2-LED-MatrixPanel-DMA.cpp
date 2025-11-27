
#include "hd-wf2-esp32s3-config.h"
#include <esp_err.h>
#include <esp_log.h>
#include "debug.h"
#include <ctime>
#include <ctime>
#include "driver/ledc.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#include <WebServer.h>
#include <ESPmDNS.h>
#include <I2C_BM8563.h>   // Библиотека для работы с часами реального времени BM8563: https://github.com/tanakamasayuki/I2C_BM8563

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
//#include <ElegantOTA.h> // Загрузка прошивки по воздуху (OTA): откройте http://<ipaddress>/update в браузере

#include <ESP32Time.h>
#include <Bounce2.h>



#ifndef PI
#define PI 3.14159265358979323846
#endif

/*----------------------------- Конфигурация WiFi -------------------------------*/
// Здесь задаются параметры для подключения устройства к беспроводной сети WiFi.
// SSID — имя сети, wifi_pass — пароль.

const char *wifi_ssid = "ZaLuPeN";
const char *wifi_pass = "14789632";

/*----------------------------- Работа с RTC и NTP -------------------------------*/
// Настройка работы с часами реального времени (RTC) и синхронизация времени через интернет (NTP).
// RTC используется для автономного хранения времени, NTP — для получения точного времени из сети.

I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire1);
const char* ntpServer         = "pool.ntp.org";
const char* ntpLastUpdate     = "/ntp_last_update.txt";

// Смещение времени относительно GMT (часовой пояс). Например, GMT+2.
#define CLOCK_GMT_OFFSET 2

/*-------------------------- Настройка LED-матрицы HUB75E через DMA -----------------------------*/
// Здесь задаются параметры для работы с LED-матрицей:
// PANEL_RES_X — ширина одного модуля матрицы в пикселях
// PANEL_RES_Y — высота одного модуля матрицы в пикселях
// PANEL_CHAIN — количество модулей, соединённых в цепочку
#define PANEL_RES_X 128      // Количество пикселей по ширине одного модуля LED-матрицы
#define PANEL_RES_Y 64       // Количество пикселей по высоте одного модуля LED-матрицы
#define PANEL_CHAIN 1        // Общее количество модулей, соединённых в цепочку


HUB75_I2S_CFG::i2s_pins _pins_x1 = {WF2_X1_R1_PIN, WF2_X1_G1_PIN, WF2_X1_B1_PIN, WF2_X1_R2_PIN, WF2_X1_G2_PIN, WF2_X1_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X1_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};
HUB75_I2S_CFG::i2s_pins _pins_x2 = {WF2_X2_R1_PIN, WF2_X2_G1_PIN, WF2_X2_B1_PIN, WF2_X2_R2_PIN, WF2_X2_G2_PIN, WF2_X2_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X2_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};


/*-------------------------- Экземпляры классов ------------------------------*/
// Здесь создаются объекты для работы с:
// - Веб-сервером (WebServer) — обработка HTTP-запросов, OTA-обновления
// - WiFi (WiFiMulti) — подключение к нескольким сетям
// - RTC (ESP32Time) — внутренние часы ESP32
// - LED-матрицей (MatrixPanel_I2S_DMA) — управление отображением
// - Кнопкой (Bounce2::Button) — обработка нажатий с подавлением дребезга
WebServer           webServer;
WiFiMulti           wifiMulti;
ESP32Time           esp32rtc;  // Внутренние часы ESP32, смещение в секундах относительно GMT+1
MatrixPanel_I2S_DMA *dma_display = nullptr;

// Создание объекта кнопки с использованием библиотеки Bounce2 для корректной обработки нажатий
Bounce2::Button button = Bounce2::Button();

// Управление задачами FreeRTOS (например, для работы с LED и другими функциями)
TaskHandle_t Task1;
TaskHandle_t Task2;

#include "led_pwm_handler.h"

RTC_DATA_ATTR int bootCount = 0; // Переменная, сохраняющая количество перезапусков устройства (RTC_DATA_ATTR — хранение в RTC памяти)

// Режимы отображения на LED-матрице
enum DisplayMode {
  MODE_CLOCK_WITH_ANIMATION = 0,
  MODE_CLOCK_ONLY = 1,
  MODE_BOUNCING_SQUARES = 2,
  MODE_COUNT = 3
};

DisplayMode currentDisplayMode = MODE_CLOCK_WITH_ANIMATION;
unsigned long buttonPressStartTime = 0;
bool buttonPressHandled = false;
volatile bool buttonPressed = false;

// Переменные для анимации «прыгающих квадратов» на LED-матрице
struct BouncingSquare {
  float x, y;
  float vx, vy;
  uint16_t color;
  int size;
};

const int NUM_SQUARES = 3;
BouncingSquare squares[NUM_SQUARES];

IRAM_ATTR void toggleButtonPressed() {
  // Эта функция вызывается при срабатывании прерывания на пине кнопки PUSH_BUTTON_PIN
  buttonPressed = true;
  ESP_LOGI("toggleButtonPressed", "Interrupt Triggered.");
}

// Инициализация параметров для анимации прыгающих квадратов
void initBouncingSquares() {
  for (int i = 0; i < NUM_SQUARES; i++) {
    squares[i].x = random(0, PANEL_RES_X - 8);
    squares[i].y = random(0, PANEL_RES_Y - 8);
    squares[i].vx = random(1, 4) * (random(0, 2) ? 1 : -1);
    squares[i].vy = random(1, 4) * (random(0, 2) ? 1 : -1);
    squares[i].size = random(4, 8);
    squares[i].color = dma_display->color565(random(100, 255), random(100, 255), random(100, 255));
  }
}

// Обновление положения и отрисовка прыгающих квадратов на LED-матрице
void updateBouncingSquares() {
  dma_display->clearScreen();
  
  for (int i = 0; i < NUM_SQUARES; i++) {
    // Обновление координат квадрата согласно скорости движения
    squares[i].x += squares[i].vx * 0.5;
    squares[i].y += squares[i].vy * 0.5;
    
    // Отскок квадрата от границ экрана (инверсия направления при столкновении)
    if (squares[i].x <= 0 || squares[i].x >= PANEL_RES_X - squares[i].size) {
      squares[i].vx = -squares[i].vx;
      squares[i].x = constrain(squares[i].x, 0, PANEL_RES_X - squares[i].size);
    }
    if (squares[i].y <= 0 || squares[i].y >= PANEL_RES_Y - squares[i].size) {
      squares[i].vy = -squares[i].vy;
      squares[i].y = constrain(squares[i].y, 0, PANEL_RES_Y - squares[i].size);
    }
    
    // Отрисовка квадрата на LED-матрице
    dma_display->fillRect((int)squares[i].x, (int)squares[i].y, squares[i].size, squares[i].size, squares[i].color);
  }
}


/*
Функция для вывода причины пробуждения ESP32 из режима сна.
Позволяет понять, что вызвало выход из deep sleep (таймер, кнопка, сенсор и т.д.)
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Пробуждение вызвано внешним сигналом через RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Пробуждение вызвано внешним сигналом через RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Пробуждение вызвано таймером"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Пробуждение вызвано сенсорной панелью"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Пробуждение вызвано программой ULP"); break;
    default : Serial.printf("Пробуждение не связано с deep sleep: %d\n",wakeup_reason); break;
  }
}


// Функция получения текущего времени в формате epoch (количество секунд с 1970 года)
unsigned long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

// Проверка корректности даты и времени, полученных с RTC
bool isRTCTimeValid(const I2C_BM8563_DateTypeDef &date, const I2C_BM8563_TimeTypeDef &time) {
  // Check reasonable year range (2020-2099)
  if (date.year < 2020 || date.year > 2099) {
    return false;
  }
  // Check month range
  if (date.month < 1 || date.month > 12) {
    return false;
  }
  // Check day range
  if (date.date < 1 || date.date > 31) {
    return false;
  }
  // Check hour range
  if (time.hours > 23) {
    return false;
  }
  // Check minute/second range
  if (time.minutes > 59 || time.seconds > 59) {
    return false;
  }
  return true;
}

// Получение времени с внешних часов RTC, если внутренние недоступны
bool getRTCTime(struct tm* timeinfo) {
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;
  
  // Получение даты и времени с RTC (функции возвращают void, результат не проверяется)
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);
  
  if (!isRTCTimeValid(rtcDate, rtcTime)) {
    return false;
  }
  
  timeinfo->tm_year = rtcDate.year - 1900;
  timeinfo->tm_mon = rtcDate.month - 1;
  timeinfo->tm_mday = rtcDate.date;
  timeinfo->tm_hour = rtcTime.hours;
  timeinfo->tm_min = rtcTime.minutes;
  timeinfo->tm_sec = rtcTime.seconds;
  timeinfo->tm_wday = rtcDate.weekDay;
  timeinfo->tm_isdst = -1;
  
  return true;
}

// Получение времени с приоритетом внутреннего RTC, если недоступно — с внешнего RTC
bool getTimeWithFallback(struct tm* timeinfo) {
  // Сначала попытка получить время с внутренних часов ESP32
  if (getLocalTime(timeinfo)) {
    return true;
  }
  
  // Если не удалось — попытка получить время с внешних часов RTC
  Serial.println("ESP32 RTC failed, trying external RTC...");
  return getRTCTime(timeinfo);
}

// Объявления функций для обновления отображения времени и анимаций
void updateClockWithAnimation();
void updateClockOnly();
void updateClockOverlay();
void initBouncingSquares();
void updateBouncingSquares();

//
// Основная функция setup() — инициализация всех компонентов устройства
//
void setup() {

  // Инициализация последовательного порта для вывода отладочной информации
  // Если определён ARDUINO_USB_CDC_ON_BOOT, вывод будет через USB
  Serial.begin(115200);
    // Проверочный код: выводим сообщение о начале проверки кнопки
    Serial.println("Проверка кнопки: каждую секунду будет выводиться состояние пина");

  /*-------------------- ИНИЦИАЛИЗАЦИЯ LED-МАТРИЦЫ HUB75E --------------------*/
  // Настройка параметров LED-матрицы, запуск DMA, установка яркости и тестовые цвета
    
    // Конфигурация модуля LED-матрицы: размеры, цепочка, пины
    HUB75_I2S_CFG mxconfig(
      PANEL_RES_X,   // module width
      PANEL_RES_Y,   // module height
      PANEL_CHAIN,   // Chain length
      _pins_x1       // pin mapping for port X1
    );
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;  
    mxconfig.latch_blanking = 4;
    //mxconfig.clkphase = false;
    //mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    //mxconfig.double_buff = false;  
    //mxconfig.min_refresh_rate = 30;


    // Инициализация объекта матрицы, запуск, установка яркости, очистка экрана
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(50); //0-255
    dma_display->clearScreen();

    dma_display->fillScreenRGB888(255,0,0);
    delay(1000);
    dma_display->fillScreenRGB888(0,255,0);
    delay(1000);    
    dma_display->fillScreenRGB888(0,0,255);
    delay(1000);       
    dma_display->clearScreen();
    dma_display->print("Connect to WiFi...");     


  /*-------------------- ИНИЦИАЛИЗАЦИЯ СЕТИ --------------------*/
  // Подключение к WiFi, ожидание соединения, вывод статуса на экран
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(wifi_ssid, wifi_pass); // Добавление WiFi-сети для подключения (можно задать в *-config.h)

  // Ожидание подключения к WiFi
  Serial.print("Waiting for WiFi to connect...");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println(" connected");
  dma_display->print("Connected!");
    

  /*-------------------- --------------- --------------------*/
  // Увеличение счётчика перезапусков и вывод его значения при каждом запуске
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Вывод причины пробуждения ESP32 из режима сна
  print_wakeup_reason();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();    

  if ( wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
  {
    dma_display->setCursor(3,6);
    dma_display->print("Wake up!");
    delay(1000);
  }
  else
  {
    dma_display->print("Starting.");

  }
  
  /*
    Настройка ESP32 на пробуждение по внешнему сигналу (например, кнопка).
    Для ESP32 доступны два типа пробуждения: ext0 и ext1.
  */
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PUSH_BUTTON_PIN, 0); //1 = High, 0 = Low  

  /*-------------------- --------------- --------------------*/
  // НАСТРОЙКА КНОПКИ
  button.attach( PUSH_BUTTON_PIN, INPUT ); // Используется внешний подтягивающий резистор
  button.interval(5);   // Интервал для подавления дребезга контактов (мс)
  button.setPressedState(LOW); // LOW соответствует нажатию кнопки


  /*-------------------- НАСТРОЙКА PWM-КОНТРОЛЛЕРА LEDC --------------------*/
  // Конфигурация таймера и канала для управления яркостью светодиода
    // Подготовка и применение конфигурации таймера PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT ,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 4000,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Подготовка и применение конфигурации канала PWM для светодиода
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = RUN_LED_PIN,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));  


    // Запуск задачи плавного изменения яркости светодиода (эффект дыхания)
    xTaskCreatePinnedToCore(
      ledFadeTask,            /* Task function. */
      "ledFadeTask",                 /* name of task. */
      1000,                    /* Stack size of task */
      NULL,                     /* parameter of the task */
      1,                        /* priority of the task */
      &Task1,                   /* Task handle to keep track of created task */
      0);                       /* Core */   
    


 
  /*-------------------- --------------- --------------------*/
  // Инициализация I2C для работы с RTC BM8563
  Wire1.begin(BM8563_I2C_SDA, BM8563_I2C_SCL);
  rtc.begin();

  // Получение даты и времени с RTC
  I2C_BM8563_DateTypeDef rtcDate;
  I2C_BM8563_TimeTypeDef rtcTime;
  
  // Чтение даты и времени с RTC (функции возвращают void, поэтому результат не проверяется)
  rtc.getDate(&rtcDate);
  rtc.getTime(&rtcTime);
  
  // Проверка корректности полученных данных с RTC
  bool rtcDataValid = isRTCTimeValid(rtcDate, rtcTime);
  
  if (rtcDataValid) {
    Serial.printf("RTC Date: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  rtcDate.year, rtcDate.month, rtcDate.date,
                  rtcTime.hours, rtcTime.minutes, rtcTime.seconds);
  } else {
    Serial.println("RTC data is invalid or corrupted");
  }
  
    time_t ntp_last_update_ts = 0;

  // Current RTC time (include time components for better comparison)
  time_t curr_rtc_ts = 0;
  bool needNTPUpdate = true;
  
  if (rtcDataValid) {
    struct tm curr_rtc_tm = {};
    curr_rtc_tm.tm_year = rtcDate.year - 1900;  // Год (от 1900)
    curr_rtc_tm.tm_mon = rtcDate.month - 1;     // Месяц (от 0)
    curr_rtc_tm.tm_mday = rtcDate.date;         // День месяца (1-31)
    curr_rtc_tm.tm_hour = rtcTime.hours;        // Часы (0-23)
    curr_rtc_tm.tm_min = rtcTime.minutes;       // Минуты (0-59)
    curr_rtc_tm.tm_sec = rtcTime.seconds;       // Секунды (0-59)
    curr_rtc_tm.tm_isdst = -1;                  // Флаг летнего времени
    
    curr_rtc_ts = mktime(&curr_rtc_tm);
    
    // Проверка необходимости обновления времени через NTP (если прошло больше 7 дней или не было синхронизации)
    if (ntp_last_update_ts > 0) {
      long timeDiff = abs((long int)(curr_rtc_ts - ntp_last_update_ts));
      needNTPUpdate = (timeDiff > (60*60*24*7)); // Если разница больше 7 дней — требуется синхронизация
      Serial.printf("Разница во времени: %ld секунд (%ld дней)\n", timeDiff, timeDiff/(60*60*24));
    }
  }

  if (!rtcDataValid || needNTPUpdate)
  {
      Serial.println("Выполняется синхронизация времени через NTP...");
      ESP_LOGI("ntp_update", "Обновление времени с NTP-сервера");    
  
      // Установка времени через NTP (Network Time Protocol)
      configTime(CLOCK_GMT_OFFSET * 3600, 0, ntpServer);

      // Ожидание синхронизации времени через NTP с таймаутом
      int ntpRetries = 0;
      struct tm timeInfo;
      while (!getLocalTime(&timeInfo) && ntpRetries < 10) {
        delay(1000);
        ntpRetries++;
        Serial.print(".");
      }
      
      if (getLocalTime(&timeInfo)) {
        Serial.println("\nNTP sync successful");
        
        // Установка времени RTC (функция возвращает void, результат не проверяется)
        I2C_BM8563_TimeTypeDef timeStruct;
        timeStruct.hours   = timeInfo.tm_hour;
        timeStruct.minutes = timeInfo.tm_min;
        timeStruct.seconds = timeInfo.tm_sec;
        
        rtc.setTime(&timeStruct);
        Serial.println("RTC time set successfully");

        // Установка даты RTC (функция возвращает void, результат не проверяется)
        I2C_BM8563_DateTypeDef dateStruct;
        dateStruct.weekDay = timeInfo.tm_wday;
        dateStruct.month   = timeInfo.tm_mon + 1;
        dateStruct.date    = timeInfo.tm_mday;
        dateStruct.year    = timeInfo.tm_year + 1900;
        
        rtc.setDate(&dateStruct);
        Serial.printf("RTC updated to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      dateStruct.year, dateStruct.month, dateStruct.date,
                      timeStruct.hours, timeStruct.minutes, timeStruct.seconds);
        
        // Обновление локальных переменных с новым временем
        rtcDate = dateStruct;
        rtcTime = timeStruct;
        rtcDataValid = true;
      } else {
        Serial.println("\nСинхронизация NTP не удалась — будет использовано текущее время RTC, если оно корректно");
      }

      // Сохранение времени последней синхронизации NTP
      ntp_last_update_ts = getEpochTime();
      // if (ntp_last_update_ts > 0) {
      //   // File writing to NTP timestamp removed
      // }

  }
  
  // Обновление внутренних часов ESP32, если есть корректные данные с внешнего RTC
  if (rtcDataValid) {
    esp32rtc.setTime(rtcTime.seconds, rtcTime.minutes, rtcTime.hours, 
                     rtcDate.date, rtcDate.month, rtcDate.year);
    Serial.println("Внутренние часы ESP32 обновлены из внешнего RTC:");    
    Serial.println(esp32rtc.getTime("%A, %B %d %Y %H:%M:%S"));
  } else {
    Serial.println("Внимание: Нет корректного источника времени!");
  }

  /*-------------------- НАСТРОЙКА ВЕБ-СЕРВЕРА --------------------*/
  // Запуск HTTP-сервера для OTA-обновлений и других функций

    webServer.on("/", []() {
      webServer.sendHeader("Content-Type", "text/html; charset=utf-8");
      webServer.send(200, "text/html; charset=utf-8", "<h1>HD-WF2 LED Matrix</h1><p><a href='/mode?next=1'>Переключить режим</a></p><p>Текущий режим: " + String(currentDisplayMode) + "</p>");
    });
    // Новый обработчик для смены режима через веб
    webServer.on("/mode", []() {
      if (webServer.hasArg("next")) {
        currentDisplayMode = (DisplayMode)((currentDisplayMode + 1) % MODE_COUNT);
        dma_display->clearScreen();
        if (currentDisplayMode == MODE_BOUNCING_SQUARES) {
          initBouncingSquares();
        }
      }
      webServer.sendHeader("Content-Type", "text/html; charset=utf-8");
      webServer.send(200, "text/html; charset=utf-8", "<h1>Режим переключён!</h1><p><a href='/'>Назад</a></p><p>Текущий режим: " + String(currentDisplayMode) + "</p>");
    });

    //ElegantOTA.begin(&webServer);    // Запуск OTA-обновления через веб-интерфейс
    webServer.begin();
    Serial.println("OTA HTTP server started");

    /*-------------------- ВЫВОД IP-АДРЕСА УСТРОЙСТВА --------------------*/
    Serial.print("IP-адрес: ");
    Serial.println(WiFi.localIP());  

    delay(1000);
    
    dma_display->clearScreen();
    dma_display->setCursor(0,0);

    dma_display->print(WiFi.localIP());
    dma_display->clearScreen();
    delay(3000);

    // Инициализация анимации прыгающих квадратов для режима отображения
    initBouncingSquares();

}

unsigned long last_update = 0;
char buffer[64];
void loop() 
{
    // Управление режимами теперь через веб-интерфейс

    webServer.handleClient();
    delay(1);

    // Обновление отображения на экране в зависимости от выбранного режима
    switch (currentDisplayMode) {
        case MODE_CLOCK_WITH_ANIMATION:
            updateClockWithAnimation();
            break;
            
        case MODE_CLOCK_ONLY:
            updateClockOnly();
            break;
            
        case MODE_BOUNCING_SQUARES:
            updateBouncingSquares();
            updateClockOverlay(); // В режиме прыгающих квадратов поверх анимации отображается текущее время
            break;
    }
}

// Функция обновления часов с анимированным фоном (основной режим)
void updateClockWithAnimation() {
    // Обновление отображения времени каждую секунду
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

            Serial.println("Performing screen time update...");
            dma_display->fillRect(0, 9, PANEL_RES_X, 9, 0x00); // Очистка области экрана, где отображается время
            dma_display->setCursor(8, 10);
            dma_display->print(buffer);
        } else {
            Serial.println("Failed to get time from all sources.");
            // Display error indicator
            dma_display->fillRect(0, 9, PANEL_RES_X, 9, 0x00);
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 0, 0)); // Красный цвет для отображения ошибки
            dma_display->print("NO TIME");
        }
        last_update = millis();
    }

    // Основной цикл отрисовки анимированного фона
    float t = (float)((millis() % 4000) / 4000.f);
    float tt = (float)((millis() % 16000) / 16000.f);

    for (int x = 0; x < (PANEL_RES_X * PANEL_CHAIN); x++) {
        // Расчёт общей яркости пикселя
        float f = (((sin(tt - (float)x / PANEL_RES_Y / 32.) * 2.f * PI) + 1) / 2) * 255;
        // Преобразование оттенка в RGB-цвет
        float r = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 0.f) / 3.f)) + 0.5f, 1.f), 0.f);
        float g = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 1.f) / 3.f)) + 0.5f, 1.f), 0.f);
        float b = max(min(cosf(2.f * PI * (t + ((float)x / PANEL_RES_Y + 2.f) / 3.f)) + 0.5f, 1.f), 0.f);

        // Перебор всех пикселей по строкам
        for (int y = 0; y < PANEL_RES_Y; y++) {
            // Оставить область для отображения часов пустой (не закрашивать)
            if (y > 8 && y < 18) {
                continue; // leave a black bar for the time, don't touch, this part of display is updated by the code in the clock update bit above
            }

            if (y * 2 < PANEL_RES_Y) {
                // Верхняя часть экрана — плавный переход яркости
                float t = (2.f * y + 1) / PANEL_RES_Y;
                dma_display->drawPixelRGB888(x, y,
                    (r * t) * f,
                    (g * t) * f,
                    (b * t) * f);
            } else {
                // Средняя и нижняя часть экрана — плавный переход насыщенности
                float t = (2.f * (PANEL_RES_Y - y) - 1) / PANEL_RES_Y;
                dma_display->drawPixelRGB888(x, y,
                    (r * t + 1 - t) * f,
                    (g * t + 1 - t) * f,
                    (b * t + 1 - t) * f);
            }
        }
    }
}

// Режим отображения только часов (без анимационного фона)
void updateClockOnly() {
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            dma_display->clearScreen();
            
            // Отображение времени
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            dma_display->setCursor(8, 4);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);
            
            // Отображение даты
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            dma_display->setCursor(2, 16);
            dma_display->setTextColor(dma_display->color565(200, 200, 200));
            dma_display->print(buffer);
            
            // Отображение дня недели
            const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            dma_display->setCursor(2, 26);
            dma_display->setTextColor(dma_display->color565(150, 150, 255));
            dma_display->print(days[timeinfo.tm_wday]);

            Serial.println("Clock-only mode update");
        } else {
            Serial.println("Failed to get time from all sources.");
            dma_display->clearScreen();
            dma_display->setCursor(8, 10);
            dma_display->setTextColor(dma_display->color565(255, 0, 0));
            dma_display->print("NO TIME");
        }
        last_update = millis();
    }
}

// Обновление отображения времени поверх анимации прыгающих квадратов
void updateClockOverlay() {
    if ((millis() - last_update) > 1000) {
        struct tm timeinfo;
        if (getTimeWithFallback(&timeinfo)) {
            memset(buffer, 0, 64);
            snprintf(buffer, 64, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            
            // Отрисовка времени на чёрном фоне для лучшей читаемости
            dma_display->fillRect(18, 12, 28, 8, 0x0000);
            dma_display->setCursor(20, 14);
            dma_display->setTextColor(dma_display->color565(255, 255, 255));
            dma_display->print(buffer);

            Serial.println("Bouncing squares with clock overlay update");
        } else {
            Serial.println("Failed to get time from all sources.");
            // Отображение ошибки поверх анимации
            dma_display->fillRect(18, 12, 28, 8, 0x0000);
            dma_display->setCursor(20, 14);
            dma_display->setTextColor(dma_display->color565(255, 0, 0));
            dma_display->print("ERR");
        }
        last_update = millis();
    }
} 