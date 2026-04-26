# 3D-Printer-Room-Temperature-Monitor-

A custom ESP32-C6 based environmental monitoring system for 3D printer enclosures with web-based dashboard access.

## Project Description

This project provides real-time temperature and humidity monitoring for 3D printing environments using industrial-grade sensors and custom PCB design. Built around the ESP32-C6 microcontroller, the system features local data logging, EEPROM-based persistent storage, and a network-accessible web interface for monitoring and configuration.

## Features

- **Industrial-Grade Sensing**: HMW90 temperature and humidity sensor for accurate, reliable measurements
- **Custom PCB Design**: ESP32-C6 based board with optimized sensor interface circuitry
- **Current-to-Voltage Conversion**: Precision shunt resistor design for 3.3V logic compatibility
- **Local Data Logging**: Onboard EEPROM stores historical highs, lows, and critical events
- **Web-Based GUI**: Access real-time data and historical trends from any device on your local network
- **Low Power Operation**: ESP32-C6 efficiency for continuous monitoring
- **Network Connectivity**: Wi-Fi enabled for remote monitoring and data access

## Hardware Components

- **Microcontroller**: ESP32-C6 (custom PCB)
- **Sensor**: HMW90 industrial temperature and humidity sensor
- **Signal Conditioning**: Shunt resistor current-to-voltage conversion
- **Storage**: Onboard EEPROM for data persistence
- **Power**: 3.3V logic levels

## Hardware Design

### Sensor Interface

The HMW90 sensor outputs current signals that are converted to voltage using precision shunt resistors, providing a stable 3.3V logic interface to the ESP32-C6 GPIO pins. This design ensures:

- Noise-resistant signal transmission
- Industrial environment compatibility
- Reliable long-term operation

### Data Storage

The integrated EEPROM chip provides non-volatile storage for:

- Temperature and humidity extremes (highs/lows)
- Timestamp records for environmental events
- User configuration and calibration data
- Historical data persistence across power cycles

## Software Features

### Web Dashboard

Access the monitoring interface from any device on your local network:

- Real-time temperature and humidity display
- Historical data visualization
- Configurable alert thresholds
- Data export capabilities
- No software installation required

### Data Logging

- Continuous environmental monitoring
- Automatic recording of extreme values
- Persistent storage in EEPROM
- Configurable logging intervals

## Use Cases

- **3D Printer Enclosure Monitoring**: Maintain optimal printing conditions
- **Filament Storage**: Ensure proper humidity levels for filament preservation
- **Workshop Environmental Control**: Track conditions in printing workspace
- **Print Quality Optimization**: Identify environmental factors affecting print success
