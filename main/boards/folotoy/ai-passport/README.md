# FoloToy AI Passport (ai-passport)

Board definition that lets the XiaoZhi voice assistant run on the
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport) wearable.

## Hardware

- MCU: ESP32-C3, 8 MB flash, **no PSRAM**, USB Serial/JTAG console
- Audio: ES8311 codec (I2C 0x18) on the shared I2C bus, I2S full-duplex
  (amplifier enable not wired, treated as always on)
- Display: ST7789 (ST7789P3) 240x320 portrait, 4-line SPI
- Battery: CW2017 fuel gauge (I2C 0x63, optional)
- Buttons: UP / DOWN / OK share GPIO0 (ADC1_CH0) through a resistor ladder

Pin mapping follows `ai-passport/components/bsp/include/bsp_pins.h`:

| Function | Pin |
| --- | --- |
| LCD MOSI / SCLK / CS / DC | 9 / 8 / 1 / 20 |
| LCD backlight (PWM) | 21 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 6 / 5 / 3 / 2 / 4 |
| I2C SDA / SCL | 10 / 7 |
| Buttons (ADC ladder) | GPIO0 |

## Build

```sh
python scripts/build.py folotoy/ai-passport --name ai-passport
```

Outputs `build/merged-binary.bin` (8 MB flash, USB Serial/JTAG console).

## Controls

The three physical keys map to XiaoZhi's voice-assistant actions:

- **OK** — single click: toggle the chat state (or enter Wi-Fi config mode
  while starting up)
- **UP** — single click: volume +10; long press: max volume
- **DOWN** — single click: volume -10; long press: mute

Because the ladder shares one ADC pin, XiaoZhi reads it as three
independent ADC buttons (the same pattern as the ESP-BOX-Lite).

## Notes / calibration

- Display orientation, color inversion and the backlight PWM polarity were
  taken from the Passport BSP; verify on real hardware and adjust the
  `DISPLAY_*` macros in `config.h` if the image is rotated/inverted or the
  backlight is reversed.
- The ADC button voltage windows assume the Passport's external 10 kOhm
  pull-up. Re-measure with the Button page if thresholds drift.
- CW2017 presence is optional; without the chip the status bar shows no
  battery level and the board keeps working.

## 小智学堂 · Learning v1（2026-10-13）

本版本实现 `prototypes/learning-v1/index.html` 的完整示例交互：
4 张英语图片卡片（apple / banana / cat / dog）、4 轮听音选图、《静夜思》
逐句跟读和隐去文字的背诵提示。旧的 300 首诗/60 词源数据保留，但不作为新版页面内容。

### 按键与行为

- 启动显示学习首页：英语乐园、古诗乐园。
- 上下短按选择/换词/换句，中间短按确认或播放当前内容。
- 最后一个单词后按下进入听音找图；答错重听，答对后再按中间继续。
- 末句后按下选中“我会背”；确认后文字隐藏、画面提示，中间键读当前句。
- 长按中间 650 ms 返回；测验返回词卡，其余返回首页。
- 长按上/下 650 ms 音量 +/-10；每次短按释放单独处理，不设双击动作。
- 学习音频本地播放，不启动聊天，不做录音或发音评分。
- NVS `learning_v1/word`、`learning_v1/line` 保留位置；停止翻页 1.5 秒后保存，
  返回/隐藏时立即保存。读入损坏或越界值会限制到 0–3。
- 网络配置、激活、升级或外部对话使用系统画面；返回 Idle 后恢复原学习页。
  已联网设备本次启动约 5 秒完成激活；断网且处于 Starting/Idle 时本地学习可用。

### 实现与资源

| 文件 | 用途 |
| --- | --- |
| `learning_model.h` | 与 HTML 一致的纯 C++ 按键状态逻辑 |
| `learning_render.h/cc` | 240×320 LVGL 页面，供设备和桌面验证共用 |
| `learning_ui.h/cc` | 主任务调度、页面生命周期、NVS、可取消的本地音频工作任务 |
| `learning_media.h/cc` | 预生成的 RGB565+alpha 图案和 12 段 Ogg Opus 音频 |
| `learning_assets/` | 原始图案、音频及语音来源清单 |
| `learning_v1_*.c` | 生成的 12/16/22/34 px 字体，不手工修改 |
| `scripts/learning/` | 资源生成器、真实 LVGL 桌面渲染、原型对照和音频测试 |

音频由本机 Daniel（英语）/ Tingting（普通话）语音合成，16 kHz 单声道、
60 ms Opus 帧；不是人工录音。图案来自与 Mac 浏览器原型一致的 emoji 字形。
图案按使用尺寸预生成，避免 C3 无 PSRAM 时缩放、调色板展开的大块临时内存。
独立音频任务只向现有有界解码队列送包；换页/返回通过 generation 取消旧包，
主任务不等待整段播放。播放排队和输出都结束后才显示“轮到你啦”。

`AiPassportDisplay` 把系统主题更新固定到聊天屏，防止学习屏激活期间加载新字体，
造成旧聊天屏引用已释放的字体。此问题已通过实际启动日志定位并修复。

MCP `self.learning.get_current_card` 继续返回当前词卡、诗句或测验进度；
隐文背诵不泄露诗句，测验不通过工具泄露答案。

### 编译与验证

SDK：ESP-IDF v6.1；当前唯一型号：ESP32-C3 / AI Passport / 8 MB。

```sh
export PATH="/opt/homebrew/opt/python@3.12/libexec/bin:$PATH"
source "$HOME/.local/share/esp/esp-idf-v6.1/export.sh"
python scripts/build.py folotoy/ai-passport --name ai-passport --language zh-CN

cmake -S scripts/learning -B /tmp/learning-preview-build -G Ninja
cmake --build /tmp/learning-preview-build
python3 scripts/learning/compare_prototype.py /tmp/learning-preview-build/learning_preview
/tmp/learning-preview-build/check_audio
```

桌面预览从同一份 `learning_render.cc` 生成，不是重新画一份网页。
`learning_preview <输出目录>` 输出 RGB565；`preview_images.py <输出目录>` 生成 PNG。
资源更新：`generate_media.py` 需要 macOS 语音、Swift、Pillow、ffmpeg；
`generate_fonts.py <字体文件>` 需要 fontTools 和 lv_font_conv。

验证结果：
- 原始 HTML 与 C++ 状态/播放目标经过 20,032 次同序列操作，全部一致。
- 10 个实际 LVGL 页面已渲染检查；12 段音频通过固件 Ogg parser 与 ffmpeg 解码。
- ESP-IDF v6.1 编译、分区容量、格式、diff 检查通过。
- 通用脚本测试 81 项：17 failures、18 errors；在独立 HEAD 基线重跑完全复现。
  它们包含已移除型号的旧测试假设，不属于本次新增失败。
- 已通过 USB 实际刷入，esptool 写入哈希校验通过；设备联网进入 Idle，启动观察无重启。
- 用户确认：新版英语卡片显示正常、apple 发音正常。
- 其余实体键完整练习、长按音量、断电恢复及长时间运行仍需继续实机验收；
  状态测试与桌面渲染不等价于这些硬件检查。

本次交付：`dist/learning-v1-20261013/`。应用镜像 2,691,296 字节，
应用分区剩余 388,896 字节；资源分区沿用已校验的上一版。
本次只写应用 `0x20000` 和 OTA 初始化数据 `0xd000`，保留 NVS/Wi-Fi 配置。
合并镜像供完整恢复使用；从 `0x0` 写入合并镜像会覆盖其中填充的配置区域，
日常更新优先使用分区刷写。

旧 `dist/learning-foundation-20261013/` 是基础修复历史版本，已被本版取代。

## Learning v1.1 · 小智语音控制（2026-10-13）

此版补回小智入口，并让小智控制学习画面。首页显示“长按中间：和小智聊天”；
长按直接在学习首页启动小智，两张入口始终保留，不切到传统聊天屏。顶部显示“小智在听/小智在说”。
首页再次长按中间结束对话，继续显示学习首页。

新增普通 MCP 工具 `self.learning.control(action, index=-1)`：
- `english`：打开词卡，index 0–3 对应 apple/banana/cat/dog，省略则接着上次位置。
- `poem` / `recall`：打开《静夜思》跟读/隐藏文字背诵，index 0–3 指定诗句。
- `quiz`：开始四轮听音选图；`answer` 的 index=0/1 选择左/右图。
- `next` / `previous`：下一项/上一项，测验答对后才允许下一题。
- `play`：重读当前词句、背诵提示或重听问题；`home`：学习首页。

可说：“进入英语乐园”“学苹果”“下一个单词”“再读一次”“开始听音找图”
“左边”“学古诗”“读第二句”“开始背诗”“提示一下”“返回首页”。
工具会验证参数与当前页面；无效命令不改状态。响应 `say` 供小智朗读，
语音控制模式不同时播放本地音频，听/说期间保留学习画面。
实体键仍可使用；实体键要求本地播放时，先中止并关闭云端语音，再播放本地片段。
没有录音评分功能，也不会通过工具假装打开样例以外的单词或诗篇。

MCP 调用由现有 McpServer 排入 Application 主任务，未修改 WebSocket/MQTT 协议。
新增 `check_voice` 测试语义指令和完整四轮选图；`check_chat_transition.py`
抽取生产 Tick 方法，检查聊天页保持、云端听/说保留学习页、终止一次、关通道后才本地播放。
20,032 次原型内部学习状态对照仍通过；首页长按是 v1.1 新增扩展，由 UI 入口处理。

交付目录：`dist/learning-v1.1-voice-20261013/`；已实际刷入并验证启动和工具注册。
实机串口已观察到自然对话触发 `poem` 工具并打开《静夜思》第一句。
用户按“学英语”测试流程确认：“两个入口保留，而且语音能切换”。其余语音指令尚未逐项完成实机验收。
首次 v1.1 反馈指出传统聊天屏遮住两个入口，已修正为原屏启动语音，并加入生产 HandleButton 回归测试，验证启动/结束对话均保留首页。
