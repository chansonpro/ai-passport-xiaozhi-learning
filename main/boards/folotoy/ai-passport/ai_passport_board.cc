#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "cw2017_battery_monitor.h"
#include "display/lcd_display.h"
#include "learning_ui.h"
#include "mcp_server.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <button_adc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "AiPassport"

// Physical keys share one ADC pin through a resistor ladder (see config.h).
enum {
    kAdcButtonUp = 0,
    kAdcButtonDown,
    kAdcButtonOk,
    kAdcButtonNum,
};

// The common LCD theme code targets lv_screen_active(). This board has a
// second learning screen, so font/theme rebinding must target the chat screen
// before releasing the previous font owner (including background activation).
class AiPassportDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    void SetTheme(Theme* theme) override {
        DisplayLockGuard lock(this);
        if (!lock)
            return;
        auto* active = lv_screen_active();
        auto* chat = container_ ? lv_obj_get_screen(container_) : active;
        if (active != chat)
            lv_screen_load(chat);
        LcdDisplay::SetTheme(theme);
        if (active != chat)
            lv_screen_load(active);
    }
};

class LearningAdcButton : public Button {
public:
    explicit LearningAdcButton(const button_adc_config_t& config) : Button(nullptr) {
        const button_config_t timing = {.long_press_time = 650, .short_press_time = 0};
        ESP_ERROR_CHECK(iot_button_new_adc_device(&timing, &config, &button_handle_));
    }
};

class AiPassportBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button* adc_button_[kAdcButtonNum];
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    LcdDisplay* display_;
    Cw2017BatteryMonitor* battery_;
    LearningUi* learning_ui_ = nullptr;
    bool key_held_[kAdcButtonNum] = {};

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));

        // CW2017 fuel gauge is optional; a missing chip just disables battery UI.
        battery_ = new Cw2017BatteryMonitor(codec_i2c_bus_, BATTERY_CW2017_ADDR);
        battery_->Initialize();
    }

    void ChangeVolume(int delta) {
        auto codec = GetAudioCodec();
        auto volume = codec->output_volume() + delta;
        if (volume > 100) {
            volume = 100;
        }
        if (volume < 0) {
            volume = 0;
        }
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    }

    void ToggleChat() {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateStarting) {
            EnterWifiConfigMode();
            return;
        }
        app.ToggleChatState();
    }

    // 学习模式与对话模式的按键路由（运行在主任务）。
    // - 学习界面激活时：三键全部交给学习界面。
    // - Idle 且学习界面未激活：上/下键打开学习主页；OK 键开始对话；
    //   长按上/下键保留音量调节。
    void RouteLearningButton(int button_id, LearningUi::Press press) {
        auto& app = Application::GetInstance();
        if (press == LearningUi::Press::kLongPress && button_id != kAdcButtonOk) {
            ChangeVolume(button_id == kAdcButtonUp ? 10 : -10);
            return;
        }
        if (learning_ui_ != nullptr && learning_ui_->chat_mode() && !learning_ui_->active() &&
            press == LearningUi::Press::kLongPress && button_id == kAdcButtonOk) {
            learning_ui_->ReturnToLearning();
            return;
        }
        if (learning_ui_ != nullptr && learning_ui_->active()) {
            auto button = button_id == kAdcButtonUp     ? LearningUi::Button::kUp
                          : button_id == kAdcButtonDown ? LearningUi::Button::kDown
                                                        : LearningUi::Button::kOk;
            learning_ui_->HandleButton(button, press);
            return;
        }
        if (press == LearningUi::Press::kClick && button_id == kAdcButtonOk &&
            app.GetDeviceState() != kDeviceStateIdle) {
            ToggleChat();
            return;
        }
        if (app.GetDeviceState() != kDeviceStateIdle) {
            return;
        }
        switch (press) {
            case LearningUi::Press::kClick:
                if (button_id == kAdcButtonOk) {
                    ToggleChat();
                } else {
                    // 上/下键一键进入学习主页，方便孩子发现
                    learning_ui_->Show();
                }
                break;
            case LearningUi::Press::kLongPress:
                if (button_id == kAdcButtonUp) {
                    ChangeVolume(10);
                } else if (button_id == kAdcButtonDown) {
                    ChangeVolume(-10);
                } else {
                    learning_ui_->Show();
                }
                break;
            case LearningUi::Press::kDoubleClick:
            default:
                break;
        }
    }

    void InitializeLearningUi() {
        learning_ui_ = new LearningUi(display_);

        // MCP 工具：让小智知道孩子屏幕上正在看的内容（英语单词/听音找图），
        // 从而围绕当前卡片领读、讲解、陪练。
        McpServer::GetInstance().AddTool(
            "self.learning.get_current_card",
            "获取孩子当前正在屏幕上看的学习卡片（英语单词或听音找图题目）。当孩子让你领读、"
            "讲解或陪练屏幕上的内容，或孩子刚从学习界面发起对话时，调用此工具了解上下文。",
            PropertyList(), [this](const PropertyList& properties) -> ToolResult {
                return learning_ui_->GetCurrentCardJson();
            });
        McpServer::GetInstance().AddTool(
            "self.learning.control",
            "控制设备英语乐园的实际屏幕。孩子要求学英语/苹果/下一个词、听音找图时必须调用本工具，"
            "不能只口头答应。"
            "action: "
            "english=英语卡片；quiz=四轮听音选图；next/previous=下一项/上一项；play=重读或重听题目；"
            "answer=选择答案；home=学习首页。index默认-1；english的0..3对应apple/banana/cat/"
            "dog；answer的0是左图、1是右图，若孩子说物品名可先用get_current_"
            "card获取左右选项。仅支持这四个词，不假装打开其他内容。"
            "成功后请慢速简短朗读返回的say（英文用英文发音），不要同时请求本地播放；读完等待孩子。"
            "答错可重试，答对后等孩子说下一题。"
            "error表示操作没执行，应解释原因。学习画面在小智说话和听孩子说话时保持显示。",
            PropertyList({Property("action", kPropertyTypeString).SetMaxLength(16),
                          Property("index", kPropertyTypeInteger, -1, -1, 3)}),
            [this](const PropertyList& properties) -> ToolResult {
                return learning_ui_->ControlByVoice(properties["action"].value<std::string>(),
                                                    properties["index"].value<int>());
            });
    }

    void InitializeButtons() {
        for (int i = 0; i < kAdcButtonNum; i++) {
            adc_button_[i] = nullptr;
        }

        // One ADC1 unit shared by all three ladder keys. AdcButton reuses the
        // handle when adc_config.adc_handle is non-null, so the same physical
        // pin can decode several keys without "adc1 is already in use".
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT_1,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle_));

        button_adc_config_t adc_cfg = {};
        adc_cfg.adc_handle = &adc_handle_;
        adc_cfg.unit_id = ADC_UNIT_1;
        adc_cfg.adc_channel = ADC_CHANNEL_0;  // GPIO0

        adc_cfg.button_index = kAdcButtonUp;  // UP:   ~0 mV
        adc_cfg.min = BSP_ADC_BUTTON_UP_MIN;
        adc_cfg.max = BSP_ADC_BUTTON_UP_MAX;
        adc_button_[kAdcButtonUp] = new LearningAdcButton(adc_cfg);

        adc_cfg.button_index = kAdcButtonDown;  // DOWN: ~300 mV
        adc_cfg.min = BSP_ADC_BUTTON_DOWN_MIN;
        adc_cfg.max = BSP_ADC_BUTTON_DOWN_MAX;
        adc_button_[kAdcButtonDown] = new LearningAdcButton(adc_cfg);

        adc_cfg.button_index = kAdcButtonOk;  // OK:   ~595 mV
        adc_cfg.min = BSP_ADC_BUTTON_OK_MIN;
        adc_cfg.max = BSP_ADC_BUTTON_OK_MAX;
        adc_button_[kAdcButtonOk] = new LearningAdcButton(adc_cfg);

        // Button callbacks run on the button task; schedule all UI/audio
        // work onto the main task so LVGL and codec access stay on one thread.
        for (int i = 0; i < kAdcButtonNum; i++) {
            auto* btn = adc_button_[i];
            // Process every short release, including rapid repeated taps.
            // A 650 ms hold fires once and suppresses its release action.
            btn->OnPressDown([this, i]() { key_held_[i] = false; });
            btn->OnLongPress([this, i]() {
                key_held_[i] = true;
                Application::GetInstance().Schedule(
                    [this, i]() { RouteLearningButton(i, LearningUi::Press::kLongPress); });
            });
            btn->OnPressUp([this, i]() {
                if (key_held_[i])
                    return;
                Application::GetInstance().Schedule(
                    [this, i]() { RouteLearningButton(i, LearningUi::Press::kClick); });
            });
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;  // -1 -> software reset
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);

        // Panel-specific power/porch/gamma sequence for the ST7789P3 module
        // used on the Passport (copied from the original badge firmware).
        static const struct {
            uint8_t command;
            uint8_t data[16];
            uint8_t data_length;
            uint16_t delay_ms;
        } kSt7789P3InitCommands[] = {
            {0xB2, {0x05, 0x05, 0x00, 0x33, 0x33}, 5, 0},
            {0xB7, {0x35}, 1, 0},
            {0xBB, {0x21}, 1, 0},
            {0xC0, {0x2C}, 1, 0},
            {0xC2, {0x01}, 1, 0},
            {0xC3, {0x0B}, 1, 0},
            {0xC4, {0x20}, 1, 0},
            {0xC6, {0x0F}, 1, 0},
            {0xD0, {0xA7, 0xA1}, 2, 0},
            {0xD0, {0xA4, 0xA1}, 2, 0},
            {0xD6, {0xA1}, 1, 0},
            {0xE0,
             {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43, 0x49, 0x09, 0x16, 0x15, 0x26, 0x2B},
             14,
             0},
            {0xE1,
             {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44, 0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A},
             14,
             10},
        };
        for (const auto& cmd : kSt7789P3InitCommands) {
            esp_lcd_panel_io_tx_param(panel_io, cmd.command, cmd.data, cmd.data_length);
            if (cmd.delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(cmd.delay_ms));
            }
        }

        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new AiPassportDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                         DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                         DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
    AiPassportBoard() : display_(nullptr), battery_(nullptr) {
        InitializeCodecI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeLearningUi();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!battery_ || !battery_->IsPresent()) {
            return false;
        }
        int soc = battery_->GetBatteryLevel();
        if (soc < 0) {
            return false;
        }
        level = soc;
        // CW2017 reports no charge state and the Passport has no charge-detect
        // GPIO, so report a plain (discharging) reading.
        charging = false;
        discharging = true;
        return true;
    }
};

DECLARE_BOARD(AiPassportBoard);
