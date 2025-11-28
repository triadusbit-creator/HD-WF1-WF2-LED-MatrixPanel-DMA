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
//#include <ElegantOTA.h>
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

// Управление задачами FreeRTOS (например, для работы с LED и другими функциями)
TaskHandle_t Task1;
TaskHandle_t Task2;


// Основная функция setup() — инициализация всех компонентов устройства
//
void setup() {

  Serial.begin(115200);
    
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
}

void loop() {
    dma_display->clearScreen();
    dma_display->setFont(&FreeSans9pt7b);
    dma_display->setTextSize(2);
    dma_display->setCursor(16, 32 + 9); // 9 - высота шрифта FreeSans9pt7b
    dma_display->setTextColor(dma_display->color565(0, 255, 0)); // Зеленый цвет
    dma_display->print("88.88");
    delay(10000);
}
