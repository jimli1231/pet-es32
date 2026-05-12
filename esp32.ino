// ESP32-S3 + 2.0 inch ST7789V2 cute pixel cat UI replica
// Direct SPI RGB666, no TFT_eSPI. Touch, serial, or Wi-Fi switches expressions.
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Wire.h>
#include <math.h>

constexpr int TTP223_PIN = 4;
constexpr int BUZZER_PIN = 16;         // 有源蜂鸣器 I/O 接 GPIO16，低电平响
constexpr int SU03T_TX_PIN = 17;       // ESP32 TX -> SU-03T RX
constexpr int SU03T_RX_PIN = 18;       // ESP32 RX <- SU-03T TX
constexpr uint32_t SU03T_BAUD = 9600;

constexpr int SOIL_PIN = 2;            // HW-390 AOUT 接 GPIO2 (ADC1_CH1)
constexpr int SOIL_WET_THRESHOLD = 2400;  // ADC 低于这个值就算湿润 (越湿值越低，按串口实测调)
constexpr uint32_t SOIL_CHECK_MS = 500;
constexpr uint32_t RAIN_FRAME_MS = 180;
constexpr int SHT30_SDA_PIN = 6;
constexpr int SHT30_SCL_PIN = 7;
constexpr uint8_t SHT30_ADDR = 0x44;
constexpr uint32_t SHT30_CHECK_MS = 2000;

constexpr int TFT_BL = 5;
constexpr int TFT_CS = 10;
constexpr int TFT_DC = 11;
constexpr int TFT_RST = 8;
constexpr int TFT_MOSI = 12;
constexpr int TFT_SCLK = 13;

constexpr int TFT_WIDTH = 320;
constexpr int TFT_HEIGHT = 240;

SPISettings tftSpiSettings(20000000, MSBFIRST, SPI_MODE0);

constexpr char WIFI_AP_SSID[] = "PixelCat-S3";
constexpr char WIFI_AP_PASS[] = "cat12345";

WebServer g_server(80);
HardwareSerial g_su03t(1);

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint8_t redOf(uint32_t c) { return (c >> 16) & 0xFF; }
uint8_t greenOf(uint32_t c) { return (c >> 8) & 0xFF; }
uint8_t blueOf(uint32_t c) { return c & 0xFF; }

constexpr uint32_t C_BG = rgb(255, 210, 230);
constexpr uint32_t C_BG_HAPPY = rgb(255, 209, 228);
constexpr uint32_t C_SHADOW = rgb(190, 216, 222);
constexpr uint32_t C_FUR = rgb(255, 242, 222);
constexpr uint32_t C_FUR_DARK = rgb(219, 132, 172);
constexpr uint32_t C_INNER_EAR = rgb(255, 142, 160);
constexpr uint32_t C_INK = rgb(12, 12, 12);
constexpr uint32_t C_WHITE = rgb(255, 255, 255);
constexpr uint32_t C_BLUSH = rgb(255, 132, 160);
constexpr uint32_t C_NOSE = rgb(235, 111, 133);
constexpr uint32_t C_HEART = rgb(239, 59, 101);
constexpr uint32_t C_SPARK = rgb(255, 206, 74);
constexpr uint32_t C_GREEN = rgb(35, 211, 76);
constexpr uint32_t C_BLUE = rgb(69, 126, 231);
constexpr uint32_t C_CYAN = rgb(64, 201, 235);

enum Expression {
  EX_DEFAULT,
  EX_HAPPY,
  EX_COQUETTISH,
  EX_ANGRY,
  EX_SLEEPY,
  EX_HUNGRY,
  EX_SHY,
  EX_DIZZY,
  EX_COUNT
};

bool g_raining = false;
uint32_t g_rainFrame = 0;
int g_expression = 0;
bool g_expressionDirty = false;

constexpr uint8_t BUZZER_ON = LOW;
constexpr uint8_t BUZZER_OFF = HIGH;
constexpr uint16_t SPEECH_PATTERNS[EX_COUNT][12] = {
  {90, 70, 90, 140, 140, 0},                 // 默认：短短长
  {70, 50, 70, 50, 170, 0},                  // 开心：轻快
  {55, 65, 55, 65, 55, 160, 180, 0},         // 撒娇：多次短音
  {220, 80, 220, 0},                         // 生气：两声重音
  {300, 140, 120, 0},                        // 困了：长短
  {120, 70, 120, 70, 120, 70, 260, 0},       // 饿了：连续催促
  {60, 90, 170, 120, 60, 0},                 // 害羞：轻声
  {80, 55, 80, 55, 80, 55, 80, 55, 220, 0}   // 晕晕：密集
};
bool g_speechActive = false;
int g_speechExpression = 0;
uint8_t g_speechStep = 0;
uint32_t g_speechNextMs = 0;
String g_su03tLine;
String g_usbLine;
String g_lastVoiceCommand = "boot";
bool g_sht30Ok = false;
int g_temperatureDeciC = 0;
int g_humidityPercent = 0;

const char *SU03T_EXPRESSION_COMMANDS[EX_COUNT] = {
  "text1",
  "text2",
  "text3",
  "text4",
  "text7",
  "text8",
  "text11",
  "text12"
};

int dist2(int x0, int y0, int x1, int y1) {
  int dx = x0 - x1;
  int dy = y0 - y1;
  return dx * dx + dy * dy;
}

bool inCircle(int x, int y, int cx, int cy, int r) {
  return dist2(x, y, cx, cy) <= r * r;
}

bool inEllipse(int x, int y, int cx, int cy, int rx, int ry) {
  long dx = x - cx;
  long dy = y - cy;
  return dx * dx * ry * ry + dy * dy * rx * rx <= (long)rx * rx * ry * ry;
}

bool sameSide(int px, int py, int ax, int ay, int bx, int by, int cx, int cy) {
  long cross1 = (long)(bx - ax) * (py - ay) - (long)(by - ay) * (px - ax);
  long cross2 = (long)(bx - ax) * (cy - ay) - (long)(by - ay) * (cx - ax);
  return (cross1 >= 0 && cross2 >= 0) || (cross1 <= 0 && cross2 <= 0);
}

bool inTriangle(int px, int py, int ax, int ay, int bx, int by, int cx, int cy) {
  return sameSide(px, py, ax, ay, bx, by, cx, cy) &&
         sameSide(px, py, bx, by, cx, cy, ax, ay) &&
         sameSide(px, py, cx, cy, ax, ay, bx, by);
}

bool nearLine(int px, int py, int x1, int y1, int x2, int y2, int radius) {
  long dx = x2 - x1;
  long dy = y2 - y1;
  long len2 = dx * dx + dy * dy;
  if (len2 == 0) return inCircle(px, py, x1, y1, radius);
  float t = ((px - x1) * dx + (py - y1) * dy) / (float)len2;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  int cx = x1 + dx * t;
  int cy = y1 + dy * t;
  return dist2(px, py, cx, cy) <= radius * radius;
}

bool inHeart(int x, int y, int cx, int cy, int s) {
  return inCircle(x, y, cx - s, cy - s, s) ||
         inCircle(x, y, cx + s, cy - s, s) ||
         inTriangle(x, y, cx - 2 * s, cy - s, cx + 2 * s, cy - s, cx, cy + 3 * s);
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

bool digitPixel(int x, int y, int ox, int oy, int digit) {
  int lx = x - ox;
  int ly = y - oy;
  if (lx < 0 || lx >= 18 || ly < 0 || ly >= 30) return false;

  bool a = false, b = false, c = false, d = false, e = false, f = false, g = false;
  switch (digit) {
    case 0: a = b = c = d = e = f = true; break;
    case 1: b = c = true; break;
    case 2: a = b = d = e = g = true; break;
    case 3: a = b = c = d = g = true; break;
    case 4: b = c = f = g = true; break;
    case 5: a = c = d = f = g = true; break;
    case 6: a = c = d = e = f = g = true; break;
    case 7: a = b = c = true; break;
    case 8: a = b = c = d = e = f = g = true; break;
    case 9: a = b = c = d = f = g = true; break;
  }

  if (a && inRect(lx, ly, 3, 0, 12, 4)) return true;
  if (b && inRect(lx, ly, 14, 3, 4, 11)) return true;
  if (c && inRect(lx, ly, 14, 16, 4, 11)) return true;
  if (d && inRect(lx, ly, 3, 26, 12, 4)) return true;
  if (e && inRect(lx, ly, 0, 16, 4, 11)) return true;
  if (f && inRect(lx, ly, 0, 3, 4, 11)) return true;
  if (g && inRect(lx, ly, 3, 13, 12, 4)) return true;
  return false;
}

uint8_t glyph5x7(char ch, int row) {
  static const uint8_t digits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}
  };
  static const uint8_t cGlyph[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
  static const uint8_t hGlyph[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
  if (ch >= '0' && ch <= '9') return digits[ch - '0'][row];
  if (ch == 'C') return cGlyph[row];
  if (ch == 'H') return hGlyph[row];
  if (ch == '-') return row == 3 ? 0x1F : 0x00;
  if (ch == '.') return row == 6 ? 0x04 : 0x00;
  if (ch == '%') {
    static const uint8_t percentGlyph[7] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03};
    return percentGlyph[row];
  }
  return 0;
}

bool text5x7Pixel(int x, int y, int ox, int oy, const char *text, int scale) {
  if (y < oy) return false;
  int ly = y - oy;
  int row = ly / scale;
  if (row < 0 || row >= 7) return false;
  int cursor = ox;
  for (const char *p = text; *p; p++) {
    char ch = *p;
    int width = (ch == ' ') ? 3 : 5;
    int cellW = (width + 1) * scale;
    if (x >= cursor && x < cursor + cellW) {
      if (ch == ' ') return false;
      int col = (x - cursor) / scale;
      if (col < 0 || col >= 5) return false;
      return (glyph5x7(ch, row) & (1 << (4 - col))) != 0;
    }
    cursor += cellW;
  }
  return false;
}

void buildSht30Text(char *out, size_t outSize) {
  if (!g_sht30Ok) {
    snprintf(out, outSize, "--.-C --%%");
    return;
  }
  int temp = g_temperatureDeciC;
  char sign = '\0';
  if (temp < 0) {
    sign = '-';
    temp = -temp;
  }
  if (sign) {
    snprintf(out, outSize, "-%d.%dC %d%%", temp / 10, temp % 10, g_humidityPercent);
  } else {
    snprintf(out, outSize, "%d.%dC %d%%", temp / 10, temp % 10, g_humidityPercent);
  }
}

bool sht30TextPixel(int x, int y) {
  char text[18];
  buildSht30Text(text, sizeof(text));
  return text5x7Pixel(x, y, 132, 31, text, 2);
}

bool inFrameBorder(int x, int y) {
  // 参考图那种粗黑像素屏幕边框
  return inRect(x, y, 0, 0, TFT_WIDTH, 10) ||
         inRect(x, y, 0, TFT_HEIGHT - 10, TFT_WIDTH, 10) ||
         inRect(x, y, 0, 0, 10, TFT_HEIGHT) ||
         inRect(x, y, TFT_WIDTH - 10, 0, 10, TFT_HEIGHT);
}

bool inCatHeadFill(int x, int y) {
  return inEllipse(x, y, 160, 194, 116, 70) ||
         inTriangle(x, y, 58, 142, 82, 78, 145, 122) ||
         inTriangle(x, y, 262, 142, 238, 78, 175, 122);
}

bool inCatHeadOuter(int x, int y) {
  return inEllipse(x, y, 160, 195, 126, 80) ||
         inTriangle(x, y, 47, 145, 80, 67, 155, 124) ||
         inTriangle(x, y, 273, 145, 240, 67, 165, 124);
}

bool spiralEyePixel(int x, int y, int cx, int cy) {
  if (!inCircle(x, y, cx, cy, 22)) return false;
  if (inCircle(x, y, cx, cy, 3)) return true;
  if (nearLine(x, y, cx - 4, cy - 8, cx + 9, cy - 8, 3)) return true;
  if (nearLine(x, y, cx + 9, cy - 8, cx + 13, cy + 3, 3)) return true;
  if (nearLine(x, y, cx + 13, cy + 3, cx + 2, cy + 13, 3)) return true;
  if (nearLine(x, y, cx + 2, cy + 13, cx - 13, cy + 8, 3)) return true;
  if (nearLine(x, y, cx - 13, cy + 8, cx - 16, cy - 8, 3)) return true;
  if (nearLine(x, y, cx - 16, cy - 8, cx - 4, cy - 19, 3)) return true;
  return false;
}

bool topUiPixel(int x, int y, int expression) {
  int minute = expression % 10;
  if (digitPixel(x, y, 29, 22, 1) || digitPixel(x, y, 51, 22, 2) ||
      digitPixel(x, y, 81, 22, 0) || digitPixel(x, y, 103, 22, minute)) {
    return true;
  }
  if (inCircle(x, y, 75, 31, 3) || inCircle(x, y, 75, 44, 3)) return true;

  if (inRect(x, y, 252, 22, 45, 5) || inRect(x, y, 252, 47, 45, 5) ||
      inRect(x, y, 252, 22, 5, 30) || inRect(x, y, 292, 22, 5, 30) ||
      inRect(x, y, 297, 31, 6, 12)) {
    return true;
  }
  return false;
}

bool batteryFillPixel(int x, int y) {
  return inRect(x, y, 260, 29, 7, 16) ||
         inRect(x, y, 270, 29, 7, 16) ||
         inRect(x, y, 280, 29, 7, 16);
}

void writeCommand(uint8_t command) {
  SPI.beginTransaction(tftSpiSettings);
  digitalWrite(TFT_DC, LOW);
  digitalWrite(TFT_CS, LOW);
  SPI.transfer(command);
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

void writeData(uint8_t data) {
  SPI.beginTransaction(tftSpiSettings);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  SPI.transfer(data);
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

void setAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  writeCommand(0x2A);
  writeData(x0 >> 8);
  writeData(x0 & 0xFF);
  writeData(x1 >> 8);
  writeData(x1 & 0xFF);

  writeCommand(0x2B);
  writeData(y0 >> 8);
  writeData(y0 & 0xFF);
  writeData(y1 >> 8);
  writeData(y1 & 0xFF);

  writeCommand(0x2C);
}

void initDisplay() {
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_RST, LOW);
  delay(1000);
  digitalWrite(TFT_RST, HIGH);
  digitalWrite(TFT_CS, HIGH);
  delay(1000);

  writeCommand(0x11);
  delay(120);

  writeCommand(0x2A);
  writeData(0x00);
  writeData(0x00);
  writeData(0x01);
  writeData(0x3F);

  writeCommand(0x2B);
  writeData(0x00);
  writeData(0x00);
  writeData(0x00);
  writeData(0xEF);

  writeCommand(0xB2);
  writeData(0x0C);
  writeData(0x0C);
  writeData(0x00);
  writeData(0x33);
  writeData(0x33);

  writeCommand(0x20);
  writeCommand(0xB7);
  writeData(0x56);
  writeCommand(0xBB);
  writeData(0x18);
  writeCommand(0xC0);
  writeData(0x2C);
  writeCommand(0xC2);
  writeData(0x01);
  writeCommand(0xC3);
  writeData(0x1F);
  writeCommand(0xC4);
  writeData(0x20);
  writeCommand(0xC6);
  writeData(0x0F);
  writeCommand(0xD0);
  writeData(0xA6);
  writeData(0xA1);

  writeCommand(0xE0);
  uint8_t gamma[] = {0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A,
                     0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32};
  for (uint8_t v : gamma) writeData(v);
  writeCommand(0xE1);
  for (uint8_t v : gamma) writeData(v);

  writeCommand(0x36);
  writeData(0x60);
  writeCommand(0x3A);
  writeData(0x66);
  writeCommand(0xE7);
  writeData(0x00);
  writeCommand(0x21);
  writeCommand(0x29);
  writeCommand(0x2C);
}

bool readTouched() {
  static bool stableState = false;
  static bool lastRaw = false;
  static uint32_t lastChangeMs = 0;

  bool raw = digitalRead(TTP223_PIN) == HIGH;
  uint32_t now = millis();
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChangeMs = now;
  }
  if (now - lastChangeMs > 25) stableState = raw;
  return stableState;
}

bool readSht30() {
  Wire.beginTransmission(SHT30_ADDR);
  Wire.write(0x2C);
  Wire.write(0x06);
  if (Wire.endTransmission() != 0) {
    g_sht30Ok = false;
    return false;
  }
  delay(16);

  if (Wire.requestFrom((int)SHT30_ADDR, 6) != 6) {
    g_sht30Ok = false;
    return false;
  }
  uint8_t data[6];
  for (int i = 0; i < 6; i++) data[i] = Wire.read();

  uint16_t rawTemp = ((uint16_t)data[0] << 8) | data[1];
  uint16_t rawHum = ((uint16_t)data[3] << 8) | data[4];
  float tempC = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
  float hum = 100.0f * ((float)rawHum / 65535.0f);
  if (hum < 0.0f) hum = 0.0f;
  if (hum > 100.0f) hum = 100.0f;
  g_temperatureDeciC = (int)lroundf(tempC * 10.0f);
  g_humidityPercent = (int)lroundf(hum);
  g_sht30Ok = true;
  Serial.printf("[sht30] temp=%.1fC hum=%.0f%%\n", tempC, hum);
  return true;
}

bool rainOverlayPixel(int x, int y, uint32_t t) {
  // 列伪随机：每个 x 列分配一个相位/速度，让雨看起来散乱
  int col = ((x * 263) ^ ((y / 40) * 17)) & 0xFF;
  if ((col & 0x7) != 0) return false;     // 大约 1/8 的列才下雨，避免太密
  int phase = (col * 5) % 80;
  int pos = (y + (int)t * 6 + phase) % 80;
  return pos < 6;                         // 每滴长 6px
}

uint32_t catPixel(int x, int y, int expression) {
  int px = x & ~1;
  int py = y & ~1;

  if (inFrameBorder(px, py)) return C_INK;

  int shade = min(py / 16, 18);
  uint32_t bg = rgb(255, 198 + shade, 225 + min(py / 22, 10));

  if (topUiPixel(px, py, expression)) return C_INK;
  if (sht30TextPixel(px, py)) return C_INK;
  if (batteryFillPixel(px, py)) return C_GREEN;
  if (inRect(px, py, 258, 28, 32, 18)) return C_WHITE;

  // 下雨叠加层：在猫前面但在 UI 后面
  if (g_raining && rainOverlayPixel(px, py, g_rainFrame)) return C_BLUE;

  // 表情外部装饰
  if (expression == EX_HAPPY &&
      (inHeart(px, py, 50, 205, 13) || inHeart(px, py, 270, 205, 13))) return C_HEART;
  if (expression == EX_COQUETTISH &&
      (inHeart(px, py, 47, 78, 8) || inHeart(px, py, 84, 112, 7) ||
       inHeart(px, py, 236, 112, 7) || inHeart(px, py, 272, 205, 13))) return C_HEART;
  if (expression == EX_COQUETTISH &&
      (nearLine(px, py, 277, 80, 277, 108, 3) ||
       nearLine(px, py, 263, 94, 291, 94, 3) ||
       nearLine(px, py, 268, 85, 286, 103, 2) ||
       nearLine(px, py, 286, 85, 268, 103, 2))) return C_SPARK;
  if (expression == EX_ANGRY &&
      (nearLine(px, py, 270, 70, 270, 92, 3) ||
       nearLine(px, py, 258, 82, 282, 82, 3) ||
       nearLine(px, py, 260, 68, 267, 78, 2) ||
       nearLine(px, py, 280, 68, 273, 78, 2) ||
       nearLine(px, py, 260, 96, 267, 87, 2) ||
       nearLine(px, py, 280, 96, 273, 87, 2))) return rgb(245, 69, 43);
  if (expression == EX_SLEEPY &&
      (nearLine(px, py, 250, 65, 272, 65, 3) ||
       nearLine(px, py, 272, 65, 252, 88, 3) ||
       nearLine(px, py, 252, 88, 276, 88, 3) ||
       nearLine(px, py, 274, 96, 292, 96, 2) ||
       nearLine(px, py, 292, 96, 276, 115, 2) ||
       nearLine(px, py, 276, 115, 296, 115, 2))) return C_BLUE;

  // 前景小道具
  if (expression == EX_HUNGRY &&
      (inEllipse(px, py, 262, 217, 24, 13) && !inEllipse(px, py, 262, 217, 18, 8))) return C_INK;
  if (expression == EX_HUNGRY && inEllipse(px, py, 262, 217, 20, 10)) return C_WHITE;
  if (expression == EX_HUNGRY && inEllipse(px, py, 262, 218, 11, 5)) return rgb(233, 160, 89);
  if (expression == EX_HUNGRY &&
      (inEllipse(px, py, 102, 190, 5, 11) || inEllipse(px, py, 218, 190, 5, 11))) return C_CYAN;

  // 猫头粗描边
  if (inCatHeadOuter(px, py) && !inCatHeadFill(px, py)) return C_INK;
  if (nearLine(px, py, 80, 68, 47, 145, 3) ||
      nearLine(px, py, 80, 68, 155, 124, 3) ||
      nearLine(px, py, 240, 68, 273, 145, 3) ||
      nearLine(px, py, 240, 68, 165, 124, 3)) {
    return C_INK;
  }

  // 脸颊外伸的胡须线
  if (nearLine(px, py, 70, 168, 35, 163, 2) ||
      nearLine(px, py, 69, 184, 29, 184, 2) ||
      nearLine(px, py, 70, 199, 38, 207, 2) ||
      nearLine(px, py, 250, 168, 285, 163, 2) ||
      nearLine(px, py, 251, 184, 291, 184, 2) ||
      nearLine(px, py, 250, 199, 282, 207, 2)) {
    return C_INK;
  }

  // 表情五官
  switch (expression) {
    case EX_HAPPY:
      if (nearLine(px, py, 87, 164, 103, 146, 5) ||
          nearLine(px, py, 103, 146, 121, 164, 5) ||
          nearLine(px, py, 199, 164, 217, 146, 5) ||
          nearLine(px, py, 217, 146, 233, 164, 5)) return C_INK;
      if (nearLine(px, py, 149, 190, 158, 199, 3) ||
          nearLine(px, py, 171, 190, 162, 199, 3)) return C_INK;
      if (inEllipse(px, py, 160, 199, 9, 11)) return C_NOSE;
      if (inEllipse(px, py, 160, 204, 6, 7)) return rgb(255, 118, 151);
      break;
    case EX_COQUETTISH:
      if ((inCircle(px, py, 98, 158, 7) || inCircle(px, py, 216, 158, 7) ||
           inCircle(px, py, 108, 151, 5) || inCircle(px, py, 226, 151, 5)) &&
          (inEllipse(px, py, 108, 166, 27, 28) || inEllipse(px, py, 218, 166, 27, 28))) return C_WHITE;
      if (nearLine(px, py, 97, 178, 119, 153, 2) ||
          nearLine(px, py, 97, 153, 119, 178, 2) ||
          nearLine(px, py, 207, 178, 229, 153, 2) ||
          nearLine(px, py, 207, 153, 229, 178, 2)) return C_WHITE;
      if (nearLine(px, py, 86, 153, 96, 143, 3) ||
          nearLine(px, py, 232, 143, 242, 153, 3) ||
          inEllipse(px, py, 108, 166, 27, 28) ||
          inEllipse(px, py, 218, 166, 27, 28)) return C_INK;
      break;
    case EX_ANGRY:
      if (inTriangle(px, py, 83, 148, 133, 138, 126, 170) ||
          inTriangle(px, py, 237, 148, 187, 138, 194, 170)) return C_INK;
      if (inCircle(px, py, 106, 157, 5) || inCircle(px, py, 214, 157, 5)) return C_WHITE;
      break;
    case EX_SLEEPY:
      if (nearLine(px, py, 86, 168, 124, 168, 4) ||
          nearLine(px, py, 196, 168, 234, 168, 4)) return C_INK;
      break;
    case EX_HUNGRY:
      if ((inCircle(px, py, 98, 158, 7) || inCircle(px, py, 216, 158, 7) ||
           inCircle(px, py, 108, 151, 5) || inCircle(px, py, 226, 151, 5)) &&
          (inEllipse(px, py, 108, 166, 27, 28) || inEllipse(px, py, 218, 166, 27, 28))) return C_WHITE;
      if (nearLine(px, py, 84, 151, 100, 137, 3) ||
          nearLine(px, py, 220, 137, 236, 151, 3) ||
          inEllipse(px, py, 108, 166, 27, 28) ||
          inEllipse(px, py, 218, 166, 27, 28)) return C_INK;
      if (nearLine(px, py, 151, 199, 160, 193, 2) ||
          nearLine(px, py, 160, 193, 169, 199, 2)) return C_INK;
      break;
    case EX_SHY:
      if (nearLine(px, py, 86, 165, 123, 165, 4) ||
          nearLine(px, py, 197, 165, 234, 165, 4)) return C_INK;
      if (nearLine(px, py, 78, 198, 104, 184, 3) ||
          nearLine(px, py, 86, 203, 112, 189, 3) ||
          nearLine(px, py, 208, 189, 234, 203, 3) ||
          nearLine(px, py, 216, 184, 242, 198, 3)) return C_BLUSH;
      break;
    case EX_DIZZY:
      if (spiralEyePixel(px, py, 108, 166) || spiralEyePixel(px, py, 218, 166)) return C_INK;
      if (inCircle(px, py, 108, 166, 26) || inCircle(px, py, 218, 166, 26)) return C_WHITE;
      break;
    case EX_DEFAULT:
    default:
      if ((inCircle(px, py, 97, 155, 6) || inCircle(px, py, 211, 155, 6) ||
           inCircle(px, py, 106, 150, 4) || inCircle(px, py, 220, 150, 4)) &&
          (inEllipse(px, py, 108, 166, 26, 27) || inEllipse(px, py, 218, 166, 26, 27))) return C_WHITE;
      if ((inCircle(px, py, 120, 180, 3) || inCircle(px, py, 230, 180, 3)) &&
          (inEllipse(px, py, 108, 166, 26, 27) || inEllipse(px, py, 218, 166, 26, 27))) return rgb(255, 141, 164);
      if (nearLine(px, py, 84, 157, 96, 145, 3) ||
          nearLine(px, py, 224, 145, 236, 157, 3) ||
          inEllipse(px, py, 108, 166, 26, 27) ||
          inEllipse(px, py, 218, 166, 26, 27)) return C_INK;
      break;
  }

  // 嘴鼻
  if (nearLine(px, py, 153, 186, 160, 190, 2) ||
      nearLine(px, py, 167, 186, 160, 190, 2) ||
      nearLine(px, py, 160, 190, 160, 197, 2) ||
      nearLine(px, py, 159, 197, 151, 194, 2) ||
      nearLine(px, py, 161, 197, 169, 194, 2)) {
    return C_NOSE;
  }

  // 腮红和粉色装饰
  if (expression != EX_SHY &&
      (inEllipse(px, py, 72, 198, 20, 12) ||
       inEllipse(px, py, 248, 198, 20, 12))) {
    return C_BLUSH;
  }
  if (nearLine(px, py, 148, 115, 148, 137, 3) ||
      nearLine(px, py, 160, 115, 160, 140, 3) ||
      nearLine(px, py, 172, 115, 172, 137, 3)) {
    return C_FUR_DARK;
  }
  if (inTriangle(px, py, 68, 130, 88, 90, 126, 116) ||
      inTriangle(px, py, 252, 130, 232, 90, 194, 116)) {
    return C_INNER_EAR;
  }

  // 猫脸填充
  if (inEllipse(px, py, 160, 198, 98, 58)) return C_WHITE;
  if (inCatHeadFill(px, py)) return C_FUR;

  return bg;
}

void renderCat(int expression) {
  uint8_t line[TFT_WIDTH * 3];
  setAddressWindow(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
  SPI.beginTransaction(tftSpiSettings);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  for (int y = 0; y < TFT_HEIGHT; y++) {
    for (int x = 0; x < TFT_WIDTH; x++) {
      uint32_t c = catPixel(x, y, expression);
      line[x * 3 + 0] = redOf(c);
      line[x * 3 + 1] = greenOf(c);
      line[x * 3 + 2] = blueOf(c);
    }
    SPI.writeBytes(line, sizeof(line));
  }
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

const char *expressionName(int expression) {
  switch (expression) {
    case EX_DEFAULT: return "默认";
    case EX_HAPPY: return "开心";
    case EX_COQUETTISH: return "撒娇";
    case EX_ANGRY: return "生气";
    case EX_SLEEPY: return "困了";
    case EX_HUNGRY: return "饿了";
    case EX_SHY: return "害羞";
    case EX_DIZZY: return "晕晕";
    default: return "未知";
  }
}

void requestExpression(int expression) {
  if (expression < 0 || expression >= EX_COUNT) return;
  g_expression = expression;
  g_expressionDirty = true;
  Serial.printf("[cat] expression=%d %s\n", expression, expressionName(expression));
}

void sendSu03tCommand(const char *command) {
  if (!command || command[0] == '\0') return;
  g_su03t.print(command);
  g_su03t.print('\n');
  g_lastVoiceCommand = command;
  Serial.printf("[su03t] tx=%s\n", command);
}

void sendSu03tExpression(int expression) {
  if (expression < 0 || expression >= EX_COUNT) return;
  sendSu03tCommand(SU03T_EXPRESSION_COMMANDS[expression]);
}

void stopSpeaking() {
  g_speechActive = false;
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void startSpeaking(int expression) {
  if (expression < 0 || expression >= EX_COUNT) return;
  sendSu03tExpression(expression);
  g_speechExpression = expression;
  g_speechStep = 0;
  g_speechActive = true;
  g_speechNextMs = millis() + SPEECH_PATTERNS[expression][0];
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  Serial.printf("[cat] speak=%d %s\n", expression, expressionName(expression));
}

void updateSpeech() {
  if (!g_speechActive) return;
  uint32_t now = millis();
  if ((int32_t)(now - g_speechNextMs) < 0) return;

  g_speechStep++;
  if (g_speechStep >= 12 || SPEECH_PATTERNS[g_speechExpression][g_speechStep] == 0) {
    stopSpeaking();
    return;
  }

  digitalWrite(BUZZER_PIN, (g_speechStep % 2 == 0) ? BUZZER_ON : BUZZER_OFF);
  g_speechNextMs = now + SPEECH_PATTERNS[g_speechExpression][g_speechStep];
}

String expressionJson() {
  String json = "{\"expression\":";
  json += g_expression;
  json += ",\"name\":\"";
  json += expressionName(g_expression);
  json += "\",\"speaking\":";
  json += g_speechActive ? "true" : "false";
  json += ",\"voice\":\"su03t\"";
  json += ",\"lastVoiceCommand\":\"";
  json += g_lastVoiceCommand;
  json += "\"";
  json += "}";
  return json;
}

void sendCorsJson(int code, const String &json) {
  g_server.sendHeader("Access-Control-Allow-Origin", "*");
  g_server.send(code, "application/json; charset=utf-8", json);
}

void sendCorsText(int code, const String &text) {
  g_server.sendHeader("Access-Control-Allow-Origin", "*");
  g_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  g_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  g_server.send(code, "text/plain; charset=utf-8", text);
}

String wifiControlPage() {
  String page = F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>PixelCat-S3</title><style>"
                  "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#ffd2e6;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif;color:#111}"
                  ".box{width:min(520px,92vw);background:#fff;border:6px solid #111;border-radius:14px;padding:18px;box-shadow:0 10px 0 #111}"
                  "h1{margin:0 0 6px;font-size:24px}.meta{margin:0 0 16px;color:#666;font-size:13px}"
                  ".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}"
                  "button{min-height:58px;border:2px solid #111;border-radius:8px;background:#fff0f7;font-size:16px;font-weight:800;cursor:pointer}"
                  "button:active,button.on{background:#ff78a8;color:#fff}.speak{width:100%;margin-top:12px;background:#fff7d6}.status,.link{margin-top:14px;font-size:14px;color:#444}.link a{color:#be185d;font-weight:800}"
                  "</style></head><body><main class='box'><h1>PixelCat-S3</h1>"
                  "<p class='meta'>Wi-Fi 热点控制页 · 发送 0-7 切换表情，SU-03T 喇叭播报</p><div class='grid'>");
  for (int i = 0; i < EX_COUNT; i++) {
    page += "<button data-id='";
    page += i;
    page += "'>12:0";
    page += i;
    page += "<br>";
    page += expressionName(i);
    page += "</button>";
  }
  page += F("</div><button id='speak' class='speak' type='button'>让它说话</button><div id='status' class='status'>当前：读取中</div>"
            "<div class='link'><a href='/mobile'>手机版控制页</a> · <a href='/update'>OTA 上传新固件</a></div></main>"
            "<script>"
            "const buttons=[...document.querySelectorAll('.grid button')],status=document.querySelector('#status'),speak=document.querySelector('#speak');"
            "async function refresh(){const r=await fetch('/status');const j=await r.json();"
            "buttons.forEach(b=>b.classList.toggle('on',Number(b.dataset.id)===j.expression));"
            "status.textContent='当前：12:0'+j.expression+' '+j.name+(j.speaking?' · 说话中':'');}"
            "buttons.forEach(b=>b.onclick=async()=>{await fetch('/set?id='+b.dataset.id);refresh();});"
            "speak.onclick=async()=>{const id=buttons.find(b=>b.classList.contains('on'))?.dataset.id||0;await fetch('/speak?id='+id);refresh();};"
            "refresh();setInterval(refresh,1500);"
            "</script></body></html>");
  return page;
}

String mobileControlPage() {
  return F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
           "<title>PixelCat Mobile</title><style>"
           "*{box-sizing:border-box}body{margin:0;min-height:100dvh;background:#f7f9fc;color:#121826;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif}"
           ".app{width:min(100%,520px);margin:0 auto;padding:calc(env(safe-area-inset-top) + 14px) 14px calc(env(safe-area-inset-bottom) + 18px)}"
           "header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 2px 16px}h1{margin:0;font-size:22px}"
           ".pill{display:inline-flex;align-items:center;min-height:32px;padding:0 11px;border:1px solid #d9e0ea;border-radius:999px;background:#fff;color:#667085;font-size:12px;font-weight:800}"
           ".ok{color:#166534;border-color:#86efac;background:#dcfce7}.card{overflow:hidden;margin-bottom:14px;border:1px solid #d9e0ea;border-radius:8px;background:#fff;box-shadow:0 14px 36px rgba(18,24,38,.1)}"
           ".head{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:13px 14px;border-bottom:1px solid #d9e0ea}.body{padding:14px}"
           ".wifi{display:grid;grid-template-columns:78px 1fr;gap:10px 12px;margin-bottom:14px;font-size:14px}.label{color:#667085;font-weight:800}"
           "code{padding:2px 6px;border:1px solid #dbe3ed;border-radius:5px;background:#f8fafc;font-family:Consolas,monospace;font-size:.92em}"
           ".actions,.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}button,a.btn{display:inline-grid;place-items:center;min-height:48px;padding:0 12px;border:1px solid #d9e0ea;border-radius:8px;background:#fff;color:#121826;font:inherit;font-size:14px;font-weight:800;text-decoration:none}"
           ".primary{border-color:#1d4ed8!important;background:#2563eb!important;color:#fff!important}.speak{width:100%;min-height:76px;border-color:#a16207;background:#facc15;color:#422006;font-size:20px}"
           ".expr{display:grid;grid-template-columns:42px 1fr;gap:10px;align-items:center;min-height:60px;text-align:left}.expr.on{border-color:#be185d;background:#fdf2f8;color:#9d174d}"
           ".time{display:grid;place-items:center;min-height:36px;border-radius:7px;background:#111827;color:#fff;font-family:Consolas,monospace;font-size:12px}.name{font-size:15px;font-weight:900}"
           ".hint{margin:10px 0 0;color:#667085;font-size:12px;line-height:1.5}.toast{position:fixed;left:14px;right:14px;bottom:calc(env(safe-area-inset-bottom) + 14px);max-width:492px;margin:0 auto;padding:12px 14px;border-radius:8px;background:rgba(18,24,38,.94);color:#fff;font-size:13px;opacity:0;transform:translateY(12px);pointer-events:none;transition:.16s}.toast.show{opacity:1;transform:translateY(0)}"
           "@media(max-width:360px){.actions,.grid{grid-template-columns:1fr}h1{font-size:20px}}</style></head><body><main class='app'>"
           "<header><h1>PixelCat</h1><span id='net' class='pill'>未连接</span></header>"
           "<section class='card'><div class='head'><strong>连接设备 Wi-Fi</strong><button id='copy' type='button'>复制</button></div><div class='body'>"
           "<div class='wifi'><span class='label'>Wi-Fi</span><code>PixelCat-S3</code><span class='label'>密码</span><code>cat12345</code><span class='label'>地址</span><code>192.168.4.1</code></div>"
           "<div class='actions'><button id='check' class='primary' type='button'>检测连接</button><a class='btn' href='/'>打开普通页</a></div>"
           "<p class='hint'>手机先在系统 Wi-Fi 设置里连接 PixelCat-S3；浏览器不能自动切换系统 Wi-Fi。</p></div></section>"
           "<section class='card'><div class='head'><strong>和设备对话</strong><span id='cur' class='pill'>12:00 默认</span></div><div class='body'><button id='speak' class='speak' type='button'>让它说话</button><p class='hint'>ESP32 通过 UART 发送命令给 SU-03T，再由 SU-03T 驱动喇叭播报。</p></div></section>"
           "<section class='card'><div class='head'><strong>对话命令</strong><span class='pill'>SU-03T</span></div><div class='body'><div class='actions'><button class='vcmd' data-cmd='HELLO' type='button'>打招呼</button><button class='vcmd' data-cmd='FEED' type='button'>喂食</button><button class='vcmd' data-cmd='SLEEP' type='button'>睡觉</button><button class='vcmd' data-cmd='LOVE' type='button'>撒娇</button></div><p class='hint'>这些按钮请求 /voice?cmd=...，需要在 SU-03T 工具里配置同名串口命令和播报内容。</p></div></section>"
           "<section class='card'><div class='head'><strong>表情控制</strong><button id='refresh' type='button'>刷新</button></div><div class='body'><div id='grid' class='grid'></div></div></section></main><div id='toast' class='toast'></div>"
           "<script>"
           "const ex=['默认','开心','撒娇','生气','困了','饿了','害羞','晕晕'];let sel=0,t=0;"
           "const net=document.querySelector('#net'),cur=document.querySelector('#cur'),grid=document.querySelector('#grid'),toast=document.querySelector('#toast');"
           "function msg(s){toast.textContent=s;toast.classList.add('show');clearTimeout(t);t=setTimeout(()=>toast.classList.remove('show'),2200)}"
           "function online(v){net.textContent=v?'已连接':'未连接';net.classList.toggle('ok',v)}"
           "function current(id,name){sel=id;cur.textContent='12:0'+id+' '+name;render()}"
           "async function api(path){const c=new AbortController(),timer=setTimeout(()=>c.abort(),1600);try{const r=await fetch(path,{cache:'no-store',signal:c.signal});clearTimeout(timer);if(!r.ok)throw Error(r.status);online(true);return r}catch(e){clearTimeout(timer);online(false);throw e}}"
           "async function status(show){try{const j=await(await api('/status')).json();current(Number(j.expression),j.name||ex[Number(j.expression)]);if(show)msg('设备已连接')}catch(e){if(show)msg('请先连接 PixelCat-S3')}}"
           "async function setExpr(id){current(id,ex[id]);try{const j=await(await api('/set?id='+id)).json();current(Number(j.expression),j.name||ex[id]);msg('已切换：'+(j.name||ex[id]))}catch(e){msg('发送失败：请检查 Wi-Fi')}}"
           "async function speak(){try{await api('/speak?id='+sel);msg('已发送说话指令')}catch(e){msg('发送失败：请检查 Wi-Fi')}}"
           "async function voice(c){try{await api('/voice?cmd='+encodeURIComponent(c));msg('已发送：'+c)}catch(e){msg('发送失败：请检查 Wi-Fi')}}"
           "function render(){grid.innerHTML=ex.map((n,i)=>`<button class='expr ${i===sel?'on':''}' type='button' data-id='${i}'><span class='time'>12:0${i}</span><span class='name'>${n}</span></button>`).join('');grid.querySelectorAll('button').forEach(b=>b.onclick=()=>setExpr(Number(b.dataset.id)))}"
           "document.querySelector('#copy').onclick=async()=>{try{await navigator.clipboard.writeText('Wi-Fi: PixelCat-S3\\n密码: cat12345\\n地址: http://192.168.4.1/mobile');msg('Wi-Fi 信息已复制')}catch(e){msg('Wi-Fi：PixelCat-S3，密码：cat12345')}};"
           "document.querySelector('#check').onclick=()=>status(true);document.querySelector('#refresh').onclick=()=>status(true);document.querySelector('#speak').onclick=speak;document.querySelectorAll('.vcmd').forEach(b=>b.onclick=()=>voice(b.dataset.cmd));render();status(false);"
           "</script></body></html>");
}

String otaUpdatePage() {
  return F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>PixelCat-S3 OTA</title><style>"
           "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#eef4ff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif;color:#111}"
           ".box{width:min(520px,92vw);background:#fff;border:6px solid #111;border-radius:14px;padding:18px;box-shadow:0 10px 0 #111}"
           "h1{margin:0 0 8px;font-size:24px}p{color:#555;line-height:1.5}.file{display:block;width:100%;padding:12px;border:2px dashed #94a3b8;border-radius:8px;background:#f8fafc}"
           "button{margin-top:14px;min-height:44px;width:100%;border:2px solid #111;border-radius:8px;background:#ff78a8;color:#fff;font-size:16px;font-weight:800}"
           "a{display:inline-block;margin-top:14px;color:#be185d;font-weight:800}</style></head><body><main class='box'>"
           "<h1>OTA 上传</h1><p>选择 Arduino 导出的 <code>.bin</code> 固件。上传完成后 ESP32 会自动重启。</p>"
           "<form method='POST' action='/update' enctype='multipart/form-data'>"
           "<input class='file' type='file' name='firmware' accept='.bin' required>"
           "<button type='submit'>上传并重启</button></form><a href='/'>返回表情控制</a></main></body></html>");
}

void handleRoot() {
  g_server.send(200, "text/html; charset=utf-8", wifiControlPage());
}

void handleStatus() {
  sendCorsJson(200, expressionJson());
}

void handleMobile() {
  g_server.send(200, "text/html; charset=utf-8", mobileControlPage());
}

void handleOtaPage() {
  g_server.send(200, "text/html; charset=utf-8", otaUpdatePage());
}

void handleOtaUpload() {
  HTTPUpload &upload = g_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[ota] success %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    Serial.println("[ota] aborted");
  }
}

void handleOtaDone() {
  bool ok = !Update.hasError();
  g_server.sendHeader("Connection", "close");
  sendCorsText(ok ? 200 : 500, ok ? "OTA OK, rebooting" : "OTA failed");
  if (ok) {
    delay(700);
    ESP.restart();
  }
}

void handleCorsOptions() {
  g_server.sendHeader("Access-Control-Allow-Origin", "*");
  g_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  g_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  g_server.send(204);
}

void handleSetExpression() {
  if (!g_server.hasArg("id")) {
    sendCorsJson(400, "{\"error\":\"missing id\"}");
    return;
  }
  int expression = g_server.arg("id").toInt();
  if (expression < 0 || expression >= EX_COUNT) {
    sendCorsJson(400, "{\"error\":\"id must be 0-7\"}");
    return;
  }
  requestExpression(expression);
  sendCorsJson(200, expressionJson());
}

void handleSpeak() {
  int expression = g_expression;
  if (g_server.hasArg("id")) {
    expression = g_server.arg("id").toInt();
    if (expression < 0 || expression >= EX_COUNT) {
      sendCorsJson(400, "{\"error\":\"id must be 0-7\"}");
      return;
    }
  }
  startSpeaking(expression);
  sendCorsJson(200, expressionJson());
}

void handleVoiceCommand() {
  if (!g_server.hasArg("cmd")) {
    sendCorsJson(400, "{\"error\":\"missing cmd\"}");
    return;
  }

  String command = g_server.arg("cmd");
  command.trim();
  if (command.length() == 0 || command.length() > 32) {
    sendCorsJson(400, "{\"error\":\"cmd length must be 1-32\"}");
    return;
  }
  for (size_t i = 0; i < command.length(); i++) {
    char c = command[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) {
      sendCorsJson(400, "{\"error\":\"cmd allows A-Z a-z 0-9 _ -\"}");
      return;
    }
  }

  command.toUpperCase();
  sendSu03tCommand(command.c_str());
  sendCorsJson(200, expressionJson());
}

void handleNextExpression() {
  requestExpression((g_expression + 1) % EX_COUNT);
  sendCorsJson(200, expressionJson());
}

void setupWifiControl() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  g_server.on("/", handleRoot);
  g_server.on("/mobile", handleMobile);
  g_server.on("/status", handleStatus);
  g_server.on("/set", handleSetExpression);
  g_server.on("/speak", handleSpeak);
  g_server.on("/voice", handleVoiceCommand);
  g_server.on("/next", handleNextExpression);
  g_server.on("/update", HTTP_GET, handleOtaPage);
  g_server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
  g_server.on("/update", HTTP_OPTIONS, handleCorsOptions);
  g_server.begin();
  Serial.printf("[wifi] AP=%s password=%s url=http://%s/\n",
                WIFI_AP_SSID, WIFI_AP_PASS, WiFi.softAPIP().toString().c_str());
}

bool readSerialExpression() {
  bool changed = false;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (g_usbLine.length() > 0) {
        g_usbLine.trim();
        if (g_usbLine.length() == 1 && g_usbLine[0] >= '0' && g_usbLine[0] <= '7') {
          requestExpression(g_usbLine[0] - '0');
          changed = true;
        } else if (g_usbLine.equalsIgnoreCase("s")) {
          startSpeaking(g_expression);
          changed = true;
        } else if (g_usbLine.equalsIgnoreCase("n")) {
          requestExpression((g_expression + 1) % EX_COUNT);
          changed = true;
        } else if (g_usbLine.length() > 1 && g_usbLine.length() <= 32) {
          if (g_usbLine[0] == '!') {
            g_usbLine.remove(0, 1);
          } else {
            g_usbLine.toUpperCase();
          }
          sendSu03tCommand(g_usbLine.c_str());
          changed = true;
        }
        g_usbLine = "";
      }
    } else if (g_usbLine.length() < 32) {
      bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '!';
      if (ok) g_usbLine += c;
    } else {
      g_usbLine = "";
    }
  }
  return changed;
}

bool applyVoiceToken(String token) {
  token.trim();
  if (token.length() == 0) return false;
  int cmdPos = token.indexOf("\"cmd\":\"");
  if (cmdPos >= 0) {
    int start = cmdPos + 7;
    int end = token.indexOf("\"", start);
    if (end > start) token = token.substring(start, end);
  }
  token.toUpperCase();
  Serial.printf("[su03t] rx=%s\n", token.c_str());

  if (token.length() == 1 && token[0] >= '0' && token[0] <= '7') {
    requestExpression(token[0] - '0');
    startSpeaking(g_expression);
    return true;
  }
  if (token == "NEXT" || token == "CAT_NEXT") {
    requestExpression((g_expression + 1) % EX_COUNT);
    startSpeaking(g_expression);
    return true;
  }
  if (token == "SPEAK" || token == "CAT_SPEAK" || token == "HELLO") {
    startSpeaking(g_expression);
    return true;
  }

  for (int i = 0; i < EX_COUNT; i++) {
    String command = SU03T_EXPRESSION_COMMANDS[i];
    command.toUpperCase();
    if (token == command || token == String(i)) {
      requestExpression(i);
      startSpeaking(i);
      return true;
    }
  }
  return false;
}

bool readSu03tVoice() {
  bool changed = false;
  while (g_su03t.available() > 0) {
    char c = (char)g_su03t.read();
    if (c == '\r' || c == '\n') {
      if (g_su03tLine.length() > 0) {
        changed = applyVoiceToken(g_su03tLine) || changed;
        g_su03tLine = "";
      }
    } else if (g_su03tLine.length() < 32) {
      g_su03tLine += c;
    } else {
      g_su03tLine = "";
    }
  }
  return changed;
}

void setup() {
  pinMode(TTP223_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(TFT_BL, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_RST, HIGH);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Starting pixel cat replica");
  g_su03t.begin(SU03T_BAUD, SERIAL_8N1, SU03T_RX_PIN, SU03T_TX_PIN);
  g_lastVoiceCommand = "ready";

  Wire.begin(SHT30_SDA_PIN, SHT30_SCL_PIN);
  Wire.setClock(100000);

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  initDisplay();
  readSht30();
  renderCat(g_expression);
  setupWifiControl();
}

void loop() {
  static bool lastTouched = false;
  static uint32_t lastSoilCheck = 0;
  static uint32_t lastRainFrame = 0;
  static uint32_t lastSht30Check = 0;

  uint32_t now = millis();
  bool needRedraw = false;

  g_server.handleClient();
  updateSpeech();

  if (readSerialExpression()) needRedraw = true;
  if (readSu03tVoice()) needRedraw = true;

  if (now - lastSht30Check > SHT30_CHECK_MS) {
    lastSht30Check = now;
    readSht30();
    needRedraw = true;
  }

  if (now - lastSoilCheck > SOIL_CHECK_MS) {
    lastSoilCheck = now;
    int v = analogRead(SOIL_PIN);
    bool wet = v < SOIL_WET_THRESHOLD;
    Serial.printf("[soil] adc=%d wet=%d\n", v, (int)wet);
    if (wet != g_raining) {
      g_raining = wet;
      g_rainFrame = 0;
      needRedraw = true;
    }
  }

  if (g_raining && now - lastRainFrame > RAIN_FRAME_MS) {
    lastRainFrame = now;
    g_rainFrame++;
    needRedraw = true;
  }

  bool touched = readTouched();
  if (touched && !lastTouched) {
    requestExpression((g_expression + 1) % EX_COUNT);
  }
  lastTouched = touched;

  if (g_expressionDirty) {
    g_expressionDirty = false;
    needRedraw = true;
  }

  if (needRedraw) renderCat(g_expression);
}
