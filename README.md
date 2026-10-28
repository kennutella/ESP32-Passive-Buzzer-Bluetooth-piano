# ESP32 PocketSynth

A standalone ESP32 project for controlling a passive buzzer from an Android phone over Bluetooth Low Energy (BLE). This project does **not** modify Kermet Console.

## Hardware

Reuse the same layout where appropriate:

| Part | GPIO | Notes |
|---|---:|---|
| Passive buzzer signal | 23 | Existing buzzer signal pin |
| Optional left button | 18 | Reserved for future on-device controls; INPUT_PULLUP |
| Optional right button | 19 | Reserved for future on-device controls; INPUT_PULLUP |
| SSD1306 SDA | 21 | Reserved for adding OLED status UI |
| SSD1306 SCL | 22 | Reserved for adding OLED status UI |
| Buzzer ground | GND | Common ground |

Keep the buzzer wiring and any existing resistor/transistor stage you already use. Do not connect a speaker that draws high current directly to an ESP32 GPIO. A passive piezo buzzer is suitable for this starter design.

## Firmware setup

1. Install Arduino IDE and ESP32 board support.
2. Select the correct ESP32 board and port.
3. Open `firmware/PocketSynth.ino`.
4. The sketch uses the ESP32 Arduino BLE API (`BLEDevice.h`, `BLEServer.h`, etc.) included with many ESP32 board-package versions. If your setup says these headers are missing, update/install the Espressif ESP32 board package. Avoid installing an unrelated BLE library unless needed.
5. Compile and upload.
6. Open Serial Monitor at 115200 baud. It should say `PocketSynth ready`.
7. Leave the ESP32 powered and open the phone interface.

## Phone interface (Android)

The web interface is `phone_web/index.html`. It uses Web Bluetooth, supported by Chrome on Android in secure contexts. A local `file://` page usually cannot use Web Bluetooth. Host the file over HTTPS (for example, from a GitHub Pages site) or use a local HTTPS development server that your phone can access. Do not enter any credentials into third-party hosting pages.

1. Open the hosted HTTPS page in Chrome on Android.
2. Tap **Connect ESP32**.
3. Select `PocketSynth` in the Bluetooth picker.
4. Tap and hold piano keys to sustain notes; release to send note-off.
5. Try recording, playback, and the note-guessing game.

The phone interface requires a browser with Web Bluetooth. The ESP32 connection itself does not require Wi-Fi or internet; HTTPS is a browser security requirement for the web page.

## BLE protocol

Service UUID: `e6b10000-7d2a-4b6a-9a5e-7c0a4d1f0001`

Command characteristic UUID: `e6b10001-7d2a-4b6a-9a5e-7c0a4d1f0001`

Status characteristic UUID: `e6b10002-7d2a-4b6a-9a5e-7c0a4d1f0001`

Write short ASCII commands:

- `N:440` — start A4 (440 Hz)
- `O` — stop the current tone immediately
- `REC` — clear and start a new recording
- `STOPREC` — stop recording
- `PLAYREC` — play the recorded sequence
- `CLEARREC` — erase the sequence
- `STATUS` — send status

## Safety and known limits

- On BLE disconnect, firmware stops the buzzer and stops recording/playback.
- Firmware has a 15-second continuous-note cutoff as a fallback if a release event is lost.
- Recording stores up to 160 note events in RAM and is lost on reset or power-off.
- The passive buzzer plays simple square-wave pitches, not full-fidelity instrument audio.
- Browser/BLE latency depends on the phone and Bluetooth stack; this is responsive control, but not a professional low-latency MIDI system.
- The OLED and physical buttons are kept on their existing pins but aren't used by this first version. They can be added for local status and controls later.
- This starter code has not been compiled on the user's exact board/library version or tested on physical hardware yet. Compile before uploading, and keep the existing Kermet Console sketch as a separate backup.
