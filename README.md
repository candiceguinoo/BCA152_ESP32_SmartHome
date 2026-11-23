# ESP32 Smart Home BLE Mesh System

A wireless smart home automation system built using ESP32 microcontrollers and Bluetooth Low Energy (BLE) Mesh. The project uses three ESP32 nodes to provide automatic lighting control through an ambient light sensor and manual control through a web-based interface hosted by the Gateway.

## Project Overview

This project demonstrates a distributed smart home system using BLE Mesh communication. Instead of relying on a single ESP32 to handle every operation, the system separates its functions into three nodes: Gateway, Switch, and Light Sensor.

The Light Sensor monitors the surrounding light level and publishes its detected state through the BLE Mesh network. The Switch receives the sensor's messages and controls a relay connected to a load. The Gateway provides a web interface that allows users to manually turn the connected load ON or OFF.

The system uses BLE Mesh for communication between the ESP32 nodes, while Wi-Fi provides connectivity between the Gateway and the user's web browser.
