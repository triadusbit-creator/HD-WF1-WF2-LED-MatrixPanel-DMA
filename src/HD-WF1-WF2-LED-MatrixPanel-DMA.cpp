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
#include <I2C_BM8563.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ESP32Time.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

/*-------------------------- Настройка LED-матрицы HUB75E через DMA -----------------------------*/
#define PANEL_RES_X 128
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

HUB75_I2S_CFG::i2s_pins _pins_x1 = {WF2_X1_R1_PIN, WF2_X1_G1_PIN, WF2_X1_B1_PIN, WF2_X1_R2_PIN, WF2_X1_G2_PIN, WF2_X1_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X1_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};
HUB75_I2S_CFG::i2s_pins _pins_x2 = {WF2_X2_R1_PIN, WF2_X2_G1_PIN, WF2_X2_B1_PIN, WF2_X2_R2_PIN, WF2_X2_G2_PIN, WF2_X2_B2_PIN, WF2_A_PIN, WF2_B_PIN, WF2_C_PIN, WF2_D_PIN, WF2_X2_E_PIN, WF2_LAT_PIN, WF2_OE_PIN, WF2_CLK_PIN};


/*-------------------------- Экземпляры классов ------------------------------*/
MatrixPanel_I2S_DMA *dma_display = nullptr;
WebServer server(80); // Веб-сервер на порту 80

// Переменные для имени и пароля WiFi сети
String wifiSsid = "ZaLuPeN"; // Имя WiFi сети
String wifiPass = "14789632"; // Пароль WiFi сети

String panelDigits = "00.00"; // Текущие цифры для отображения

// Обработчик главной страницы
void handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><title>ESP32 WebServer</title></head><body>";
    html += "<h2>Устройство работает!</h2>";
    html += "<form method='POST' action='/set'>";
    html += "<label>Цифры для вывода:</label> ";
    html += "<input name='digits' maxlength='5' value='" + panelDigits + "'>";
    html += "<button type='submit'>Изменить</button></form>";
    html += "<p>Текущие цифры: <b>" + panelDigits + "</b></p>";
    html += "<p>WiFi: <b>" + wifiSsid + "</b></p>";
    html += "</body></html>";
    server.send(200, "text/html; charset=UTF-8", html);
}

void handleSet() {
    if (server.hasArg("digits")) {
        String digits = server.arg("digits");
        if (digits.length() == 5 && digits[2] == '.') {
            panelDigits = digits;
        }
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// Основная функция setup() — инициализация всех компонентов устройства
//
void setup() {
    Serial.begin(115200);
    Serial.println("Serial test: setup started");
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    unsigned long startAttemptTime = millis();
    const unsigned long wifiTimeout = 15000; // 15 секунд таймаут
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
    // Конфигурация модуля LED-матрицы: размеры, цепочка, пины
    HUB75_I2S_CFG mxconfig(
      PANEL_RES_X,   // module width
      PANEL_RES_Y,   // module height
      PANEL_CHAIN,   // Chain length
      _pins_x1       // pin mapping for port X1
    );
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;  
    mxconfig.latch_blanking = 1;
    mxconfig.clkphase = false;
    //mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    mxconfig.double_buff = false;  
    mxconfig.min_refresh_rate = 200;
    mxconfig.setPixelColorDepthBits(8); // 8 bits per color channel (RGB888)

    // Инициализация объекта матрицы, запуск, установка яркости, очистка экрана
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->begin();
    dma_display->setBrightness8(80); //0-255
    dma_display->clearScreen();

    dma_display->fillScreenRGB888(255,0,0);
    delay(500);
    dma_display->fillScreenRGB888(0,255,0);
    delay(500);    
    dma_display->fillScreenRGB888(0,0,255);
    delay(500);       
    dma_display->clearScreen();

    // Сервер запускается после инициализации матрицы
    server.on("/", handleRoot);
    server.on("/set", HTTP_POST, handleSet);
    server.begin();
}

void loop() {
    server.handleClient();
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    if (now - lastUpdate > 1000) { // обновлять экран раз в секунду
        lastUpdate = now;
        String displayDigits = panelDigits;
        // Проверка формата: если первый символ '0' и второй не '0', выводим без первого нуля
        if (displayDigits.length() == 5 && displayDigits[0] == '0' && displayDigits[1] != '0') {
            displayDigits = displayDigits.substring(1); // убираем первый ноль
        }
        dma_display->clearScreen();
        dma_display->setFont(&FreeSans9pt7b);
        dma_display->setTextSize(2);
        dma_display->setCursor(16, 32 + 9); // 9 - высота шрифта FreeSans9pt7b
        dma_display->setTextColor(dma_display->color565(0, 255, 0)); // Зеленый цвет
        dma_display->print(displayDigits);
    }
    delay(10); // минимальная задержка для стабильности
}
