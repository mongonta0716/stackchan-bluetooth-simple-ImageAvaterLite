# stackchan-bluetooth-simple

[Japanese](README.md) | English

# Overview

This project adds Bluetooth speaker functionality and Stack-chan servo control to an image avatar rendered by M5StackImageAvatarLib.
It is modified from the Bluetooth_with_ESP32A2DP example in [M5Unified](https://github.com/m5stack/M5Unified).

# Development Environment
- VSCode
- PlatformIO

# Supported Models

- M5Stack Basic/Gray/M5Go<br>For Basic, only models with 16MB Flash memory are supported.

- M5Stack Fire

- M5Stack Core2 / Core2 for AWSIoT

# Required Libraries
Arduino-ESP32 has been tested with version 2.0.4 (2.0.0 for Fire only). M5Stack Fire has issues with arduino-esp32v2.0.4 and won't start.

For detailed version information, please check [platformio.ini](https://github.com/mongonta0716/stackchan-bluetooth-simple/blob/main/platformio.ini).

- [M5StackImageAvatarLib](https://github.com/mongonta0716/M5StackImageAvatarLib)

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)

- [stackchan-arduino](https://github.com/mongonta0716/stackchan-arduino)

# Compilation Notes

- M5Stack Fire V2.6/M5Stack Basic V2.6<br>When using TF card, we found that SD.begin() cannot read at speeds above 20MHz. We have lowered it to 15MHz.

- M5Stack Basic V2.6<br>When compiling with VSCode+PlatformIO, select `env:m5stack-grey` as the env.

# Configuration
If there is no YAML configuration file on the SD card, default values will be used. (Set to connect servo to PortA.)
Place `/yaml/SC_BasicConfig.yaml` on the SD card to use your own settings.

To display the image avatar, copy `data/bmp_slime`, `data/yaml/M5AvatarLiteSystem.yaml`, and `data/yaml/M5AvatarLite00slime.yaml` from this repository to the SD card while preserving the directory structure. The bundled images use PSRAM and are intended for Core2 or Fire.

**On 2022/10/18, we changed from JSON to YAML. You can convert from JSON to YAML using [JSON to YAML Converter](https://www.site24x7.com/tools/json-to-yaml.html).**<br>Please note that comment handling has changed.
**On 2025/03/24, we changed to use the stackchan-arduino library. Please create SC_BasicConfig.yaml based on the contents of SC_Config.yaml.**

For details, please refer to the [YAML file](https://github.com/mongonta0716/stackchan-bluetooth-simple/blob/main/data/yaml/SC_BasicConfig.yaml).

## Configuration Items
(Values in parentheses are default values)
- servo
    - pin
        - x(Core1 22, Core2 33, CoreS3 1)<br> Specify GPIO for X-axis (RX for serial servo)
        - y(Core1 21, Core2 32, CoreS3 2)<br> Specify GPIO for Y-axis (TX for serial servo)
    - offset<br>Parameter to correct misalignment when servo axis is at 90°
        - x(0)<br> Set X-axis offset value
        - y(0)<br> Set Y-axis offset value
    - center<br>Specify servo center position
        - x(180)<br> X-axis center position
        - y(270)<br> Y-axis center position
    - lower_limit<br>Specify servo lower limit
        - x(0)<br> X-axis lower limit
        - y(240)<br> Y-axis lower limit
    - upper_limit<br>Specify servo upper limit
        - x(360)<br> X-axis upper limit
        - y(280)<br> Y-axis upper limit
    - speed<br>Specify waiting time and servo movement time during standby and when sound is playing through Bluetooth speaker. Random values are used within the range specified by minimum and maximum values.
        - normal_mode
             - interval_min(3000)
             - interval_max(6000)
             - move_min(500)
             - move_max(1500)
        - sing_mode
             - interval_min(500)
             - interval_max(1000)
             - move_min(500)
             - move_max(1000)
- bluetooth
    - device_name(M5Stack)<br>Specify the Bluetooth speaker device name.
    - starting_state(false)<br>Specify whether to start in Bluetooth mode.
    - start_volume(100)<br>Set initial value for Bluetooth speaker

- auto_power_off_time(0)<br>Core2 only. Powers off after the specified time has elapsed after USB power supply is turned OFF. (0 means no power off)

- balloon<br>Configure speech bubble settings.
    - font_language("CN")<br>Specify font language. "JA" or "CN", uses Latin font if not specified.
    - lyrics<br>Set lines to display randomly in normal mode. Maximum of 10 lines.
- led_lr(0)<br>The Nekomimi LED connected to Port.B OUT (GPIO26) reacts to the left and right audio levels. Power it from Port.B 5V/GND; 18 SK6812 LEDs are controlled, nine per ear.
    - 0: Stereo
    - 1: Responds to left volume only
    - 2: Responds to right volume only
- led_pin(15)<br>This setting is for M5Go Bottom. It is unused while Nekomimi LED uses the fixed GPIO26 connection.
- takao_base(false)<br>Setting for using [Stack-chan_Takao_Base](https://ssci.to/8905). (*Currently only supported on Core2)
- servo_type("DYN_XL330")<br>Specify servo type
    - "PWM": SG90 series PWM servo
    - "SCS": Feetech SCS0009 serial servo
    - "DYN_XL330": Dynamixel XL330 serial servo
- extend_config_filename("")<br>Configuration filename for application
- extend_config_filesize(2048)<br>Buffer size for feature extensions
- secret_config_filename("")<br>Configuration filename for personal information
- secret_config_filesize(2048)<br>Buffer size for personal information
- secret_info_show(true)<br>Whether to output personal information to log

# Usage

- BtnA<br>Enter Bluetooth mode. (Only valid when bluetooth_mode = false)<br>
- BtnA hold for 3 seconds<br>Forget the saved peer and old bond information, then enter Bluetooth pairing mode. This works whether Bluetooth mode is on or off. Use it for the first connection or when changing PCs. Normal Bluetooth mode only accepts the saved PC and is not discoverable by nearby PCs.<br>

After a PC has connected once, it is remembered and Bluetooth starts automatically on the next boot to reconnect, regardless of the `starting_state` setting.

On the first boot of firmware using the updated peer-protection format, a peer saved by an older version is discarded once. If the screen shows `Bluetooth Pairing` or `Waiting Connection`, remove the old `M5StackBTSPK` entry in Windows settings on the intended PC, then add it again. Keep Bluetooth off on PCs that should not connect until this initial enrollment is complete. If authentication with the saved PC fails, automatic reconnection stops to avoid repeatedly opening the Windows pairing dialog and the screen shows `Pairing Required`. Hold BtnA for three seconds and repeat the same pairing procedure.

- BtnB<br>Decrease volume.

- BtnC<br>Increase volume.

# Credit
- [meganetaaan](https://github.com/meganetaaan)
- [lovyan03](https://github.com/lovyan03/LovyanGFX)
- [robo8080](https://github.com/robo8080)
- [tobozo](https://github.com/tobozo)

# LICENSE
[MIT](LICENSE)

# Author
[Takao Akaki](https://github.com/mongonta0716)
