/* MIT License - Copyright (c) 2020 Francis Van Roie
   For full license information read the LICENSE file in the project folder */

#include "hasp_conf.h"

#include "lv_conf.h"
#include "lvgl.h"
#include "lv_drv_conf.h"

// Filesystem Driver
#include "lv_misc/lv_fs.h"
#include "lv_fs_if.h"

// Device Drivers
#include "dev/device.h"
#include "drv/hasp_drv_display.h"
#include "drv/hasp_drv_touch.h"

#include "hasp_debug.h"
#include "hasp_config.h"
#include "hasp_gui.h"
#include "hasp_oobe.h"

#include "hasp/hasp_dispatch.h"
#include "hasp/hasp.h"

#ifdef WINDOWS
#include "display/monitor.h"
#include "indev/mouse.h"
#endif

//#include "tpcal.h"

//#include "Ticker.h"

#if HASP_USE_PNGDECODE > 0
#include "png_decoder.h"
#endif

#define BACKLIGHT_CHANNEL 0 // pwm channel 0-15

#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
File pFileOut;
#endif

#define LVGL_TICK_PERIOD 20

#ifndef TFT_BCKL
#define TFT_BCKL -1 // No Backlight Control
#endif
#ifndef TFT_ROTATION
#define TFT_ROTATION 0
#endif
#ifndef INVERT_COLORS
#define INVERT_COLORS 0
#endif

// static void IRAM_ATTR lv_tick_handler(void);

gui_conf_t gui_settings = {.show_pointer   = false,
                           .backlight_pin  = TFT_BCKL,
                           .rotation       = TFT_ROTATION,
                           .invert_display = INVERT_COLORS,
                           .cal_data       = {0, 65535, 0, 65535, 0}};

// static int8_t guiDimLevel = 100;
// bool guiBacklightIsOn;

// #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
// static Ticker tick; /* timer for interrupt handler */
// #else
// static Ticker tick(lv_tick_handler, LVGL_TICK_PERIOD); // guiTickPeriod);
// #endif

/* **************************** GUI TICKER ************************************** */

/* Interrupt driven periodic handler */
// static void ICACHE_RAM_ATTR lv_tick_handler(void)
// {
//     lv_tick_inc(LVGL_TICK_PERIOD);
// }

void guiCalibrate(void)
{
#if TOUCH_DRIVER == 2046 && USE_TFT_ESPI > 0
#ifdef TOUCH_CS
    tft_espi_calibrate(gui_settings.cal_data);
#endif

    for(int i = 0; i < 5; i++) {
        Serial.print(gui_settings.cal_data[i]);
        if(i < 4) Serial.print(", ");
    }

    delay(500);
    lv_obj_invalidate(lv_disp_get_layer_sys(NULL));
#endif
}

void guiSetup(void)
{
    // Register logger to capture lvgl_init output
    LOG_TRACE(TAG_LVGL, F(D_SERVICE_STARTING));
#if LV_USE_LOG != 0 && defined(ARDUINO)
    lv_log_register_print_cb(debugLvglLogEvent);
#endif

    /* Create the Virtual Device Buffers */
#if defined(ARDUINO_ARCH_ESP32)

#ifdef USE_DMA_TO_TFT
    static lv_color_t *guiVdbBuffer1, *guiVdbBuffer2 = NULL;
    // DMA: len must be less than 32767
    const size_t guiVDBsize = 15 * 1024u; // 30 KBytes
    guiVdbBuffer1           = (lv_color_t*)heap_caps_calloc(guiVDBsize, sizeof(lv_color_t), MALLOC_CAP_DMA);
    // guiVdbBuffer2 = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * guiVDBsize,   MALLOC_CAP_DMA);
    // lv_disp_buf_init(&disp_buf, guiVdbBuffer1, guiVdbBuffer2, guiVDBsize);
#else
    static lv_color_t* guiVdbBuffer1;
    const size_t guiVDBsize = 16 * 1024u; // 32 KBytes

    if(0 && psramFound()) {
        guiVdbBuffer1 = (lv_color_t*)ps_calloc(guiVDBsize, sizeof(lv_color_t)); // too slow for VDB
    } else {
        guiVdbBuffer1 = (lv_color_t*)calloc(guiVDBsize, sizeof(lv_color_t));
    }

#endif

    // static lv_color_t * guiVdbBuffer2 = (lv_color_t *)malloc(sizeof(lv_color_t) * guiVDBsize);
    // lv_disp_buf_init(&disp_buf, guiVdbBuffer1, guiVdbBuffer2, guiVDBsize);

#elif defined(ARDUINO_ARCH_ESP8266)
    /* allocate on heap */
    // static lv_color_t guiVdbBuffer1[2 * 512u]; // 4 KBytes
    // size_t guiVDBsize = sizeof(guiVdbBuffer1) / sizeof(guiVdbBuffer1[0]);
    // lv_disp_buf_init(&disp_buf, guiVdbBuffer1, NULL, guiVDBsize);

    static lv_color_t* guiVdbBuffer1;
    const size_t guiVDBsize = 2 * 512u; // 4 KBytes * 2
    guiVdbBuffer1           = (lv_color_t*)malloc(sizeof(lv_color_t) * guiVDBsize);

#elif defined(WINDOWS)
    const size_t guiVDBsize = LV_HOR_RES_MAX * 10;
    static lv_color_t guiVdbBuffer1[guiVDBsize]; /*Declare a buffer for 10 lines*/

#else
    static lv_color_t guiVdbBuffer1[16 * 512u]; // 16 KBytes
    // static lv_color_t guiVdbBuffer2[16 * 512u]; // 16 KBytes
    size_t guiVDBsize = sizeof(guiVdbBuffer1) / sizeof(guiVdbBuffer1[0]);
    // lv_disp_buf_init(&disp_buf, guiVdbBuffer1, guiVdbBuffer2, guiVDBsize);
#endif

    /* Initialize lvgl */
    static lv_disp_buf_t disp_buf;
    if(guiVdbBuffer1 && guiVDBsize > 0) {
        lv_init();
        lv_disp_buf_init(&disp_buf, guiVdbBuffer1, NULL, guiVDBsize);
    } else {
        LOG_FATAL(TAG_GUI, F(D_ERROR_OUT_OF_MEMORY));
    }

    /* Initialize the display driver */
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.buffer = &disp_buf;

#if defined(WINDOWS)
    disp_drv.flush_cb = monitor_flush;
#else
    drv_display_init(&disp_drv, gui_settings.rotation,
                     gui_settings.invert_display); // Set display driver callback & rotation
#endif
    disp_drv.hor_res   = TFT_WIDTH;
    disp_drv.ver_res   = TFT_HEIGHT;
    lv_disp_t* display = lv_disp_drv_register(&disp_drv);

    switch(gui_settings.rotation) {
        case 1:
        case 3:
        case 5:
        case 7:
            lv_disp_set_rotation(display, LV_DISP_ROT_90);
            break;
        default:
            lv_disp_set_rotation(display, LV_DISP_ROT_NONE);
    }

        /* Initialize Filesystems */
#if LV_USE_FS_IF != 0
    _lv_fs_init();   // lvgl File System
    lv_fs_if_init(); // auxilary file system drivers
#endif

    /* Initialize PNG decoder */
#if HASP_USE_PNGDECODE > 0
    png_decoder_init();
#endif

#ifdef USE_DMA_TO_TFT
    LOG_VERBOSE(TAG_GUI, F("DMA        : ENABLED"));
#else
    LOG_VERBOSE(TAG_GUI, F("DMA        : DISABLED"));
#endif

    /* Setup Backlight Control Pin */
    haspDevice.set_backlight_pin(gui_settings.backlight_pin);
    //     if(gui_settings.backlight_pin >= 0) {
    //         LOG_VERBOSE(TAG_GUI, F("Backlight  : Pin %d"), gui_settings.backlight_pin);

    // #if defined(ARDUINO_ARCH_ESP32)
    //         ledcSetup(BACKLIGHT_CHANNEL, 20000, 12);
    //         ledcAttachPin(gui_settings.backlight_pin, BACKLIGHT_CHANNEL);
    // #elif defined(ARDUINO_ARCH_ESP8266)
    //         pinMode(gui_settings.backlight_pin, OUTPUT);
    // #endif
    //     }
    LOG_VERBOSE(TAG_GUI, F("Rotation   : %d"), gui_settings.rotation);

    LOG_VERBOSE(TAG_LVGL, F("Version    : %u.%u.%u %s"), LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                PSTR(LVGL_VERSION_INFO));

#ifdef LV_MEM_SIZE
    LOG_VERBOSE(TAG_LVGL, F("MEM size   : %d"), LV_MEM_SIZE);
#endif
    LOG_VERBOSE(TAG_LVGL, F("VFB size   : %d"), (size_t)sizeof(lv_color_t) * guiVDBsize);

    /* Initialize the touch pad */
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
#if defined(WINDOWS)
    indev_drv.read_cb = mouse_read;
#else
    indev_drv.read_cb = drv_touch_read;
#endif
    lv_indev_t* mouse_indev  = lv_indev_drv_register(&indev_drv);
    mouse_indev->driver.type = LV_INDEV_TYPE_POINTER;

    /*Set a cursor for the mouse*/
    if(gui_settings.show_pointer) {
        // lv_obj_t * label = lv_label_create(lv_layer_sys(), NULL);
        // lv_label_set_text(label, "<");
        // lv_indev_set_cursor(mouse_indev, label); // connect the object to the driver

        LOG_TRACE(TAG_GUI, F("Initialize Cursor"));
        lv_obj_t* cursor;
        lv_obj_t* mouse_layer = lv_disp_get_layer_sys(NULL); // default display

#if defined(ARDUINO_ARCH_ESP32)
        LV_IMG_DECLARE(mouse_cursor_icon);          /*Declare the image file.*/
        cursor = lv_img_create(mouse_layer, NULL);  /*Create an image object for the cursor */
        lv_img_set_src(cursor, &mouse_cursor_icon); /*Set the image source*/
#else
        cursor = lv_obj_create(mouse_layer, NULL); // show cursor object on every page
        lv_obj_set_size(cursor, 9, 9);
        lv_obj_set_style_local_radius(cursor, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
        lv_obj_set_style_local_bg_color(cursor, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_obj_set_style_local_bg_opa(cursor, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
#endif
        lv_indev_set_cursor(mouse_indev, cursor); /*Connect the image  object to the driver*/
    }

#if !defined(WINDOWS)
    drv_touch_init(gui_settings.rotation); // Touch driver
#endif

    /* Initialize Global progress bar*/
    lv_obj_user_data_t udata = (lv_obj_user_data_t){10, 0, 10};
    lv_obj_t* bar            = lv_bar_create(lv_layer_sys(), NULL);
    lv_obj_set_user_data(bar, udata);
    lv_obj_set_hidden(bar, true);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 10, LV_ANIM_OFF);
    lv_obj_set_size(bar, 200, 15);
    lv_obj_align(bar, lv_layer_sys(), LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_local_value_color(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_obj_set_style_local_value_align(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, LV_ALIGN_CENTER);
    lv_obj_set_style_local_value_ofs_y(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 20);
    lv_obj_set_style_local_value_font(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, LV_FONT_DEFAULT);
    lv_obj_set_style_local_bg_color(lv_layer_sys(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_bg_opa(lv_layer_sys(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_0);

    // guiStart(); // Ticker
}

void guiLoop(void)
{
#if defined(STM32F4xx)
    //  tick.update();
#endif

    lv_task_handler(); // process animations
#if !defined(WINDOWS)
    drv_touch_loop(); // update touch
#endif
}

void guiEverySecond(void)
{
    // nothing
}

void guiStart()
{
    /*Initialize the graphics library's tick*/
    // #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    //     tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);
    // #else
    //     tick.start();
    // #endif
}

void guiStop()
{
    /*Deinitialize the graphics library's tick*/
    // #if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
    //     tick.detach();
    // #else
    //     tick.stop();
    // #endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/*
bool guiGetBacklight()
{
    return guiBacklightIsOn;
}

void guiSetBacklight(bool lighton)
{
    guiBacklightIsOn = lighton;

    if(!lighton) hasp_enable_wakeup_touch();

    if(gui_settings.backlight_pin >= 0) {

#if defined(ARDUINO_ARCH_ESP32)
        ledcWrite(BACKLIGHT_CHANNEL, lighton ? map(guiDimLevel, 0, 100, 0, 4095) : 0); // ledChannel and value
#else
        analogWrite(gui_settings.backlight_pin, lighton ? map(guiDimLevel, 0, 100, 0, 1023) : 0);
#endif

    } else {
        guiBacklightIsOn = true;
    }
}

void guiSetDim(int8_t level)
{
    if(gui_settings.backlight_pin >= 0) {
        guiDimLevel = level >= 0 ? level : 0;
        guiDimLevel = guiDimLevel <= 100 ? guiDimLevel : 100;

        if(guiBacklightIsOn) { // The backlight is ON
#if defined(ARDUINO_ARCH_ESP32)
            ledcWrite(BACKLIGHT_CHANNEL, map(guiDimLevel, 0, 100, 0, 4095)); // ledChannel and value
#else
            analogWrite(gui_settings.backlight_pin, map(guiDimLevel, 0, 100, 0, 1023));
#endif
        }

    } else {
        guiDimLevel = -1;
    }
}

int8_t guiGetDim()
{
    return guiDimLevel;
}
*/

////////////////////////////////////////////////////////////////////////////////////////////////////
#if HASP_USE_CONFIG > 0
bool guiGetConfig(const JsonObject& settings)
{
    bool changed = false;
    uint16_t guiSleepTime1;
    uint16_t guiSleepTime2;
    hasp_get_sleep_time(guiSleepTime1, guiSleepTime2);

    // if(guiTickPeriod != settings[FPSTR(FP_GUI_TICKPERIOD)].as<uint8_t>()) changed = true;
    // settings[FPSTR(FP_GUI_TICKPERIOD)] = guiTickPeriod;

    if(guiSleepTime1 != settings[FPSTR(FP_GUI_IDLEPERIOD1)].as<uint16_t>()) changed = true;
    settings[FPSTR(FP_GUI_IDLEPERIOD1)] = guiSleepTime1;

    if(guiSleepTime2 != settings[FPSTR(FP_GUI_IDLEPERIOD2)].as<uint16_t>()) changed = true;
    settings[FPSTR(FP_GUI_IDLEPERIOD2)] = guiSleepTime2;

    if(gui_settings.backlight_pin != settings[FPSTR(FP_GUI_BACKLIGHTPIN)].as<int8_t>()) changed = true;
    settings[FPSTR(FP_GUI_BACKLIGHTPIN)] = gui_settings.backlight_pin;

    if(gui_settings.rotation != settings[FPSTR(FP_GUI_ROTATION)].as<uint8_t>()) changed = true;
    settings[FPSTR(FP_GUI_ROTATION)] = gui_settings.rotation;

    if(gui_settings.show_pointer != settings[FPSTR(FP_GUI_POINTER)].as<bool>()) changed = true;
    settings[FPSTR(FP_GUI_POINTER)] = gui_settings.show_pointer;

    if(gui_settings.invert_display != settings[FPSTR(FP_GUI_INVERT)].as<bool>()) changed = true;
    settings[FPSTR(FP_GUI_INVERT)] = gui_settings.invert_display;

    /* Check CalData array has changed */
    JsonArray array = settings[FPSTR(FP_GUI_CALIBRATION)].as<JsonArray>();
    uint8_t i       = 0;
    for(JsonVariant v : array) {
        LOG_VERBOSE(TAG_GUI, F("GUI CONF: %d: %d <=> %d"), i, gui_settings.cal_data[i], v.as<uint16_t>());
        if(i < 5) {
            if(gui_settings.cal_data[i] != v.as<uint16_t>()) changed = true;
            v.set(gui_settings.cal_data[i]);
        } else {
            changed = true;

#if TOUCH_DRIVER == 2046 && USE_TFT_ESPI > 0 && defined(TOUCH_CS)
            tft_espi_set_touch(gui_settings.cal_data);
#endif
        }
        i++;
    }

    /* Build new CalData array if the count is not correct */
    if(i != 5) {
        array = settings[FPSTR(FP_GUI_CALIBRATION)].to<JsonArray>(); // Clear JsonArray
        for(int i = 0; i < 5; i++) {
            array.add(gui_settings.cal_data[i]);
        }
        changed = true;

#if TOUCH_DRIVER == 2046 && USE_TFT_ESPI > 0 && defined(TOUCH_CS)
        tft_espi_set_touch(gui_settings.cal_data);
#endif
    }

    if(changed) configOutput(settings, TAG_GUI);
    return changed;
}

/** Set GUI Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool guiSetConfig(const JsonObject& settings)
{
    configOutput(settings, TAG_GUI);
    bool changed = false;
    uint16_t guiSleepTime1;
    uint16_t guiSleepTime2;

    hasp_get_sleep_time(guiSleepTime1, guiSleepTime2);

    // changed |= configSet(guiTickPeriod, settings[FPSTR(FP_GUI_TICKPERIOD)], F("guiTickPeriod"));
    changed |= configSet(gui_settings.backlight_pin, settings[FPSTR(FP_GUI_BACKLIGHTPIN)], F("guiBacklightPin"));
    changed |= configSet(guiSleepTime1, settings[FPSTR(FP_GUI_IDLEPERIOD1)], F("guiSleepTime1"));
    changed |= configSet(guiSleepTime2, settings[FPSTR(FP_GUI_IDLEPERIOD2)], F("guiSleepTime2"));
    changed |= configSet(gui_settings.rotation, settings[FPSTR(FP_GUI_ROTATION)], F("gui_settings.rotation"));
    changed |= configSet(gui_settings.invert_display, settings[FPSTR(FP_GUI_INVERT)], F("guiInvertDisplay"));

    hasp_set_sleep_time(guiSleepTime1, guiSleepTime2);

    if(!settings[FPSTR(FP_GUI_POINTER)].isNull()) {
        if(gui_settings.show_pointer != settings[FPSTR(FP_GUI_POINTER)].as<bool>()) {
            LOG_VERBOSE(TAG_GUI, F("guiShowPointer set"));
        }
        changed |= gui_settings.show_pointer != settings[FPSTR(FP_GUI_POINTER)].as<bool>();

        gui_settings.show_pointer = settings[FPSTR(FP_GUI_POINTER)].as<bool>();
    }

    if(!settings[FPSTR(FP_GUI_CALIBRATION)].isNull()) {
        bool status = false;
        int i       = 0;

        JsonArray array = settings[FPSTR(FP_GUI_CALIBRATION)].as<JsonArray>();
        for(JsonVariant v : array) {
            if(i < 5) {
                if(gui_settings.cal_data[i] != v.as<uint16_t>()) status = true;
                gui_settings.cal_data[i] = v.as<uint16_t>();
            }
            i++;
        }

        if(gui_settings.cal_data[0] != 0 || gui_settings.cal_data[1] != 65535 || gui_settings.cal_data[2] != 0 ||
           gui_settings.cal_data[3] != 65535) {
            LOG_VERBOSE(TAG_GUI, F("calData set [%u, %u, %u, %u, %u]"), gui_settings.cal_data[0],
                        gui_settings.cal_data[1], gui_settings.cal_data[2], gui_settings.cal_data[3],
                        gui_settings.cal_data[4]);
            oobeSetAutoCalibrate(false);
        } else {
            LOG_TRACE(TAG_GUI, F("First Touch Calibration enabled"));
            oobeSetAutoCalibrate(true);
        }

#if TOUCH_DRIVER == 2046 && USE_TFT_ESPI > 0 && defined(TOUCH_CS)
        if(status) tft_espi_set_touch(gui_settings.cal_data);
#endif
        changed |= status;
    }

    return changed;
}
#endif // HASP_USE_CONFIG

/* **************************** SCREENSHOTS ************************************** */
#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0 || HASP_USE_HTTP > 0

static void guiSetBmpHeader(uint8_t* buffer_p, int32_t data)
{
    *buffer_p++ = data & 0xFF;
    *buffer_p++ = (data >> 8) & 0xFF;
    *buffer_p++ = (data >> 16) & 0xFF;
    *buffer_p++ = (data >> 24) & 0xFF;
}

/** Send Bitmap Header.
 *
 * Sends a header in BMP format for the size of the screen.
 *
 * @note: send header before refreshing the whole screen
 *
 **/
static void gui_get_bitmap_header(uint8_t* buffer, size_t bufsize)
{
    memset(buffer, 0, bufsize);

    lv_disp_t* disp = lv_disp_get_default();
    buffer[0]       = 0x42; // B
    buffer[1]       = 0x4D; // M

    buffer[10 + 0] = 122;      // full header size
    buffer[14 + 0] = 122 - 14; // dib header size
    buffer[26 + 0] = 1;        // number of color planes
    buffer[28 + 0] = 16;       // or 24, bbp
    buffer[30 + 0] = 3;        // compression, 0 = RGB / 3 = RGBA

    // The refresh draws the active screen only, so we need the dimensions of the active screen
    // This could in be diferent from the display driver width/height if the screen has been resized
    lv_obj_t* scr = lv_disp_get_scr_act(NULL);

    // file size
    guiSetBmpHeader(&buffer[2], 122 + disp->driver.hor_res * disp->driver.ver_res * buffer[28] / 8);
    // horizontal resolution
    guiSetBmpHeader(&buffer[18], lv_obj_get_width(scr));
    // guiSetBmpHeader(&buffer[18], disp->driver.hor_res);
    // vertical resolution
    guiSetBmpHeader(&buffer[22], -lv_obj_get_height(scr));
    // guiSetBmpHeader(&buffer[22], -disp->driver.ver_res);
    // bitmap size
    guiSetBmpHeader(&buffer[34], lv_obj_get_width(scr) * lv_obj_get_height(scr) * buffer[28 + 0] / 8);
    // guiSetBmpHeader(&buffer[34], disp->driver.hor_res * disp->driver.ver_res * buffer[28 + 0] / 8);
    // horizontal pixels per meter
    guiSetBmpHeader(&buffer[38], 2836);
    // vertical pixels per meter
    guiSetBmpHeader(&buffer[42], 2836);
    // R: 1111 1000 | 0000 0000
    guiSetBmpHeader(&buffer[54], 0xF800); // Red bitmask
    // G: 0000 0111 | 1110 0000
    guiSetBmpHeader(&buffer[58], 0x07E0); // Green bitmask
    // B: 0000 0000 | 0001 1111
    guiSetBmpHeader(&buffer[62], 0x001F); // Blue bitmask
    // A: 0000 0000 | 0000 0000
    guiSetBmpHeader(&buffer[66], 0x0000); // No Aplpha Mask

    // "Win
    buffer[70 + 3] = 0x57;
    buffer[70 + 2] = 0x69;
    buffer[70 + 1] = 0x6E;
    buffer[70 + 0] = 0x20;
}

void gui_flush_not_complete()
{
    LOG_WARNING(TAG_GUI, F("Pixelbuffer not completely sent"));
}
#endif // HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0 || HASP_USE_HTTP > 0

#if HASP_USE_SPIFFS > 0 || HASP_USE_LITTLEFS > 0
/* Flush VDB bytes to a file */
static void gui_screenshot_to_file(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
    size_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1); /* Number of pixels */
    len *= sizeof(lv_color_t);                                          /* Number of bytes */
    size_t res = pFileOut.write((uint8_t*)color_p, len);
    if(res != len) gui_flush_not_complete();
    drv_display_flush_cb(disp, area, color_p); // indirect callback to flush screenshot data to the screen
}

/** Take Screenshot.
 *
 * Flush buffer into a binary file.
 *
 * @note: data pixel should be formated to uint16_t RGB. Set by Bitmap header.
 *
 * @param[in] pFileName   Output binary file name.
 *
 **/
void guiTakeScreenshot(const char* pFileName)
{
    uint8_t buffer[128];
    gui_get_bitmap_header(buffer, sizeof(buffer));

    pFileOut = HASP_FS.open(pFileName, "w");
    if(pFileOut) {

        size_t len = pFileOut.write(buffer, 122);
        if(len == 122) {
            LOG_VERBOSE(TAG_GUI, F("Bitmap header written"));

            /* Refresh screen to screenshot callback */
            lv_disp_t* disp = lv_disp_get_default();
            void (*flush_cb)(struct _disp_drv_t * disp_drv, const lv_area_t* area, lv_color_t* color_p);
            flush_cb              = disp->driver.flush_cb; /* store callback */
            disp->driver.flush_cb = gui_screenshot_to_file;

            lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL);                /* Will call our disp_drv.disp_flush function */
            disp->driver.flush_cb = flush_cb; /* restore callback */

            LOG_VERBOSE(TAG_GUI, F("Bitmap data flushed to %s"), pFileName);

        } else {
            LOG_ERROR(TAG_GUI, F("Data written does not match header size"));
        }
        pFileOut.close();

    } else {
        LOG_WARNING(TAG_GUI, F("%s cannot be opened"), pFileName);
    }
}
#endif

#if HASP_USE_HTTP > 0
/* Flush VDB bytes to a webclient */
static void gui_screenshot_to_http(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p)
{
    size_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1); /* Number of pixels */
    len *= sizeof(lv_color_t);                                          /* Number of bytes */
    size_t res = httpClientWrite((uint8_t*)color_p, len);
    if(res != len) gui_flush_not_complete();
    drv_display_flush_cb(disp, area, color_p);
}

/** Take Screenshot.
 *
 * Flush buffer into a http client.
 *
 * @note: data pixel should be formated to uint16_t RGB. Set by Bitmap header.
 *
 **/
void guiTakeScreenshot()
{
    uint8_t buffer[128];
    gui_get_bitmap_header(buffer, sizeof(buffer));

    if(httpClientWrite(buffer, 122) == 122) {
        LOG_VERBOSE(TAG_GUI, F("Bitmap header sent"));

        /* Refresh screen to screenshot callback */
        lv_disp_t* disp = lv_disp_get_default();
        void (*flush_cb)(struct _disp_drv_t * disp_drv, const lv_area_t* area, lv_color_t* color_p);
        flush_cb              = disp->driver.flush_cb; /* store callback */
        disp->driver.flush_cb = gui_screenshot_to_http;
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);                /* Will call our disp_drv.disp_flush function */
        disp->driver.flush_cb = flush_cb; /* restore callback */

        LOG_VERBOSE(TAG_GUI, F("Bitmap data flushed to webclient"));
    } else {
        LOG_ERROR(TAG_GUI, F("Data sent does not match header size"));
    }
}
#endif