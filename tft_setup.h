#define USER_SETUP_INFO "ESP32-S3 2.0 inch 7-pin ST7789V2"
#define USER_SETUP_LOADED

#define ST7789_DRIVER

#define TFT_WIDTH  320
#define TFT_HEIGHT 240

#define TFT_MISO -1
#define TFT_MOSI 12  // white wire: screen SDA
#define TFT_SCLK 13  // blue wire: screen SCL
#define TFT_CS   10  // black wire: screen CS
#define TFT_DC   11  // red wire: screen DC
#define TFT_RST   8  // orange wire: screen RST

#define TFT_RGB_ORDER TFT_RGB
#define TFT_INVERSION_ON

#define SPI_FREQUENCY  10000000
#define SPI_READ_FREQUENCY  16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
