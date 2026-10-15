"""Small synthetic projects for build-parser tests, independent of shipped boards.

Historical names below exercise directory/symbol aliases retained by build.py;
these files are test input, not supported hardware definitions.
"""
import contextlib
import json
import os
import tempfile
from pathlib import Path
from unittest import mock


@contextlib.contextmanager
def board_project(build):
    rows = [
        ('espressif/esp32-s3-box-3', 'ESP32_S3_BOX_3', 'esp32s3'),
        ('alientek/atk-dnesp32s3', 'ATK_DNESP32S3', 'esp32s3'),
        ('alientek/atk-dnesp32s3m-wifi', 'ATK_DNESP32S3M_WIFI', 'esp32s3'),
        ('alientek/atk-dnesp32s3m-4g', 'ATK_DNESP32S3M_4G', 'esp32s3'),
        ('bread-compact-wifi', 'BREAD_COMPACT_WIFI', 'esp32s3'),
        ('espressif/s31', 'ESP32_S31_FUNCTION_COREBOARD_1', 'esp32s31'),
        ('m5stack/core-s3', 'M5STACK_CORE_S3', 'esp32s3'),
        ('lilygo/t-cameraplus-s3', 'LILYGO_T_CAMERAPLUS_S3_V1_1', 'esp32s3'),
        ('lilygo/t-cameraplus-s3', 'LILYGO_T_CAMERAPLUS_S3_V1_2', 'esp32s3'),
        ('m5stack/cardputer-adv', 'M5STACK_CARDPUTER_ADV', 'esp32s3'),
        ('xorigin/aipi-lite', 'XORIGIN_AIPI_LITE', 'esp32s3'),
        ('kevin/box-2', 'KEVIN_BOX_2', 'esp32s3'),
        ('labplus/ledong-v2', 'LABPLUS_LEDONG_V2', 'esp32s3'),
        ('lckfb/szpi-esp32s3', 'LICHUANG_DEV_S3', 'esp32s3'),
        ('lckfb/szpi-esp32c3', 'LICHUANG_DEV_C3', 'esp32c3'),
        ('wdmomo/esp32-cgc', 'WDMOMO_CGC', 'esp32s3'),
        ('wdmomo/esp32-cgc-144', 'WDMOMO_CGC_144', 'esp32s3'),
        ('magiclick/c3', 'MAGICLICK_C3', 'esp32c3'),
        ('xmini/c3', 'XMINI_C3', 'esp32c3'),
        ('bread-compact-esp32-lcd', 'BREAD_COMPACT_ESP32_LCD', 'esp32'),
        ('espressif/esp-vocat', 'ESP_VOCAT', 'esp32s3'),
        ('espressif/esp32-s3-korvo-2-v3.0', 'ESP32_S3_KORVO_2_V3_0', 'esp32s3'),
        ('otto-robot', 'OTTO_ROBOT', 'esp32s3'),
    ]
    previous = Path.cwd()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / 'main/boards').mkdir(parents=True)
        kconfig = []
        cmake = []
        for board, symbol, target in rows:
            label = ('Alientek ATK-DNESP32S3 Development Board (正点原子)'
                     if symbol == 'ATK_DNESP32S3' else symbol)
            kconfig.append(f'config BOARD_TYPE_{symbol}\n    bool "{label}"\n    depends on IDF_TARGET_{target.upper()}\n')
            cmake.append(f'if(CONFIG_BOARD_TYPE_{symbol})\n    set(BOARD_DIR "{board}")\nendif()\n')
            board_dir = root / 'main/boards' / board
            board_dir.mkdir(parents=True, exist_ok=True)
            variant = {'name': board.replace('/', '-'), 'sdkconfig_append': []}
            source = ''
            if symbol == 'BREAD_COMPACT_ESP32_LCD':
                source = 'SpiLcdDisplay display;'
            if symbol == 'ESP_VOCAT':
                source = 'SpiLcdDisplay display; auto camera = new EspVideo();'
                variant['sdkconfig_append'] = ['CONFIG_USE_EMOTE_MESSAGE_STYLE=y', 'CONFIG_FLASH_EXPRESSION_ASSETS=y']
            if symbol == 'ESP32_S3_KORVO_2_V3_0':
                source = 'auto camera = new Esp32Camera();'
                variant['build_options'] = {'camera_hmirror': False, 'camera_vflip': True}
            if symbol == 'OTTO_ROBOT':
                source = 'auto camera = new (std::nothrow) Esp32Camera();'
                variant['build_options'] = {'camera_hmirror': True, 'camera_vflip': True}
            cfg = {'target': target, 'type': board.split('/')[-1], 'builds': [variant]}
            if '/' in board:
                cfg['manufacturer'] = board.split('/')[0]
            (board_dir / 'config.json').write_text(json.dumps(cfg), encoding='utf-8')
            (board_dir / 'board.cc').write_text(source, encoding='utf-8')
        kconfig.append('''
choice DISPLAY_OLED_TYPE
    depends on BOARD_TYPE_TEST_OLED
    default OLED_SSD1306_128X64
    config OLED_SSD1306_128X64
        bool "SSD1306"
    config OLED_SH1106_128X64
        bool "SH1106"
endchoice
choice DISPLAY_LCD_TYPE
    depends on BOARD_TYPE_BREAD_COMPACT_ESP32_LCD
    default LCD_ST7789_240X240_7PIN
    config LCD_ST7789_240X240_7PIN
        bool "240x240"
    config LCD_ST7789_240X320
        bool "240x320"
    config LCD_CUSTOM
        bool "Custom"
endchoice
choice DISPLAY_STYLE
    default USE_DEFAULT_MESSAGE_STYLE
    config USE_DEFAULT_MESSAGE_STYLE
        bool "Default"
    config USE_WECHAT_MESSAGE_STYLE
        bool "WeChat"
    config USE_EMOTE_MESSAGE_STYLE
        bool "Emote"
        depends on BOARD_TYPE_ESP_VOCAT
endchoice
config USE_DEVICE_AEC
    bool "AEC"
    depends on BOARD_TYPE_TEST_AEC
''')
        (root / 'main/Kconfig.projbuild').write_text('\n'.join(kconfig), encoding='utf-8')
        (root / 'main/CMakeLists.txt').write_text('\n'.join(cmake), encoding='utf-8')
        try:
            os.chdir(root)
            with mock.patch.object(build, '_BOARDS_DIR', root / 'main/boards'):
                yield root
        finally:
            os.chdir(previous)
