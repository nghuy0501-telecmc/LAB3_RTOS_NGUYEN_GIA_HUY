/*
 * Lab RTOS - Giam sat moi truong vuon cay cong nghiep (ca phe, ho tieu)
 * ESP32 + DHT22 + MQTT + FreeRTOS (2 core)
 *
 *  Core 0: TaskTransmit  - WiFi/MQTT, doc dataQueue -> publish, nhan lenh tu Web
 *  Core 1: TaskRead      - doc DHT22, in Serial, gui dataQueue + alertQueue
 *  Core 1: TaskAlert     - doc alertQueue, dieu khien may phun suong (PWM)
 *
 * Thu vien can cai: "DHT sensor library", "Adafruit Unified Sensor", "PubSubClient"
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ===================== CAU HINH CHAN =====================
#define DHT_PIN   22
#define DHT_TYPE  DHT22
#define PUMP_PIN  17   // May bom   (LED do)
#define FAN_PIN   18   // Quat      (LED xanh la)
#define MIST_PIN  19   // May phun suong (LED xanh duong, dieu khien PWM)

// ===================== PWM CHO MAY PHUN SUONG =====================
#define PWM_FREQ  5000
#define PWM_RES   8          // 0..255
#define PWM_CH    0          // dung cho core 2.x

// ===================== WIFI / MQTT =====================
const char* ssid        = "Wokwi-GUEST";
const char* password    = "";
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

#define PREFIX "lab_nhungN01_231432253"
const char* topic_pub_temp = PREFIX "/DHT22/nhietdo";
const char* topic_pub_hum  = PREFIX "/DHT22/doam";
const char* topic_pub_mist = PREFIX "/DHT22/phunsuong";  // cong suat may phun (0/25/50/100)
const char* topic_sub_pump = PREFIX "/device/pump";
const char* topic_sub_fan  = PREFIX "/device/fan";

// ===================== NGUONG DO AM =====================
#define HUM_MIN 40.0f   // do am on dinh 40% - 60%

// ===================== CAU TRUC DU LIEU HANG DOI =====================
struct SensorData {      // "data Queue": du lieu day du
  float temperature;
  float humidity;
  int   mistPower;       // % cong suat may phun hien tai
};

QueueHandle_t dataQueue;    // SensorData
QueueHandle_t alertQueue;   // float (do am)
SemaphoreHandle_t serialMutex;   // bao ve Serial dung chung giua cac task

volatile int mistPowerNow = 0;   // TaskAlert ghi, TaskRead doc

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient espClient;
PubSubClient client(espClient);

// ===================== TIEN ICH =====================
void logPrint(const String &s) {
  if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    Serial.println(s);
    xSemaphoreGive(serialMutex);
  }
}

void setMist(int percent) {
  uint32_t duty = (uint32_t)percent * 255 / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MIST_PIN, duty);
#else
  ledcWrite(PWM_CH, duty);
#endif
  mistPowerNow = percent;
}

// ===================== CALLBACK MQTT (lenh manual tu Web) =====================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  String t = String(topic);
  logPrint("MQTT nhan [" + t + "] = " + msg);

  int level = (msg == "ON") ? HIGH : LOW;
  if (t == topic_sub_pump && (msg == "ON" || msg == "OFF")) {
    digitalWrite(PUMP_PIN, level);
    logPrint(level ? "-> BAT May Bom" : "-> TAT May Bom");
  } else if (t == topic_sub_fan && (msg == "ON" || msg == "OFF")) {
    digitalWrite(FAN_PIN, level);
    logPrint(level ? "-> BAT Quat" : "-> TAT Quat");
  }
}

bool mqttReconnect() {
  String clientId = "ESP32Client-" + String(random(0, 0xffff), HEX);
  if (client.connect(clientId.c_str())) {
    client.subscribe(topic_sub_pump);
    client.subscribe(topic_sub_fan);
    logPrint("MQTT ket noi thanh cong");
    return true;
  }
  logPrint("MQTT loi rc=" + String(client.state()));
  return false;
}

// ===================== TASK TRUYEN DATA (CORE 0) =====================
void TaskTransmit(void *pv) {
  // 1) Ket noi WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  logPrint("WiFi da ket noi. IP: " + WiFi.localIP().toString());

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  SensorData d;
  for (;;) {
    // 2) Giu ket noi WiFi + MQTT
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!client.connected()) {
      if (!mqttReconnect()) { vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
    }
    client.loop();

    // 3) Doc dataQueue (cho toi da 100 ms de van goi duoc client.loop())
    if (xQueueReceive(dataQueue, &d, pdMS_TO_TICKS(100)) == pdTRUE) {
      char buf[16];
      dtostrf(d.temperature, 1, 2, buf);  client.publish(topic_pub_temp, buf);
      dtostrf(d.humidity,    1, 2, buf);  client.publish(topic_pub_hum,  buf);
      snprintf(buf, sizeof(buf), "%d", d.mistPower);
      client.publish(topic_pub_mist, buf);
      logPrint("[Core0] Da gui MQTT: T=" + String(d.temperature, 1) +
               " H=" + String(d.humidity, 1) + " Phun=" + String(d.mistPower) + "%");
    }
  }
}

// ===================== TASK DOC DATA (CORE 1) =====================
void TaskRead(void *pv) {
  dht.begin();
  vTaskDelay(pdMS_TO_TICKS(2000));    // DHT22 can on dinh
  for (;;) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      logPrint("Loi doc cam bien DHT22!");
    } else {
      SensorData d = { t, h, mistPowerNow };
      logPrint("[Core1] Nhiet do: " + String(t, 1) + " C | Do am: " + String(h, 1) + " %");

      xQueueSend(dataQueue,  &d, 0);   // du lieu day du -> Task truyen
      xQueueSend(alertQueue, &h, 0);   // do am -> Task canh bao
    }
    vTaskDelay(pdMS_TO_TICKS(5000));   // chu ky doc 5 giay
  }
}

// ===================== TASK CANH BAO (CORE 1) =====================
// Thuat toan:  35-40% : 25% cong suat, 10 s
//              30-35% : 50% cong suat, 20 s
//              <30%   : 100% cong suat, 30 s
// Khong dung vTaskDelay dai de van nhan duoc du lieu moi: neu do am
// da ve >= 40% thi tat som, neu xuong muc nang hon thi nang cong suat.
void TaskAlert(void *pv) {
  float h;
  int  curPower = 0;
  uint32_t endMs = 0;

  for (;;) {
    if (xQueueReceive(alertQueue, &h, pdMS_TO_TICKS(200)) == pdTRUE) {
      int power = 0; uint32_t durMs = 0;
      if      (h < 30.0f)       { power = 100; durMs = 30000; }
      else if (h < 35.0f)       { power = 50;  durMs = 20000; }
      else if (h < HUM_MIN)     { power = 25;  durMs = 10000; }

      if (power == 0) {                       // do am da on dinh (>= 40%)
        if (curPower != 0) { setMist(0); curPower = 0; logPrint("[Alert] Do am on dinh -> TAT may phun"); }
      } else if (power > curPower || curPower == 0) {   // bat moi hoac nang muc
        curPower = power;
        endMs = millis() + durMs;
        setMist(curPower);
        logPrint("[Alert] Do am " + String(h, 1) + "% -> BAT may phun " +
                 String(curPower) + "% trong " + String(durMs / 1000) + "s");
      }
    }
    // Het thoi gian phun thi tat
    if (curPower != 0 && (int32_t)(millis() - endMs) >= 0) {
      setMist(0); curPower = 0;
      logPrint("[Alert] Het thoi gian phun -> TAT may phun");
    }
  }
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(MIST_PIN, PWM_FREQ, PWM_RES);
#else
  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(MIST_PIN, PWM_CH);
#endif
  setMist(0);

  serialMutex = xSemaphoreCreateMutex();
  dataQueue   = xQueueCreate(10, sizeof(SensorData));
  alertQueue  = xQueueCreate(10, sizeof(float));

  //                 ham          ten            stack  tham so  uu tien  handle  core
  xTaskCreatePinnedToCore(TaskTransmit, "TaskTransmit", 8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(TaskRead,     "TaskRead",     4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskAlert,    "TaskAlert",    4096, NULL, 3, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);   // loop khong dung, moi viec do cac Task dam nhiem
}
