# PlantPulse

## **Soil Moisture & Environmental Sensing with Wireless Gauge Display**

###### Linghao Meng 2430494

**PlantPulse** is a two-device system that **monitors soil moisture, ambient temperature, and humidity**, and **presents plant health through a physical gauge needle, LED indicator, and an OLED screen**.
A low-power sensing device collects environmental data, while a separate display device receives the data wirelessly via BLE and provides intuitive visual feedback and interaction.

------



### 1 System Overview & Physical Sketch

#### 1.1 System Overview

The system is composed of two independent but wirelessly connected devices:

- **Sensing Device**: Placed near or inside the plant pot to measure soil moisture and environmental conditions.
- **Display Device**: Placed on a desk or shelf to visualize plant status using a stepper-driven gauge, LED, and OLED screen.

#### 1.2 Physical Sketch

<img src="images\Overview.jpg" alt="1" style="zoom:50%;" width="50%"/>

------



### 2 Sensor Device

#### 2.1 Detailed sketch

<img src="images\SensingDevice.jpg" alt="1" style="zoom:50%;" width="50%"/>

| Part Number |         Device Name          |
| :---------: | :--------------------------: |
|     S0      | SENSING DEVICE XIAO ESP32-C3 |
|     S1      |          SHT31-DIS           |
|     S2      |           SEN0193            |
|     S3      |         LiPo Battery         |

#### 2.2 Description

1. Periodically reads: Soil moisture (analog ADC); Ambient temperature (°C); Ambient humidity (%RH)

2. Applies basic DSP: EMA smoothing

3. Sends processed data to the display device via BLE notifications

4. Uses deep sleep to minimize power consumption

------



### 3 Display Device

#### 3.1 Detailed sketch

<img src="images\DisplayDevice.jpg" alt="1" style="zoom:50%;" width="50%"/>

| Part Number |         Device Name          |
| :---------: | :--------------------------: |
|     D0      | DISPLAY DEVICE XIAO ESP32-C3 |
|     D1      |           X27.168            |
|     D2      |           ULN2003A           |
|     D3      |         OLED SSD1306         |
|     D4      |           TS1187A            |
|     D5      |           WS2812B            |
|     D6      |         LiPo battery         |

#### 3.2 Description

1. Receives sensor data via BLE

2. Runs decision logic / ML inference to determine plant health state

3. Main display: Stepper gauge shows overall plant health; LED indicates status (Dry / OK / Wet)

4. OLED display shows one of three values: Soil moisture, Temperature, Humidity

5. Button interaction: Short press cycles OLED display mode

------



### 4 BLE Communication & System Workflow

#### 4.1 How the devices communicate with each other

<img src="images\Communication.jpg" alt="1" style="zoom:50%;" width="50%"/>

#### 4.2  How it will work

<img src="images\Interaction.jpg" alt="1" style="zoom:50%;" width="50%"/>