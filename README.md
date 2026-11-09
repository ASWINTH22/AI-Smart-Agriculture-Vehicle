# AI-Powered Smart Agriculture Vehicle 🌱🤖

### ESP32 | ESP32-CAM | Gemini AI | IoT | Web-Based Vehicle Control

## 📌 Project Overview

An AI-powered smart agriculture vehicle designed for remote vehicle operation, real-time agricultural field monitoring, sensor data collection, and AI-based plant health analysis.

The system uses an ESP32 and ESP32-CAM, along with two mobile devices connected to the same Wi-Fi network, to provide live field visualization, vehicle control, environmental monitoring, and plant health analysis using Google Gemini AI.

## ⚙️ System Architecture

### 1. ESP32 – Vehicle Control and Sensor Monitoring
- Controls the agriculture vehicle through a web-based dashboard.
- Controls DC motors using the L298N motor driver.
- Collects temperature, humidity, soil moisture, and air quality readings.
- Sends sensor data to the dashboard.

### 2. ESP32-CAM – Plant Image Capture
- Captures plant images.
- Sends captured images for Google Gemini AI analysis.
- Retrieves plant identification and health assessment results.

### 3. Dual-Mobile Device Interface

**Device 1 – Field Camera**
- Provides a live view of the agricultural field.
- Streams video to the control dashboard.

**Device 2 – Vehicle Control Dashboard**
- Displays live field video.
- Provides remote vehicle movement controls.
- Displays real-time sensor readings.
- Shows captured plant images.
- Displays Gemini AI plant health analysis.

## 🔍 AI-Based Plant Health Analysis

Google Gemini AI is used to analyze captured plant images and provide:
- Plant identification.
- Plant health condition.
- Possible disease identification.
- Estimated healthy and affected percentages.
- AI analysis details and confidence score.

## 🔧 Hardware Components
- ESP32 Development Board
- AI Thinker ESP32-CAM
- L298N Motor Driver
- DC Gear Motors
- DHT11 Sensor
- Soil Moisture Sensor
- MQ135 Air Quality Sensor
- Servo Motor

## 💻 Software and Technologies
- Embedded C / Arduino Programming
- Arduino IDE
- ESP32
- ESP32-CAM
- Wi-Fi Communication
- WebRTC / PeerJS
- Google Gemini AI
- Ubidots IoT Platform

## ⭐ Key Features
- Wireless vehicle control through a web dashboard.
- Live agricultural field visualization.
- Real-time environmental and soil monitoring.
- AI-powered plant health analysis.
- Plant image capture and visualization.
- Dual-mobile device monitoring and control.

## 🎯 Project Objective

To develop an intelligent agriculture vehicle that combines embedded systems, IoT, wireless communication, live video monitoring, and artificial intelligence to support smart agricultural monitoring.

## 📈 Future Improvements
- Automated field navigation.
- Improved plant disease detection.
- Advanced crop monitoring.
- Automated irrigation based on soil conditions.
