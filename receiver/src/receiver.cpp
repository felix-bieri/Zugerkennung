#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>

#define LED 2

static const uint8_t FIXED_CHANNEL     = 6;

static const uint32_t PRESENCE_TIMEOUT_MS = 2500;
static const int MAX_TRAINS = 20;

// RSSI-Schwellen
static const int RSSI_NEAR   = -70;  // stärker als -70 dBm => nah
static const int RSSI_MID    = -85;  // -70..-85 => mittel
// schwächer als -85 => weit

#pragma pack(push, 1)
struct BeaconPacket {
  uint32_t train_id;
  uint32_t seq;
  uint32_t uptime_ms;
};
#pragma pack(pop)

struct TrainState {
  bool     used = false;
  uint32_t train_id = 0;
  uint32_t last_seq = 0;
  uint32_t last_seen_ms = 0;
  int      last_rssi = -127;
};

static TrainState g_trains[MAX_TRAINS];

static void setFixedChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

// ---- Train slot finden/anlegen
static int findOrCreateTrain(uint32_t id) {
  for (int i = 0; i < MAX_TRAINS; i++) {
    if (g_trains[i].used && g_trains[i].train_id == id) return i;
  }
  for (int i = 0; i < MAX_TRAINS; i++) {
    if (!g_trains[i].used) {
      g_trains[i].used = true;
      g_trains[i].train_id = id;
      return i;
    }
  }
  return -1;
}

static volatile int g_last_rssi = -127;

static void IRAM_ATTR promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  g_last_rssi = ppkt->rx_ctrl.rssi;
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  (void)mac;
  if (len != (int)sizeof(BeaconPacket)) return;

  BeaconPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  int idx = findOrCreateTrain(pkt.train_id);
  if (idx < 0) {
    Serial.println("Train table full!");
    return;
  }

  g_trains[idx].last_seq = pkt.seq;
  g_trains[idx].last_seen_ms = millis();
  g_trains[idx].last_rssi = (int)g_last_rssi; // RSSI "näherungsweise"

  char train_id_str[5];
  train_id_str[0] = (char)((pkt.train_id >> 24) & 0xFF);
  train_id_str[1] = (char)((pkt.train_id >> 16) & 0xFF);
  train_id_str[2] = (char)((pkt.train_id >> 8) & 0xFF);
  train_id_str[3] = (char)(pkt.train_id & 0xFF);
  train_id_str[4] = '\0';

  Serial.printf("RX: id=%s seq=%lu rssi=%d uptime=%lums\n",
                train_id_str,
                (unsigned long)pkt.seq,
                g_trains[idx].last_rssi,
                (unsigned long)pkt.uptime_ms);
}

static const char* classifyDistance(int rssi) {
  if (rssi >= RSSI_NEAR) return "NEAR";
  if (rssi >= RSSI_MID)  return "MID";
  return "FAR";
}

static void presence() {
  uint32_t now = millis();
  Serial.println("---- Presence ----");

  bool any = false;
  bool anyPresent = false;

  for (int i = 0; i < MAX_TRAINS; i++) {
    if (!g_trains[i].used) continue;

    uint32_t age = now - g_trains[i].last_seen_ms;
    bool present = (age <= PRESENCE_TIMEOUT_MS);
    if (present) anyPresent = true;

    // Train-ID als ASCII-String interpretieren (4 Bytes)
    char train_id_str[5];
    train_id_str[0] = (char)((g_trains[i].train_id >> 24) & 0xFF);
    train_id_str[1] = (char)((g_trains[i].train_id >> 16) & 0xFF);
    train_id_str[2] = (char)((g_trains[i].train_id >> 8) & 0xFF);
    train_id_str[3] = (char)(g_trains[i].train_id & 0xFF);
    train_id_str[4] = '\0';
    
    Serial.printf("id=%s present=%s age=%lums last_seq=%lu rssi=%d dist=%s\n",
            train_id_str,
            present ? "YES" : "no",
            (unsigned long)age,
            (unsigned long)g_trains[i].last_seq,
            g_trains[i].last_rssi,
            classifyDistance(g_trains[i].last_rssi));

    any = true;
  }

  digitalWrite(LED, anyPresent ? HIGH : LOW);

  if (!any) Serial.println("(no trains tracked yet)");
}

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.disconnect();
  esp_wifi_start();

  setFixedChannel(FIXED_CHANNEL);
  Serial.printf("Fixed channel: %u\n", (unsigned)FIXED_CHANNEL);

  // Promiscuous an (RSSI lesen)
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&promisc_cb);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP-NOW Receiver ready.");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  static uint32_t lastPrint = 0;
  uint32_t now = millis();

  if (now - lastPrint >= 1000) {
    lastPrint = now;
    presence();
  }

  delay(10);
}