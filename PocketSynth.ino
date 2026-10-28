/*
  ESP32 PocketSynth - standalone phone-controlled passive buzzer
  Hardware:
    Passive buzzer: GPIO 23
    Optional buttons: LEFT GPIO 18, RIGHT GPIO 19 (reserved for future modes)
    SSD1306 OLED: SDA GPIO 21, SCL GPIO 22 (reserved / status display optional)

  Phone control:
    Android Chrome Web Bluetooth app writes short ASCII commands:
      N:<frequency>  start a note, e.g. N:440
      O              stop note immediately
      REC            start recording
      STOPREC        stop recording
      PLAYREC        play recorded notes
      CLEARREC       clear recorded melody
      STATUS         request status notification

  BLE UUIDs are custom. No Wi-Fi or internet is needed for the ESP32.
  The web interface itself needs to be opened in a secure context (HTTPS)
  for Web Bluetooth; see README.
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const uint8_t BUZZER_PIN = 23;
static const uint8_t LEFT_BUTTON_PIN = 18;
static const uint8_t RIGHT_BUTTON_PIN = 19;
static const uint8_t OLED_SDA_PIN = 21;
static const uint8_t OLED_SCL_PIN = 22;

static const char *DEVICE_NAME = "PocketSynth";
static const char *SERVICE_UUID = "e6b10000-7d2a-4b6a-9a5e-7c0a4d1f0001";
static const char *COMMAND_UUID = "e6b10001-7d2a-4b6a-9a5e-7c0a4d1f0001";
static const char *STATUS_UUID  = "e6b10002-7d2a-4b6a-9a5e-7c0a4d1f0001";

BLECharacteristic *statusCharacteristic = nullptr;
volatile bool clientConnected = false;

struct NoteEvent {
  uint32_t atMs;
  uint16_t frequency; // 0 means note-off
};

static const uint16_t MAX_EVENTS = 160;
NoteEvent recorded[MAX_EVENTS];
uint16_t eventCount = 0;
bool isRecording = false;
bool isPlayingRecording = false;
uint32_t recordStartedAt = 0;
uint16_t playbackIndex = 0;
uint32_t playbackStartedAt = 0;
uint32_t playbackLastEventAt = 0;

uint16_t currentFrequency = 0;
uint32_t lastNoteOnAt = 0;
static const uint32_t MAX_CONTINUOUS_NOTE_MS = 15000;

void stopToneNow() {
  noTone(BUZZER_PIN);
  currentFrequency = 0;
}

void startTone(uint16_t frequency) {
  if (frequency < 40 || frequency > 4000) {
    stopToneNow();
    return;
  }
  tone(BUZZER_PIN, frequency);
  currentFrequency = frequency;
  lastNoteOnAt = millis();
}

void addRecordEvent(uint16_t frequency) {
  if (!isRecording || eventCount >= MAX_EVENTS) return;
  recorded[eventCount].atMs = millis() - recordStartedAt;
  recorded[eventCount].frequency = frequency;
  eventCount++;
  if (eventCount >= MAX_EVENTS) isRecording = false;
}

void notifyStatus(const String &message) {
  if (statusCharacteristic && clientConnected) {
    statusCharacteristic->setValue(message.c_str());
    statusCharacteristic->notify();
  }
}

void stopRecording() {
  if (!isRecording) return;
  addRecordEvent(0);
  isRecording = false;
  notifyStatus(String("REC_STOPPED:") + eventCount);
}

void startRecording() {
  stopToneNow();
  isPlayingRecording = false;
  eventCount = 0;
  recordStartedAt = millis();
  isRecording = true;
  notifyStatus("REC_STARTED");
}

void clearRecording() {
  isRecording = false;
  isPlayingRecording = false;
  eventCount = 0;
  stopToneNow();
  notifyStatus("REC_CLEARED");
}

void startPlayback() {
  if (isRecording) stopRecording();
  if (eventCount == 0) {
    notifyStatus("ERR:NO_RECORDING");
    return;
  }
  stopToneNow();
  isPlayingRecording = true;
  playbackIndex = 0;
  playbackStartedAt = millis();
  playbackLastEventAt = 0;
  notifyStatus(String("PLAYING:") + eventCount);
}

void updatePlayback() {
  if (!isPlayingRecording) return;
  uint32_t elapsed = millis() - playbackStartedAt;
  while (playbackIndex < eventCount && recorded[playbackIndex].atMs <= elapsed) {
    uint16_t f = recorded[playbackIndex].frequency;
    if (f == 0) stopToneNow();
    else startTone(f);
    playbackLastEventAt = recorded[playbackIndex].atMs;
    playbackIndex++;
  }
  if (playbackIndex >= eventCount) {
    isPlayingRecording = false;
    stopToneNow();
    notifyStatus("PLAYBACK_DONE");
  }
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  if (command.startsWith("N:")) {
    long f = command.substring(2).toInt();
    if (f >= 40 && f <= 4000) {
      isPlayingRecording = false;
      startTone((uint16_t)f);
      addRecordEvent((uint16_t)f);
      notifyStatus(String("NOTE:") + f);
    }
    return;
  }

  if (command == "O") {
    stopToneNow();
    addRecordEvent(0);
    return;
  }
  if (command == "REC") { startRecording(); return; }
  if (command == "STOPREC") { stopRecording(); return; }
  if (command == "PLAYREC") { startPlayback(); return; }
  if (command == "CLEARREC") { clearRecording(); return; }
  if (command == "STATUS") {
    notifyStatus(String("STATUS:events=") + eventCount +
                 ",recording=" + (isRecording ? "1" : "0") +
                 ",playing=" + (isPlayingRecording ? "1" : "0"));
    return;
  }
  notifyStatus("ERR:UNKNOWN_COMMAND");
}

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    std::string raw = characteristic->getValue();
    if (!raw.empty()) {
      // BLE writes are short, so process them immediately for low latency.
      handleCommand(String(raw.c_str()));
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    clientConnected = true;
    notifyStatus("CONNECTED");
  }
  void onDisconnect(BLEServer *server) override {
    clientConnected = false;
    stopToneNow();            // fail-safe: never leave buzzer sounding after disconnect
    isPlayingRecording = false;
    isRecording = false;
    BLEDevice::startAdvertising();
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LEFT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON_PIN, INPUT_PULLUP);
  stopToneNow();

  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  BLECharacteristic *commandCharacteristic = service->createCharacteristic(
    COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  commandCharacteristic->setCallbacks(new CommandCallbacks());

  statusCharacteristic = service->createCharacteristic(
    STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  statusCharacteristic->addDescriptor(new BLE2902());
  statusCharacteristic->setValue("READY");

  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("PocketSynth ready. Pair/connect from the phone web interface.");
}

void loop() {
  updatePlayback();

  // Safety cutoff if a phone/browser loses its release event unexpectedly.
  if (currentFrequency != 0 && !isPlayingRecording &&
      millis() - lastNoteOnAt > MAX_CONTINUOUS_NOTE_MS) {
    stopToneNow();
    addRecordEvent(0);
    notifyStatus("SAFETY_NOTE_OFF");
  }

  // OLED pins and buttons are reserved for optional local controls; no OLED library
  // is required for this starter firmware.
  delay(1);
}
