# XiaoZhi 2.5 for FoloToy AI Passport

This distribution intentionally contains only the FoloToy AI Passport (ESP32-C3) board implementation. The XiaoZhi core is based on v2.5.0 and the board definition follows the official [FoloToy AI Passport BSP](https://github.com/FoloToy/ai-passport).

(English | [中文](README_zh.md) | [日本語](README_ja.md))

## Introduction

👉 [Human: Give AI a camera vs AI: Instantly finds out the owner hasn't washed hair for three days【bilibili】](https://www.bilibili.com/video/BV1bpjgzKEhd/)

👉 [Handcraft your AI girlfriend, beginner's guide【bilibili】](https://www.bilibili.com/video/BV1XnmFYLEJN/)

As a voice interaction entry, the XiaoZhi AI chatbot leverages the AI capabilities of large models like Qwen / DeepSeek, and achieves multi-terminal control via the MCP protocol.

<img src="docs/mcp-based-graph.jpg" alt="Control everything via MCP" width="320">

## Recent Updates

- The project now requires ESP-IDF v6.0.1 or later. [ESP-IDF v6.1](https://github.com/espressif/esp-idf/releases/tag/v6.1) is the recommended SDK. ESP-IDF 5.x is no longer supported. This distribution builds one variant: `folotoy-ai-passport` for ESP32-C3.
- MQTT and BluFi cryptographic code has migrated to PSA Crypto. IDF 6 component splits and third-party dependency compatibility have also been addressed.
- Audio pipeline concurrency, MQTT/UDP packet validation, and release-matrix selection have been hardened.
- The FoloToy AI Passport includes offline English cards and listening quizzes, with optional cloud voice control.

### Core Features

The following capabilities are inherited from upstream XiaoZhi. Availability depends on hardware and configuration; this distribution includes only the ESP32-C3 AI Passport board. Its default configuration disables ESP wake-word detection.

- Wi-Fi, wired Ethernet, USB RNDIS, and ML307/EC801E or NT26 Cat.1 4G networking; supported boards can switch between Wi-Fi and 4G
- Offline voice wake-up with [ESP-SR](https://github.com/espressif/esp-sr), including customizable wake words
- Two communication transports: [WebSocket](docs/websocket.md) and [MQTT + UDP](docs/mqtt-udp.md)
- Opus audio streaming with conventional streaming ASR + LLM + TTS pipelines and Realtime end-to-end voice models; AEC-capable hardware supports realtime full-duplex interaction
- Speaker recognition, identifies the current speaker [3D Speaker](https://github.com/modelscope/3D-Speaker)
- OLED / LCD displays with emoji and rich expression support, plus camera vision input on supported boards
- Battery display and power management
- 39 interface languages, with localized voice prompts where available and English fallback
- ESP32, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-S3, and ESP32-P4 chip platforms
- Wi-Fi provisioning through hotspot or BluFi
- Device-side MCP for device control (Speaker, LED, Servo, GPIO, etc.)
- Cloud-side MCP to extend large model capabilities (smart home control, PC desktop operation, knowledge search, email, etc.)
- Customizable wake words, fonts, emojis, and chat backgrounds with online web-based editing ([Custom Assets Generator](https://github.com/78/xiaozhi-assets-generator))

## Hardware

### Breadboard DIY Practice

See the Feishu document tutorial:

👉 ["XiaoZhi AI Chatbot Encyclopedia"](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb?from=from_copylink)

Breadboard demo:

![Breadboard Demo](docs/v1/wiring2.jpg)

### Supported hardware

Only the FoloToy AI Passport is enabled in this repository: ESP32-C3, 8 MB flash, no PSRAM, ST7789 240×320 display, ES8311 audio codec, CW2017 fuel gauge, and GPIO0 ADC-ladder buttons. See [`main/boards/folotoy/ai-passport`](main/boards/folotoy/ai-passport) for the pin map and board implementation.

Build it with:

```sh
python scripts/build.py folotoy/ai-passport --name ai-passport --language zh-CN
```

The output is `build/merged-binary.bin` for flash address `0x0`. A raw merged image overwrites the gaps between segments and may overwrite NVS configuration; for an existing device, use the segmented addresses in `build/flash_args` and preserve identity/configuration partitions. This project does not back up existing firmware before flashing.

Other upstream boards and their release variants are not included in this distribution.

## Software

### Firmware Flashing

For beginners, it is recommended to use the firmware that can be flashed without setting up a development environment.

The firmware connects to the official [xiaozhi.me](https://xiaozhi.me) server by default. Personal users can register an account to use the Qwen real-time model for free.

👉 [Beginner's Firmware Flashing Guide](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)

### Development Environment

- Cursor or VSCode
- Install the ESP-IDF plugin. The minimum SDK is [ESP-IDF v6.0.1](https://github.com/espressif/esp-idf/releases/tag/v6.0.1); [ESP-IDF v6.1](https://github.com/espressif/esp-idf/releases/tag/v6.1) is recommended. ESP-IDF 5.x is not supported.
- Linux is better than Windows for faster compilation and fewer driver issues
- This project uses Google C++ code style, please ensure compliance when submitting code

### Developer Documentation

- [Custom Board Guide](docs/custom-board.md) - Learn how to create custom boards for XiaoZhi AI
- [MCP Protocol IoT Control Usage](docs/mcp-usage.md) - Learn how to control IoT devices via MCP protocol
- [MCP Protocol Interaction Flow](docs/mcp-protocol.md) - Device-side MCP protocol implementation
- [MQTT + UDP Hybrid Communication Protocol Document](docs/mqtt-udp.md)
- [A detailed WebSocket communication protocol document](docs/websocket.md)

## Large Model Configuration

If you already have a XiaoZhi AI chatbot device and have connected to the official server, you can log in to the [xiaozhi.me](https://xiaozhi.me) console for configuration.

👉 [Backend Operation Video Tutorial (Old Interface)](https://www.bilibili.com/video/BV1jUCUY2EKM/)

## Related Open Source Projects

For server deployment on personal computers, refer to the following open-source projects:

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python server
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java server
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang server
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang server

Other client projects using the XiaoZhi communication protocol:

- [huangjunsen0406/py-xiaozhi](https://github.com/huangjunsen0406/py-xiaozhi) Python client
- [TOM88812/xiaozhi-android-client](https://github.com/TOM88812/xiaozhi-android-client) Android client
- [100askTeam/xiaozhi-linux](http://github.com/100askTeam/xiaozhi-linux) Linux client by 100ask
- [78/xiaozhi-sf32](https://github.com/78/xiaozhi-sf32) Bluetooth chip firmware by Sichuan
- [QuecPython/solution-xiaozhiAI](https://github.com/QuecPython/solution-xiaozhiAI) QuecPython firmware by Quectel

Custom Assets Tools:

- [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) Custom Assets Generator (Wake words, fonts, emojis, backgrounds)

## About the Project

This is an open-source ESP32 project, released under the MIT license, allowing anyone to use it for free, including for commercial purposes.

We hope this project helps everyone understand AI hardware development and apply rapidly evolving large language models to real hardware devices.

If you have any ideas or suggestions, please feel free to raise Issues or join our [Discord](https://discord.gg/C759fGMBcZ) or QQ group: 1095994019

## Star History

<a href="https://star-history.com/#78/xiaozhi-esp32&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
 </picture>
</a>
