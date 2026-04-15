# Orion Energy Meter — Changelog

All notable changes to the ESP32 energy meter firmware are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [1.0.0] — 2026-04-15

### Core Features
- Three-phase energy monitoring (ZMPT101B voltage + 3× SCT-013 current)
- Per-phase NVS-based dynamic calibration with individual R/Y/B values
- Real-time power (W), energy (kWh), and cumulative energy tracking
- CT sensor auto-detection with periodic recheck during operation
- 500-sample RMS voltage measurement for ±1% accuracy

### Connectivity
- WiFi with WPA2 and captive portal provisioning ("OrionSetup" AP)
- MQTT QoS 1 via PubSubClient with mDNS broker auto-discovery
- Automatic MQTT reconnection with exponential backoff
- Smart fallback mode: disables auto-reconnect after prolonged disconnection, switches to manual retry intervals
- WiFi credential updates received over MQTT (orion/config topic)
- mDNS hostname registration as "OrionEM"

### Data Resilience
- NVS-based offline data buffer (672 readings = 1 week at 15-min intervals)
- CompactReading struct (packed binary) for efficient NVS storage
- Automatic buffer flush on MQTT reconnection with progress logging
- Corrupted reading detection and skip during buffer replay
- Ring buffer with separate read/write indices and count metadata

### Power Management
- Light sleep between measurement cycles with configurable duration
- Smart sleep with periodic wake for button checks (CHECK_INTERVAL_MS)
- EXT0 wake on hard reset button, EXT1 wake on WiFi pairing button
- Noise rejection during sleep wake (80% sample threshold over 10 readings)
- Battery voltage monitoring via ADC with 20-sample averaging
- TP4056 charging status detection via GPIO 33
- LED indicators for power, WiFi, charging, and battery states

### OTA Updates
- GitHub-based firmware updates from DioneProtocol/orion-firmware
- ETag-based version detection (version.json + firmware.bin)
- SHA-256 checksum verification before installation
- Rollback protection via esp_ota partition validation
- Measurement-aware scheduling: OTA waits for active sensor reads to complete
- Automatic daily check interval (OTA_CHECK_INTERVAL_MS = 86400000)
- Power loss recovery: detects first boot after OTA via partition state

### Hardware Interface
- Button debouncing with multi-sample validation (10 samples, 80% threshold)
- Boot mode detection: normal, WiFi pairing (3s hold), hard reset (5s hold)
- Immediate action callbacks during button press (not just on release)
- LED manager with blink modes: solid on, fast blink, slow blink, off
- Hard reset clears all NVS preferences and restarts in provisioning mode

### Testing
- Embedded Unity test framework with 12 test categories
- Test suites: battery, button, chip, energy, integration, LED, memory, MQTT, OTA, preferences, time, WiFi
- TLS connection tests with Let's Encrypt and ISRG Root X1 CA certs
- OTA corruption and power-loss recovery tests
- ADC stability and calibration accuracy tests

### Security (Prepared, Not Yet Active)
- MqttSecurityConfig struct with TLS, mutual TLS, and auth fields
- WiFiClientSecure setup with CA cert, client cert, and private key slots
- MQTT credential storage and loading via NVS
- Certificate infrastructure in certificates.h (commented, awaiting VPS broker)

### Dependencies
- PlatformIO (espressif32 platform)
- ArduinoJson v7.2.0
- WiFiManager v2.0.17
- PubSubClient v2.8
- EmonLib v1.1.0
- NimBLE-Arduino v2.3.6 (included, BLE not yet active)
- Unity v2.5.2 (test framework)