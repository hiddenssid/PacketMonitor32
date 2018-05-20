/* uncomment if the default 4 bit mode doesn't work */
/* ------------------------------------------------ */
// #define BOARD_HAS_1BIT_SDMMC true // forces 1bit mode for SD MMC
/* ------------------------------------------------ */

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string>
#include <cstddef>
#include <Wire.h>
#include <Preferences.h>
using namespace std;

/* ===== compile settings ===== */
#define MAX_CH 11       // 1 - 14 channels (1-11 for US, 1-13 for EU and 1-14 for Japan)
#define SNAP_LEN 2324   // max len of each recieved packet

#define BUTTON_PIN 0    // button to change the channel


#define MAX_X 480
#define MAX_Y 320


#if CONFIG_FREERTOS_UNICORE
#define RUNNING_CORE 0
#else
#define RUNNING_CORE 1
#endif

#include "FS.h"
//#include "SD_MMC.h"
#include "Buffer.h"
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>

esp_err_t event_handler(void* ctx, system_event_t* event) {
  return ESP_OK;
}



/* ===== run-time variables ===== */
//Buffer sdBuffer;
Preferences preferences;
TFT_eSPI tft = TFT_eSPI();

//bool useSD = false;
bool buttonPressed = false;
bool buttonEnabled = true;
uint32_t lastDrawTime;
uint32_t lastButtonTime;
uint32_t tmpPacketCounter;
uint32_t pkts[MAX_X];       // here the packets per second will be saved
uint32_t deauths = 0;       // deauth frames per second
uint32_t auths = 0;
unsigned int ch = 1;        // current 802.11 channel
int rssiSum;

/* ===== functions ===== */
double getMultiplicator() {
  uint32_t maxVal = 1;
  for (int i = 0; i < MAX_X; i++) {
    if (pkts[i] > maxVal) maxVal = pkts[i];
  }
  if (maxVal > MAX_Y) return (double)MAX_Y / (double)maxVal;
  else return 1;
}

void setChannel(int newChannel) {
  ch = newChannel;
  if (ch > MAX_CH || ch < 1) ch = 1;

  preferences.begin("packetmonitor32", false);
  preferences.putUInt("channel", ch);
  preferences.end();

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);


}

/*bool setupSD() {
  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD_MMC card attached");
    return false;
  }

  Serial.print("SD_MMC Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);

  return true;
  }*/

void wifi_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

  if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0 )) deauths++;
  if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xB0))
  {
    auths++; Serial.println("Paquetes "+(String)pkts[MAX_X - 1]);
  }

  if (type == WIFI_PKT_MISC) return;             // wrong packet type
  if (ctrl.sig_len > SNAP_LEN) return;           // packet too long

  uint32_t packetLength = ctrl.sig_len;
  if (type == WIFI_PKT_MGMT) packetLength -= 4;  // fix for known bug in the IDF https://github.com/espressif/esp-idf/issues/886

  //Serial.print(".");
  tmpPacketCounter++;
  rssiSum += ctrl.rssi;

  //if (useSD) sdBuffer.addPacket(pkt->payload, packetLength);

}

void draw() {

  double multiplicator = getMultiplicator();
  int len;
  int rssi;

  if (pkts[MAX_X - 1] > 0) rssi = rssiSum / (int)pkts[MAX_X - 1];
  else rssi = rssiSum;

  tft.setCursor(0, 0);
  tft.print("Channel " + String(ch));
  tft.print((" | "));
  tft.print("Strength " + (String)rssi);
  tft.print((" | "));
  tft.println("Packets " + (String)tmpPacketCounter+"       ");
  tft.setCursor(0, 15);
  tft.print(("Deauths Packets ["));
  tft.print((String)deauths);
  tft.print(("]"));

  tft.print((" | "));
  tft.print(("New connections ["));
  tft.print((String)auths);
  tft.print(("]  "));
  // tft.print((useSD ? "SD" : ""));


  for (int i = 0; i < MAX_X; i++) {
    len = pkts[i] * multiplicator;
    if (deauths > 0)
    {
      tft.drawLine(i, 319, i, 319 - (len > MAX_Y - 35 ? MAX_Y - 35 : len), TFT_RED);
    }
    else if (auths > 0)
    {
      tft.drawLine(i, 319, i, 319 - (len > MAX_Y - 35 ? MAX_Y - 35 : len), TFT_BLUE);
     // authsCo++;
    }
    else
    {
      tft.drawLine(i, 319, i, 319 - (len > MAX_Y - 35 ? MAX_Y - 35 : len), TFT_WHITE);
    }


    tft.drawLine(i, 30 , i, 319 - (len > MAX_Y - 30 ? MAX_Y - 30 : len), TFT_BLACK);
    if (i < MAX_X - 1) pkts[i] = pkts[i + 1];
    tft.drawLine(0, 30, MAX_X, 30, TFT_WHITE);
  }



}

/* ===== main program ===== */
void setup() {

  // Serial
  Serial.begin(115200);


  //Screen
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);


  // Settings
  preferences.begin("packetmonitor32", false);
  ch = preferences.getUInt("channel", 1);
  preferences.end();

  // System & WiFi
  nvs_flash_init();
  tcpip_adapter_init();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  //ESP_ERROR_CHECK(esp_wifi_set_country(WIFI_COUNTRY_EU));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  //SD card
  // sdBuffer = Buffer();

  // if (setupSD())
  //   sdBuffer.open(&SD_MMC);

  // I/O
  pinMode(BUTTON_PIN, INPUT_PULLUP);


  // second core
  xTaskCreatePinnedToCore(
    coreTask,               /* Function to implement the task */
    "coreTask",             /* Name of the task */
    2500,                   /* Stack size in words */
    NULL,                   /* Task input parameter */
    0,                      /* Priority of the task */
    NULL,                   /* Task handle. */
    RUNNING_CORE);          /* Core where the task should run */

  // start Wifi sniffer
  esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
  esp_wifi_set_promiscuous(true);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

void coreTask( void * p ) {

  uint32_t currentTime;

  while (true) {

    currentTime = millis();

    /* bit of spaghetti code, have to clean this up later :D */

    // check button
    if (digitalRead(BUTTON_PIN) == LOW) {
      if (buttonEnabled) {
        if (!buttonPressed) {
          buttonPressed = true;
          lastButtonTime = currentTime;
        } else if (currentTime - lastButtonTime >= 2000) {
          /* if (useSD) {
             useSD = false;
             sdBuffer.close(&SD_MMC);
             draw();
            } else {
             if (setupSD())
               sdBuffer.open(&SD_MMC);*/
          draw();
          //  }
          buttonPressed = false;
          buttonEnabled = false;
        }
      }
    } else {
      if (buttonPressed) {
        setChannel(ch + 1);
        draw();
      }
      buttonPressed = false;
      buttonEnabled = true;
    }

    // save buffer to SD
    //   if (useSD)
    //    sdBuffer.save(&SD_MMC);

    // draw Display
    if ( currentTime - lastDrawTime > 1000 ) {
      lastDrawTime = currentTime;
      // Serial.printf("\nFree RAM %u %u\n", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_minimum_free_size(MALLOC_CAP_32BIT));// for debug purposes

      pkts[MAX_X - 1] = tmpPacketCounter;

      draw();

     // Serial.println((String)pkts[MAX_X - 1]);

      tmpPacketCounter = 0;
      deauths = 0;
      auths = 0;
      rssiSum = 0;
    }

    // Serial input
    if (Serial.available()) {
      ch = Serial.readString().toInt();
      if (ch < 1 || ch > 14) ch = 1;
      setChannel(ch);
    }

  }

}
