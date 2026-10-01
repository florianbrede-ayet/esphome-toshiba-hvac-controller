# ESPHome Toshiba / Carrier HVAC Controller over UART / WIFI Interface
This repository contains both the hardware (kicad & production files) for an ESP32 module as well as an [ESPHome](https://esphome.io/) component to control Toshiba / Carrier RAS HVAC units independently from the Toshiba Cloud.

ESP8266 is also supported but requires custom wiring.

The device can be used as a local, low cost drop in replacement to control the AC from [Home Assistant](https://www.home-assistant.io/) or others.

Features of the component:
* All operation modes & fan modes supported
* Does not depend on external vendor cloud services
* External temperature sensor support for accurate temperature regulation (external sensor entity configurable)
* Wired and IR remotes still work, even with external temperature sensor
* Supports most special modes / merit modes like silent, fireplace, extended heating temperature range
* Support for power modes on single split units and ionizer toggle
* Additional information like fan speeds, compressor load, refrigerant / pipe temperatures
* WiFi LED toggle (on/off via UART registers 0xDE/0xDF)

Missing functionality:
* Manual defrost (probably unsupported over UART WIFI interface)
* ODU error messages (not decoded yet)
* Timer / schedules (had no interest in this functionality)
* Capability detection (all fan & merit modes will be shown, but not all might have an effect depending on the IDU)


Requirements:
* Toshiba/Carrier RAS single- or multisplit unit with built-in WIFI interface or WIFI module support (others might work as well, but untested)
* ESP32 DevKit 38 Pins (others like NodeMCU 8266 work as well but might require modifications)
* Toshiba HVAC module (see `hardware/` directory)
* ESPhome installed to flash the ESP32 initially


<img src="images/entities.png" width="100%" alt="Home Assistant Entities">

# UART Protocol
The IDU communicates over 5V `UART 9600E1`.
Messages include a length and a distinct header. Initial synchronization is ensured with a 200ms message timeout.

The protocol is relatively simple and can be observed in the `process_uart_rx` and `handle_message` functions, where the message type is identified by its length.
I've decoded most of the protocol by reading the communication with stock WRE-T00BJ10 modules and analyzing the messages against known responses from the Toshiba Cloud.

We can request registers from the IDU, we can write certain registers and also the IDU will send registers if updated externally (IDU / ODU status and changes via IR remote).


# Compatibility
Probably all Toshiba RAS units with wifi capabilities (built-in or with additional module) are supported.

The software has been tested with the following outdoor units:
* RAS-3M18U2AVG-E (Tripe-split)
* RAS-2M14U2AVG-E (Dual-split)
* RAS-25G2KVP-ND (Single-split)
And these indoor units:
* RAS-B10N4KVRG-E (Haori)
* RAS-25G2AVP-ND (Super DAISEIKAI 8)

Other units (with any compatible outdoor unit) that are almost certainly supported:
* Toshiba Seiya
* Toshiba Shorai
* Rebranded models like *Carrier X Inverter*


# Hardware

<img src="images/schematic.png" width="75%" height="75%">
<img src="images/back.jpg" width="320px" alt="PCB Headers">
<img src="images/top.jpg" width="320px" alt="PCB Headers">

The hardware needs three main components:
1) **Microcontroller**. Any ESP32-DevKit with 38 Pins should work. ESP8266 is also supported, see "ESP8266" below.
2) **Bidirectional Voltage Level Translator**. This is needed to connect to the UART interface and shift between 5V from the AC to 3.3V of the ESP32. I used a `Texas Instruments TXS0108EPWR`.
3) **Connector** Toshiba uses as JST PASK connector for the WIFI modules cable, `JST S05B-PASK-2(LF)(SN)` is the correct connector. 

External voltage regulators are not required, as the UART inteface in the indoor units works at 5V and can be directly fed into the ESP32 DevKit.

The Kicad 7 project files and ready-to-use production files (for JLCPCB or PcbWay manufacturing) are available in `hardware/` and `hardware/production` respectively.

*Attention: The Haori units with built-in WIFI modules lack height for the 2x 19p 2.54mm pin sockets. I disabled placement for the pin sockets and soldered the ESP32 on the modules:* 

<img src="images/headers.jpg" width="320px" alt="PCB Headers">

# Installation
This depends on the indoor unit.
The following photos from a Toshiba Haori show how the built-in module can be replaced in a few minutes:

<img src="images/install1.jpg" width="320px" alt="Installation 1">
<img src="images/install2.jpg" width="320px" alt="Installation 2">
<img src="images/install3.jpg" width="320px" alt="Installation 3">
<img src="images/install4.jpg" width="320px" alt="Installation 4">
<img src="images/install5.jpg" width="320px" alt="Installation 5">

# Firmware

## ESPHome 2024+ (recommended)

The `toshiba_controller/` directory contains an `external_components`-based component compatible with ESPHome 2024.x and later. The legacy `platform: custom` approach no longer compiles on current ESPHome versions.

> **Note:** ESPHome 2026.5.0 removed the deprecated `ClimateTraits::set_supports_*` accessors in favor of `add_feature_flags()`/`has_feature_flags()`. This component has been updated accordingly and is compatible with ESPHome 2026.5.0 and later (as well as 2024.x/2025.x).

To use the component:
1. Clone this repository and navigate to the `esphome` directory.
2. Install ESPHome: https://esphome.io/guides/installing_esphome.html
3. Copy `template_v3.yaml` to (for example) `toshiba-livingroom.yaml` and fill in your WiFi credentials, API key, and temperature sensor entity ID.
4. Connect the ESP32 module via USB (first flash only; subsequent updates use OTA).
5. Run `esphome run toshiba-livingroom.yaml`.
6. Add the device in Home Assistant (Settings → Devices & Services).

## Migrating from Legacy (`base.yaml` / `platform: custom`)

If you are upgrading from the old `base.yaml` include with `platform: custom`, the main changes are:

1. **Remove the `<<: !include base.yaml`** line and the `includes: [toshiba-controller.h]` reference.
2. **Add `external_components`** pointing to the `toshiba_controller/` directory (see `template_v3.yaml`).
3. **Replace the `climate: platform: custom` block** with a top-level `toshiba_controller:` block.
4. **Replace `sensor: platform: custom`** with `sensor: platform: toshiba_controller` (sensor names are preserved).
5. **Replace `switch: platform: custom`** with `switch: platform: toshiba_controller` for Internal Thermistor and Ionizer.
6. **Split the Special Mode select** into three separate selects: Special Mode, Silent Mode, and Fireplace (the old config had all modes in a single flat list).
7. **Optionally add** the new WiFi LED template switch.

See `template_v3.yaml` for a complete example of the new configuration format.

## Legacy ESPHome (< 2024)

If you are still on an older ESPHome version, use `template.yaml` with the single-file `toshiba-controller.h` from an [older release](https://github.com/florianbrede-ayet/esphome-toshiba-hvac-controller/tree/90621ac). Note that `platform: custom` has been removed in ESPHome 2024.

## Logs

```bash
esphome logs toshiba-livingroom.yaml
```

# ESP8266
ESP8266 is supported but requires custom wiring. Use `template_v3.yaml` as a starting point and adjust the board, UART pins (`GPIO1` TX / `GPIO3` RX), and framework accordingly.
A bidirectional logic level converter must be connected to the UART pins.

The NodeMCU can be powered directly from the indoor unit's UART connector 5V Pin 3 through VIN.

# FAQ
## Smart Thermostat / Internal Thermistor
If the binary switch `Internal Thermistor` is disabled in Home Assistant, the external temperature sensor supplied in the `yaml` configuration will be used for the room temperature.

Most ACs suffer from poor thermistor placement (high position, heat draft) and Toshiba does not support external thermistors over UART.

This requires a more complex regulation in the `smart_thermostat_control` function which can be fine tuned with the `smart_thermostat_multiplier` config parameter.

A recommended value is `4`, which results in a regulation accuracy of around `+/- 0.25°K`.
Higher values increase the accuracy at the cost of more compressor cycles.

## ODU parameters (`cduIac` & `cduLoad`)
The parameters `cduIac` and `cduLoad` seem off at first however, the values are identical to Toshiba's own parameters.

The `cduLoad` is internally called `cduCompHz` by Toshiba and has a range from 0-170.
Since the value has an identical range regardless of the compressor, does not match the actual compressor frequency and is independant for each indoor unit, I converted the number to a percentage which correlates well with the requested power for the IDU.

The same is true for `cduIac` which is closely following `cduLoad`.
My best guess is that `cduLoad` is the heat request for the IDU and `cduIac` is related to the IDU's EEV.

# Credits
* Inspiration & initial protocol description from [ToshibaCarrierHvac](https://github.com/ormsport/ToshibaCarrierHvac)
* ESPhome component structure from [esphome-lg-controller](https://github.com/JanM321/esphome-lg-controller)

# License

This project is released under the MIT License.
