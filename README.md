# Orion Energy Monitor

A three-phase energy monitoring system built on ESP32 for real-time power generation tracking and blockchain validation integration.

## Overview

The Orion Energy Monitor is an ESP32-based IoT device that measures voltage and current across three phases, calculates power generation in real-time, and transmits energy data via MQTT to Orange Pi gateway devices. The system integrates with the Dione Protocol's ORION blockchain validator network for renewable energy validation on the Odyssey blockchain.

**Key Capabilities:**
- Real-time three-phase energy monitoring with ±1% accuracy
- MQTT-based data transmission with automatic broker discovery
- WiFi provisioning via captive portal
- Secure OTA firmware updates from GitHub releases
- Battery-powered operation with intelligent light sleep
- Dynamic sensor calibration stored in NVS
- Week-long data buffering during network outages

## Hardware Requirements

### Core Components
- **ESP32 Development Board** - Main microcontroller
- **ZMPT101B Voltage Sensor** - AC voltage measurement module
- **3× SCT-013 Current Transformers** - Non-invasive current sensing (30A/100A variants)
- **Li-ion Battery** (3.7V) with TP4056 charging circuit

### Supporting Components
- Status LEDs (power, WiFi, charging, battery)
- Push buttons for WiFi pairing and hard reset
- Bulk capacitors for power stability (470µF-1000µF recommended)

## Pin Configuration

| Function | GPIO | Notes |
|----------|------|-------|
| Voltage Sensor (ZMPT101B) | 32 | ADC1_CH4 |
| Current Phase R | 39 | ADC1_CH3 |
| Current Phase Y | 34 | ADC1_CH6 |
| Current Phase B | 35 | ADC1_CH7 |
| Battery Voltage Monitor | 36 | ADC1_CH0 |
| Power LED | 14 | Active HIGH |
| WiFi Status LED | 27 | Active HIGH |
| Charging Indicator LED | 26 | Active HIGH |
| Battery Status LED | 25 | Active HIGH |
| WiFi Pairing Button | 4 | Active LOW with pull-up |
| Hard Reset Button | 13 | Active LOW with pull-up |
| Charging Status Input | 33 | From TP4056 STDBY pin |

## Features

### Energy Monitoring
- **Three-phase measurement**: Simultaneous voltage and current sampling across R, Y, B phases
- **High accuracy**: ±1% measurement accuracy through NVS-based dynamic calibration
- **Real-time calculations**: Power (W), apparent power (VA), power factor per phase
- **Energy accumulation**: Cumulative energy tracking (kWh) with persistent storage
- **Data buffering**: Week-long data retention in NVS during MQTT unavailability

### Connectivity & Communication
- **WiFi Management**: WPA2 with automatic reconnection and captive portal setup
- **MQTT Protocol**: QoS 1 messaging with automatic broker discovery via mDNS
- **Network Resilience**: Smart reconnection with exponential backoff
- **Data Integrity**: ETag-based synchronization and deduplication

### Power Management
- **Intelligent Sleep**: Light sleep mode between measurement cycles with wake-on-interrupt
- **Battery Monitoring**: Real-time SOC estimation and low-battery protection
- **Charging Detection**: TP4056 status integration for charge state awareness
- **Power Optimization**: <50mA average consumption in sleep mode
- **Brownout Protection**: Bulk capacitance prevents WiFi transmission resets

### Firmware & Updates
- **Secure OTA**: GitHub-based firmware updates with rollback protection
- **Version Control**: Semantic versioning with Git tag integration
- **Update Safety**: Comprehensive error handling for network timeouts and power failures
- **Testing Framework**: Automated OTA validation and power consumption testing

### User Interface
- **LED Status Indicators**: Visual feedback for power, WiFi, charging, and battery states
- **Button Controls**: WiFi pairing and factory reset functionality with debouncing
- **LCD Display** (optional): Real-time energy metrics with 24-hour and 7-day analytics

## Quick Start

### Prerequisites

- [PlatformIO CLI](https://platformio.org/install/cli) or IDE
- ESP32 board support package
- USB to serial driver (CP2102/CH340)

## Configuration

### Calibration Values

The system uses NVS (Non-Volatile Storage) for dynamic calibration. Initial values in `config.h`:

```cpp
// Voltage calibration factor (adjust based on your ZMPT101B module)
#define VOLTAGE_CALIBRATION 106.8

// Current calibration (per-phase, can be calibrated individually)
#define CURRENT_CALIBRATION_R 30.0
#define CURRENT_CALIBRATION_Y 30.0
#define CURRENT_CALIBRATION_B 30.0
```

### Network Configuration

```cpp
// MQTT broker settings (mDNS auto-discovery is preferred)
#define MQTT_BROKER_HOST "orion.local"  // Or IP address
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID_PREFIX "OrionEM"

// Data transmission interval
#define DATA_SEND_INTERVAL_MS 30000  // 30 seconds
```

## MQTT Communication

### Published Topics

| Topic | QoS | Retained | Description |
|-------|-----|----------|-------------|
| `energy/metrics` | 1 | No | Real-time energy measurements (JSON) |
| `orion/status` | 1 | Yes | Device online/offline status |
| `orion/wifi_credentials` | 1 | Yes | WiFi credentials entered by the User |


### Subscribed Topics

| Topic | Description |
|-------|-------------|
| `pairing/status` | Pairing status between Meter and Gateway |
| `orion/config` | WiFi credential updates from gateway |
| `orion/trigger` | Remote commands (reset, calibrate, etc.) |
| `orion/refresh` | Network scan request for WiFi troubleshooting |

### Energy Metrics Payload

```json
{
   "deviceId": "F8B3B77FEAF4",
    "timestamp": "2026-02-11 19:35:53",
    "battery": 82.36668,
    "voltage": 217.7177,
    "totalPower": 0,
    "energyTotal": 0.00092,
    "phases": [
    {
       "current": 0,
       "power": 0
   },
   {
      "current": 0,
       "power": 0
   },
   {
      "current": 0,
       "power": 0
   }
 ]
}
```

## WiFi Provisioning

### Initial Setup

1. **Enter Provisioning Mode**
   - If there is no saved networks, the device will automatically enter Provisioning Mode, ELSE
   - Hold the WiFi pairing button during power-on, OR
   - Press hard reset button to factory reset

3. **Connect to Device**
   - Look for WiFi network: `OrionSetup`
   - Connect from your phone/laptop (no password required)

4. **Configure Network**
   - Captive portal will open automatically
   - Select your WiFi network from the scan list
   - Enter password and save

5. **Automatic Connection**
   - Device restarts and connects to configured network
   - WiFi LED indicates connection status (solid = connected)

### WiFi LED Status Codes

- **Off**: WiFi disabled or not initialized
- **Slow Blink** (1s): Searching for network
- **Fast Blink** (200ms): Provisioning mode active
- **Solid On**: Connected to network and MQTT broker

## Calibration Procedures

### Voltage Sensor Calibration (ZMPT101B)

1. **Measure reference voltage**
   ```bash
   # Use a calibrated multimeter on mains voltage
   # Example reading: 230.5V AC
   ```

2. **Read device voltage**
   ```bash
   # From serial monitor or MQTT message
   # Example reading: 228.3V
   ```

3. **Calculate new calibration**
   ```
   new_calibration = current_calibration × (actual_voltage / reported_voltage)
   new_calibration = 106.8 × (230.5 / 228.3) = 107.83
   ```

4. **Update and test**
   - Update `VOLTAGE_CALIBRATION` in `config.h` and reflash

### Current Sensor Calibration (SCT-013)

**Method 1: Known Load Test**

1. **Connect calibrated load**
   - Use a heater or resistive load with known power rating
   - Example: 1000W heater → 1000W / 230V = 4.35A

2. **Measure with clamp meter**
   - Verify actual current draw
   - Example reading: 4.38A

3. **Compare with device reading**
   - Check MQTT payload or serial output
   - Example device reading: 4.52A

4. **Calculate calibration per phase**
   ```
   new_cal = current_cal × (actual_current / reported_current)
   new_cal = 30.0 × (4.38 / 4.52) = 29.07
   ```

**Method 2: Testbench Calibration (Recommended)**

For precise calibration, use the custom 2.5kW resistor load bank testbench:

1. Connect phase under test to load bank
2. Measure current with precision clamp meter
3. Record device reading via serial/MQTT
4. Calculate and apply calibration factor
5. Store in NVS using dedicated calibration script
6. Repeat for remaining phases

> **Note**: Each phase can have individual calibration values stored in NVS for maximum accuracy.

## Project Structure

```
orion-monitor/
├── include/
│   ├── config.h              # System configuration constants
│   ├── battery_manager.h     # Battery monitoring & charging
│   ├── certificates.h         # Bluetooth Low Energy (future)
│   ├── button_manager.h      # Button debouncing & handling
│   ├── energy_sensor.h       # Three-phase measurement
│   ├── led_manager.h         # Status LED control
│   ├── mqtt_manager.h        # MQTT client & broker discovery
│   ├── ota_manager.h         # Firmware update handling
│   ├── utils.h               # Helper functions & utilities
│   └── wifi_manager.h        # WiFi & captive portal
├── src/
│   ├── main.cpp              # Application entry point
│   ├── battery_manager.cpp
│   ├── button_manager.cpp
│   ├── energy_sensor.cpp
│   ├── led_manager.cpp
│   ├── mqtt_manager.cpp
│   ├── ota_manager.cpp
│   ├── utils.cpp
│   └── wifi_manager.cpp
├── test/
│   └───test_embedded/
│       ├───test_battery
│       ├───test_button
│       ├───test_chip
│       ├───test_energy
│       ├───test_integration
│       ├───test_led
│       ├───test_memory
│       ├───test_mqtt
│       ├───test_ota
│       ├───test_preferences
│       ├───test_time
│       └───test_wifi
├── platformio.ini            # PlatformIO build configuration
├── README.md                 # This file
└── LICENSE
```

## Dependencies

All dependencies are automatically managed by PlatformIO:

| Library | Version | Purpose |
|---------|---------|---------|
| WiFiManager | ^2.0.16-rc.2 | Captive portal for WiFi setup |
| PubSubClient | ^2.8 | MQTT client implementation |
| ArduinoJson | ^6.21.3 | JSON serialization/parsing |
| EmonLib | ^1.1.0 | Energy monitoring calculations |
| NimBLE-Arduino | ^1.4.1 | Bluetooth Low Energy (optional) |
| Unity | ^2.5.2 | Unit Testing |

## Troubleshooting

### WiFi Connection Issues

**Problem**: Device not connecting to WiFi network

**Solutions**:
- Verify credentials are correct (case-sensitive)
- Check signal strength (move closer to AP if RSSI < -75dBm)
- Ensure WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
- Check router security (WPA2-PSK supported, WPA3 may need router config)
- Use hard reset to clear stored credentials and reconfigure

### MQTT Connection Issues

**Problem**: Cannot connect to MQTT broker

**Solutions**:
- Verify broker is running: `sudo systemctl status mosquitto`
- Check broker is accessible: `ping orion.local` or broker IP
- Verify firewall allows port 1883: `sudo ufw allow 1883/tcp`
- Check mDNS/Avahi is running: `systemctl status avahi-daemon`
- Review broker logs: `sudo journalctl -u mosquitto -f`

**Problem**: Messages not being received

**Solutions**:
- Verify topic names match exactly (case-sensitive)
- Check QoS settings (device uses QoS 1)
- Ensure buffer sizes are adequate in PubSubClient config
- Monitor with MQTT client to verify broker is receiving messages

### Measurement Issues

**Problem**: Incorrect voltage readings

**Solutions**:
- Verify ZMPT101B is connected to L and N (not ground)
- Check calibration value in config.h
- Ensure ADC reference voltage is correct (3.3V)
- Test sensor with multimeter for comparison
- Verify sensor isn't damaged (check output waveform with oscilloscope)

**Problem**: Current readings are zero or very low

**Solutions**:
- Verify CT clamp orientation (arrow toward load)
- Ensure only one conductor passes through CT (not both L and N)
- Check CT burden resistor value (typically 22Ω-100Ω depending on sensor)
- Verify current is actually flowing (test with clamp meter)
- Check ADC bias voltage (should be ~1.65V with no current)

**Problem**: Readings fluctuate wildly

**Solutions**:
- Add capacitors to ADC inputs (100nF ceramic recommended)
- Ensure good grounding and shielding
- Move sensor wires away from high-voltage conductors
- Check for loose connections
- Verify power supply is stable and noise-free

### Power & Battery Issues

**Problem**: Brownout resets during WiFi transmission

**Solutions**:
- **Critical**: Add 470µF-1000µF bulk capacitor near ESP32 VIN
- Use quality battery with sufficient discharge rating (>1C)
- Verify battery voltage is adequate (>3.3V under load)
- Check power path from battery to ESP32

**Problem**: Battery draining too quickly

**Solutions**:
- Increase `DATA_SEND_INTERVAL_MS` (reduce transmission frequency)
- Verify light sleep is working (check serial output for sleep logs)
- Disable unnecessary peripherals (LCD, LEDs)
- Check for floating GPIO pins causing leakage current
- Use deep sleep for longer intervals (requires hardware modification)

**Problem**: OTA updates failing

**Solutions**:
- Ensure sufficient battery charge (>30% recommended)
- Verify stable WiFi connection during update
- Check GitHub release URL is accessible
- Review OTA partition size in platformio.ini
- Monitor serial output for specific error messages
- Test with smaller firmware first

## Performance Specifications

- **Measurement Accuracy**: ±1% (with proper calibration)
- **Sampling Rate**: Configurable, default 30s intervals
- **Power Consumption**: 
  - Active (WiFi TX): ~180mA @ 3.7V
  - Light Sleep: <50mA @ 3.7V
  - Deep Sleep: <10µA (future implementation)
- **Battery Life**: 
  - 2000mAh battery: ~18-24 hours @ 30s intervals
  - 5000mAh battery: ~45-60 hours @ 30s intervals
- **WiFi Range**: Up to 100m line-of-sight (depends on AP)
- **Data Buffering**: 7 days in NVS (at 30s intervals)
- **OTA Update Time**: ~60-90 seconds for typical firmware
- 

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- **Dione Protocol LLC** - Project sponsor and blockchain integration
- **ESP32 Community** - Extensive documentation and support
- **OpenEnergyMonitor** - Inspiration for energy monitoring algorithms

---

