// LovyanGFX configuration of the Guition ESP32-4848S040: a 4-inch 480x480
// RGB panel (ST7701S, set up over a 3-wire SPI), a GT911 touch controller,
// an ESP32-S3 with 16MB flash and 8MB OPI PSRAM.
//
// Uses the modified Bus_RGB driver of these examples (double frame buffer
// with show(), _frame_buffer, pixel_size, offset), like the matouch boards.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device
{
public:

  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_ST7701_guition_esp32_4848S040 _panel_instance;
  lgfx::Light_PWM   _light_instance;
  lgfx::Touch_GT911 _touch_instance;

  LGFX(void)
  {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 480;
      cfg.panel_width   = 480;
      cfg.memory_height = 480;
      cfg.panel_height  = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }

    {
      // the ST7701S's set-up commands go over a software SPI on these pins
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;
      cfg.pin_cs   = GPIO_NUM_39;
      cfg.pin_sclk = GPIO_NUM_48;
      cfg.pin_mosi = GPIO_NUM_47;
      _panel_instance.config_detail(cfg);
    }

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_4;   // B0
      cfg.pin_d1  = GPIO_NUM_5;   // B1
      cfg.pin_d2  = GPIO_NUM_6;   // B2
      cfg.pin_d3  = GPIO_NUM_7;   // B3
      cfg.pin_d4  = GPIO_NUM_15;  // B4
      cfg.pin_d5  = GPIO_NUM_8;   // G0
      cfg.pin_d6  = GPIO_NUM_20;  // G1
      cfg.pin_d7  = GPIO_NUM_3;   // G2
      cfg.pin_d8  = GPIO_NUM_46;  // G3
      cfg.pin_d9  = GPIO_NUM_9;   // G4
      cfg.pin_d10 = GPIO_NUM_10;  // G5
      cfg.pin_d11 = GPIO_NUM_11;  // R0
      cfg.pin_d12 = GPIO_NUM_12;  // R1
      cfg.pin_d13 = GPIO_NUM_13;  // R2
      cfg.pin_d14 = GPIO_NUM_14;  // R3
      cfg.pin_d15 = GPIO_NUM_0;   // R4

      cfg.pin_henable = GPIO_NUM_18;
      cfg.pin_vsync   = GPIO_NUM_17;
      cfg.pin_hsync   = GPIO_NUM_16;
      cfg.pin_pclk    = GPIO_NUM_21;
      cfg.freq_write  = 12000000;

      cfg.hsync_polarity    = 1;
      cfg.hsync_front_porch = 10;
      cfg.hsync_pulse_width = 8;
      cfg.hsync_back_porch  = 50;
      cfg.vsync_polarity    = 1;
      cfg.vsync_front_porch = 10;
      cfg.vsync_pulse_width = 8;
      cfg.vsync_back_porch  = 20;
      cfg.pclk_idle_high    = 0;
      cfg.de_idle_high      = 0;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_38;
      cfg.invert = false;
      // the backlight driver follows a slow PWM only: at 44.1 kHz any level
      // under 255 left the screen black
      cfg.freq = 1000;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
    }
    _panel_instance.light(&_light_instance);

    {
      auto cfg = _touch_instance.config();
      cfg.x_min      = 0;
      cfg.x_max      = 479;
      cfg.y_min      = 0;
      cfg.y_max      = 479;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port   = I2C_NUM_1;
      cfg.pin_sda    = GPIO_NUM_19;
      cfg.pin_scl    = GPIO_NUM_45;
      cfg.pin_int    = GPIO_NUM_NC;
      cfg.pin_rst    = GPIO_NUM_NC;
      cfg.freq       = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};
