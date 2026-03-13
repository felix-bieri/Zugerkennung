#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_err.h>
#include <esp_wifi.h>

#include <string>
#include <sstream>
#include <iomanip>

#define LED 2

static const String TRAIN_NAME = "RhB Ge 6/6"; // change
static const String TRAIN_ID = "G6/6"; // change, must be exactly 4 characters
static const uint32_t BEACON_INTERVAL_MS = 500;

static const bool USE_FIXED_CHANNEL = true;
static const uint8_t FIXED_CHANNEL = 6;
static const wifi_power_t TX_POWER = WIFI_POWER_19_5dBm;

static uint8_t BROADCAST_ADDR[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

#pragma pack(push, 1)
struct BeaconPacket {
  uint32_t train_id;
  uint32_t seq;
  uint32_t uptime_ms;
};
#pragma pack(pop)

static uint32_t g_seq = 0;
static uint32_t g_lastSend = 0;

static void setFixedChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(TX_POWER);
  WiFi.setHostname(TRAIN_NAME.c_str());
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.disconnect();
  esp_err_t e = esp_wifi_start();
  if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_INIT && e != ESP_ERR_WIFI_CONN) {
    Serial.printf("esp_wifi_start: %d (%s)\n", (int)e, esp_err_to_name(e));
  }
  setFixedChannel(FIXED_CHANNEL);
  Serial.printf("Fixed channel: %u\n", (unsigned)FIXED_CHANNEL);
  // ESP-NOW init
  e = esp_now_init();
  if (e != ESP_OK) {
    Serial.printf("esp_now_init failed: %d (%s)\n", (int)e, esp_err_to_name(e));
    while (true) delay(1000);
  }
  // Broadcast Peer hinzufügen
  esp_now_peer_info_t peerInfo{};
  memcpy(peerInfo.peer_addr, BROADCAST_ADDR, 6);
  peerInfo.channel = FIXED_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx   = WIFI_IF_STA;  // <<< sehr wichtig
  e = esp_now_add_peer(&peerInfo);
  if (e != ESP_OK && e != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("esp_now_add_peer failed: %d (%s)\n", (int)e, esp_err_to_name(e));
    while (true) delay(1000);
  }
  Serial.println("ESP-NOW Beacon ready.");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  uint32_t now = millis();
  if (now - g_lastSend >= BEACON_INTERVAL_MS) {
    g_lastSend = now;

    BeaconPacket pkt{};
    pkt.train_id  = ((uint32_t)(uint8_t)TRAIN_ID[0] << 24) |
                    ((uint32_t)(uint8_t)TRAIN_ID[1] << 16) |
                    ((uint32_t)(uint8_t)TRAIN_ID[2] <<  8) |
                     (uint32_t)(uint8_t)TRAIN_ID[3];
    pkt.seq       = g_seq++;
    pkt.uptime_ms = now;

    esp_err_t res = esp_now_send(BROADCAST_ADDR, (uint8_t*)&pkt, sizeof(pkt));
    if (res == ESP_OK) {
      digitalWrite(LED, (digitalRead(LED) == LOW) ? HIGH : LOW);
      Serial.printf("Beacon sent: ID=%s Seq=%lu\n",
                    TRAIN_ID.c_str(),
                    (unsigned long)pkt.seq);
    } else {
      Serial.printf("Send error: %d (%s)\n", (int)res, esp_err_to_name(res));
      digitalWrite(LED, LOW);
    }
  }
}