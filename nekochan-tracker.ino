#include <WiFi.h>
#include <M5Atom.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HTTPClient.h>

#define WIFI_SSID "<WIFI-SSID>"
#define WIFI_PASSWORD "<WIFI-PASSWORD>"
#define BLE_SCAN_TIME_SEC 60

#define CAT_DEVICE_ADDRESS "<CAT-DEVICE-ADDRESS>"

#define RSSI_STACK_DATA_CNT 10
#define SEND_RSSI_DATA_INTERVAL_MIN 5

#define JST 3600* 9

const String apiServer = "<API-SERVER>";
const portTickType xBlockTime = 10000 / portTICK_RATE_MS;

BLEScan* pBLEScan;
HTTPClient http;
String macAddress;
SemaphoreHandle_t xSemaphore = NULL;
int rssiStackData[RSSI_STACK_DATA_CNT] = {0};

class CatAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.getAddress().toString() == CAT_DEVICE_ADDRESS && advertisedDevice.haveRSSI()) {
        // Serial.printf("[Founded Cat Device!] Advertised Device: %s RSSI: %d\n", advertisedDevice.getAddress().toString(), advertisedDevice.getRSSI());
        if (pdTRUE == xSemaphoreTake(xSemaphore, xBlockTime)) {
          appendRssiData(advertisedDevice.getRSSI());
          xSemaphoreGive(xSemaphore);
        }
      }

      delay(1);
    }

    void appendRssiData(int rssi) {
      bool isAddSucceed = false;
      for (int i = 0; i < RSSI_STACK_DATA_CNT; i ++) {
        if (rssiStackData[i] == 0) {
          rssiStackData[i] = rssi;
          isAddSucceed = true;
          break;
        }
      }

      if (!isAddSucceed) {
        rssiStackData[RSSI_STACK_DATA_CNT - 1] = rssi;
      }
    }
};

void notifyBootStatus() {
  Serial.println("[Notify] Notify boot status...");

  if (!http.begin(apiServer + "/notify/boot?macAddress=" + macAddress)) {
    Serial.println("[Notify] Failed to begin request");
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int statusCode = http.POST("");

  Serial.printf("[Notify] StatusCode: %d\n", statusCode);

  http.end();
}

void sendRssiData(String macAddress, String rssiArrayText) {
  Serial.println("[Send RSSI] Started...");

  String requestUrl = apiServer + "/beacon?macAddress=" + macAddress;

  if (rssiArrayText != "") {
    requestUrl = requestUrl + "&rssi=" + rssiArrayText;
  }

  if (!http.begin(requestUrl)) {
    Serial.println("[Send RSSI] Failed to begin request");
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int statusCode = http.POST("");

  Serial.printf("[Send RSSI] StatusCode: %d\n", statusCode);

  http.end();
}

void sendCatDeviceDataTask(void *pvParameters) {
  int lastMin = -1;
  while (1) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    if (lastMin != tm->tm_min) {
      lastMin = tm->tm_min;

      if (tm->tm_min % SEND_RSSI_DATA_INTERVAL_MIN == 0) {
        if (pdTRUE == xSemaphoreTake(xSemaphore, xBlockTime)) {
          String rssiArrayText = "";

          for (int i = 0; i < RSSI_STACK_DATA_CNT; i ++) {
            if (rssiStackData[i] != 0) {
              rssiArrayText = rssiArrayText + rssiStackData[i] + ",";
            }
          }

          sendRssiData(macAddress, rssiArrayText);
          memset(rssiStackData, 0, sizeof(rssiStackData));

          xSemaphoreGive(xSemaphore);
        }
      }
    }

    delay(1000);
  }
}


//-- Setup --//
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[System] Connecting to WiFi ..");

  // Waiting for connected Wi-Fi
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }

  Serial.println("Connected!");
  Serial.printf("[System] Local IP Address: %s\n", WiFi.localIP());
}

void setupBluetooth() {
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new CatAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false);
}

String fetchMACAddress() {
  byte rawMACAddress[6];
  char macAddress[17 + 1];
  WiFi.macAddress(rawMACAddress);
  sprintf(macAddress, "%x:%x:%x:%x:%x:%x", rawMACAddress[0], rawMACAddress[1], rawMACAddress[2], rawMACAddress[3], rawMACAddress[4], rawMACAddress[5]);
  Serial.printf("[System] MAC Adress: %s\n", macAddress);

  return macAddress;
}

void setupSendDataTask() {
  xSemaphore = xSemaphoreCreateMutex();
  if (xSemaphore != NULL) {
    xTaskCreate(sendCatDeviceDataTask, "sendCatDeviceDataTask", 8192, NULL, 1, (TaskHandle_t *) NULL);
  } else {
    Serial.println("[System] Failed to create semaphore object");
    ESP.restart();
  }
}

void setupNTP() {
  configTime(JST, 0, "ntp.nict.jp");
  // Waiting for sync time
  while (time(NULL) < 1000) {
    delay(100);
  }
}

void setup() {
  Serial.begin(115200);

  // おまじない
  // https://twitter.com/wakwak_koba/status/1553162622479974400
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);

  M5.begin(true, false, true);
  M5.dis.drawpix(0, 0xff0000);

  setupBluetooth();
  setupWiFi();
  macAddress = fetchMACAddress();
  setupNTP();
  setupSendDataTask();

  notifyBootStatus();
  M5.dis.drawpix(0, 0x000000);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[System] Wi-Fi disconnected. Trying to reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
  }

  pBLEScan->start(BLE_SCAN_TIME_SEC, true);

  delay(100);
}
