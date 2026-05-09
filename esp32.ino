// ESP32-S3 + 2.0 inch ST7789V2 cute pixel cat UI
// Direct SPI RGB666, no TFT_eSPI. Touch TTP223 cycles 8 expressions.
#include <Arduino.h>
#include <SPI.h>
#include <math.h>

constexpr int TTP223_PIN = 4;

constexpr int SOIL_PIN = 2;            // HW-390 AOUT 接 GPIO2 (ADC1_CH1)
constexpr int SOIL_WET_THRESHOLD = 2400;  // ADC 低于这个值就算湿润 (越湿值越低，按串口实测调)
constexpr uint32_t SOIL_CHECK_MS = 500;
constexpr uint32_t RAIN_FRAME_MS = 180;

constexpr int TFT_BL = 5;
constexpr int TFT_CS = 10;
constexpr int TFT_DC = 11;
constexpr int TFT_RST = 8;
constexpr int TFT_MOSI = 12;
constexpr int TFT_SCLK = 13;

constexpr int TFT_WIDTH = 240;
constexpr int TFT_HEIGHT = 320;

SPISettings tftSpiSettings(20000000, MSBFIRST, SPI_MODE0);

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint8_t redOf(uint32_t c) { return (c >> 16) & 0xFF; }
uint8_t greenOf(uint32_t c) { return (c >> 8) & 0xFF; }
uint8_t blueOf(uint32_t c) { return c & 0xFF; }

constexpr uint32_t C_BG = rgb(238, 248, 250);
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

bool g_raining = false;
uint32_t g_rainFrame = 0;

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


bool inFrameBorder(int x, int y) {
  // 参考图那种粗黑像素屏幕边框
  return inRect(x, y, 0, 0, TFT_WIDTH, 8) ||
         inRect(x, y, 0, TFT_HEIGHT - 8, TFT_WIDTH, 8) ||
         inRect(x, y, 0, 0, 8, TFT_HEIGHT) ||
         inRect(x, y, TFT_WIDTH - 8, 0, 8, TFT_HEIGHT);
}

bool inCatHeadFill(int x, int y) {
  return inEllipse(x, y, 120, 178, 92, 104) ||
         inTriangle(x, y, 30, 123, 68, 46, 101, 135) ||
         inTriangle(x, y, 210, 123, 172, 46, 139, 135);
}

bool inCatHeadOuter(int x, int y) {
  return inEllipse(x, y, 120, 178, 98, 110) ||
         inTriangle(x, y, 24, 126, 68, 36, 107, 140) ||
         inTriangle(x, y, 216, 126, 172, 36, 133, 140);
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
  int minute = (int)expression;
  if (digitPixel(x, y, 12, 18, 1) || digitPixel(x, y, 34, 18, 2) ||
      digitPixel(x, y, 64, 18, 0) || digitPixel(x, y, 86, 18, minute)) {
    return true;
  }
  if (inCircle(x, y, 58, 27, 3) || inCircle(x, y, 58, 40, 3)) return true;

  if (inRect(x, y, 184, 18, 35, 5) || inRect(x, y, 184, 43, 35, 5) ||
      inRect(x, y, 184, 18, 5, 30) || inRect(x, y, 214, 18, 5, 30) ||
      inRect(x, y, 219, 28, 5, 10)) {
    return true;
  }
  return false;
}

bool batteryFillPixel(int x, int y) {
  return inRect(x, y, 191, 25, 6, 17) ||
         inRect(x, y, 200, 25, 6, 17) ||
         inRect(x, y, 209, 25, 5, 17);
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
  writeData(0x00);
  writeData(0xEF);

  writeCommand(0x2B);
  writeData(0x00);
  writeData(0x28);
  writeData(0x01);
  writeData(0x17);

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
  writeData(0x00);
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

bool rainOverlayPixel(int x, int y, uint32_t t) {
  // 列伪随机：每个 x 列分配一个相位/速度，让雨看起来散乱
  int col = ((x * 263) ^ ((y / 40) * 17)) & 0xFF;
  if ((col & 0x7) != 0) return false;     // 大约 1/8 的列才下雨，避免太密
  int phase = (col * 5) % 80;
  int pos = (y + (int)t * 6 + phase) % 80;
  return pos < 6;                         // 每滴长 6px
}

uint32_t catPixel(int x, int y, int expression) {
  if (inFrameBorder(x, y)) return C_INK;
  uint32_t bg = rgb(255, 226 - min(y / 10, 18), 238 - min(y / 16, 12));

  if (topUiPixel(x, y, expression)) return C_INK;
  if (batteryFillPixel(x, y)) return C_GREEN;

  // 下雨叠加层：在猫前面但在 UI 后面
  if (g_raining && rainOverlayPixel(x, y, g_rainFrame)) return C_BLUE;

  // 头顶/耳侧装饰：撒娇心心、生气符号、Zzz、闪光
  if (expression == EX_COQUETTISH &&
      (inHeart(x, y, 35, 88, 5) || inHeart(x, y, 206, 101, 9))) return C_HEART;
  if (expression == EX_ANGRY &&
      (nearLine(x, y, 205, 74, 212, 62, 2) ||
       nearLine(x, y, 214, 74, 221, 62, 2) ||
       nearLine(x, y, 204, 82, 218, 82, 2) ||
       nearLine(x, y, 212, 90, 222, 100, 2))) return rgb(245, 69, 43);
  if (expression == EX_SLEEPY &&
      (nearLine(x, y, 195, 70, 213, 70, 2) ||
       nearLine(x, y, 213, 70, 196, 88, 2) ||
       nearLine(x, y, 196, 88, 214, 88, 2) ||
       nearLine(x, y, 211, 95, 226, 95, 2) ||
       nearLine(x, y, 226, 95, 212, 109, 2) ||
       nearLine(x, y, 212, 109, 227, 109, 2))) return C_BLUE;
  if (expression == EX_COQUETTISH &&
      (nearLine(x, y, 197, 78, 207, 78, 2) ||
       nearLine(x, y, 202, 73, 202, 83, 2))) return C_SPARK;

  // 黑色描边
  if (inCatHeadOuter(x, y) && !inCatHeadFill(x, y)) return C_INK;

  // 脸颊外伸的胡须线
  if (nearLine(x, y, 55, 174, 23, 163, 1) ||
      nearLine(x, y, 54, 188, 20, 188, 1) ||
      nearLine(x, y, 55, 202, 23, 213, 1) ||
      nearLine(x, y, 185, 174, 217, 163, 1) ||
      nearLine(x, y, 186, 188, 220, 188, 1) ||
      nearLine(x, y, 185, 202, 217, 213, 1)) {
    return C_INK;
  }

  // 表情专属的五官
  switch (expression) {
    case EX_DEFAULT:
      if (inEllipse(x, y, 84, 153, 14, 18) || inEllipse(x, y, 156, 153, 14, 18)) return C_INK;
      if (inCircle(x, y, 79, 146, 4) || inCircle(x, y, 151, 146, 4)) return C_WHITE;
      if (nearLine(x, y, 105, 211, 116, 218, 2) || nearLine(x, y, 124, 218, 135, 211, 2)) return C_INK;
      break;
    case EX_HAPPY:
      if (inHeart(x, y, 69, 188, 6) || inHeart(x, y, 171, 188, 6)) return C_BLUSH;
      if (nearLine(x, y, 70, 157, 84, 142, 4) || nearLine(x, y, 84, 142, 100, 157, 4) ||
          nearLine(x, y, 140, 157, 156, 142, 4) || nearLine(x, y, 156, 142, 170, 157, 4)) return C_INK;
      if (inTriangle(x, y, 112, 205, 128, 205, 120, 222)) return C_NOSE;
      if (nearLine(x, y, 98, 205, 110, 216, 2) || nearLine(x, y, 130, 216, 142, 205, 2)) return C_INK;
      break;
    case EX_COQUETTISH:
      if (inEllipse(x, y, 84, 153, 14, 18) || inEllipse(x, y, 156, 153, 14, 18)) return C_INK;
      if (inCircle(x, y, 79, 146, 5) || inCircle(x, y, 151, 146, 5)) return C_WHITE;
      if (nearLine(x, y, 72, 142, 90, 130, 3) || nearLine(x, y, 148, 130, 168, 142, 3)) return C_INK;
      if (inHeart(x, y, 70, 112, 5) || inHeart(x, y, 170, 112, 5)) return C_HEART;
      if (nearLine(x, y, 105, 211, 116, 218, 2) || nearLine(x, y, 124, 218, 135, 211, 2)) return C_INK;
      break;
    case EX_ANGRY:
      if (inTriangle(x, y, 66, 143, 103, 134, 103, 163) || inTriangle(x, y, 174, 143, 137, 134, 137, 163)) return C_INK;
      if (inCircle(x, y, 87, 151, 4) || inCircle(x, y, 153, 151, 4)) return C_WHITE;
      if (nearLine(x, y, 105, 218, 116, 211, 2) || nearLine(x, y, 124, 211, 135, 218, 2)) return C_INK;
      break;
    case EX_SLEEPY:
      if (nearLine(x, y, 68, 158, 102, 158, 3) || nearLine(x, y, 138, 158, 172, 158, 3)) return C_INK;
      if (nearLine(x, y, 105, 211, 116, 218, 2) || nearLine(x, y, 124, 218, 135, 211, 2)) return C_INK;
      break;
    case EX_HUNGRY:
      if (inEllipse(x, y, 84, 153, 14, 18) || inEllipse(x, y, 156, 153, 14, 18)) return C_INK;
      if (inCircle(x, y, 79, 146, 5) || inCircle(x, y, 151, 146, 5)) return C_WHITE;
      if (inCircle(x, y, 90, 161, 3) || inCircle(x, y, 162, 161, 3)) return C_WHITE;
      if (inCircle(x, y, 82, 158, 2) || inCircle(x, y, 154, 158, 2)) return C_WHITE;
      if (nearLine(x, y, 72, 134, 96, 123, 3) || nearLine(x, y, 144, 123, 168, 134, 3)) return C_INK;
      if (nearLine(x, y, 108, 213, 120, 207, 2) || nearLine(x, y, 120, 207, 132, 213, 2)) return C_INK;
      if (inEllipse(x, y, 205, 226, 18, 10)) return C_WHITE;
      if (inEllipse(x, y, 205, 226, 11, 5)) return rgb(235, 165, 96);
      break;
    case EX_SHY:
      if (nearLine(x, y, 68, 158, 102, 158, 3) || nearLine(x, y, 138, 158, 172, 158, 3)) return C_INK;
      if (nearLine(x, y, 58, 190, 76, 177, 2) || nearLine(x, y, 66, 194, 84, 181, 2) ||
          nearLine(x, y, 156, 181, 174, 194, 2) || nearLine(x, y, 164, 177, 182, 190, 2)) return C_BLUSH;
      if (nearLine(x, y, 105, 211, 116, 218, 2) || nearLine(x, y, 124, 218, 135, 211, 2)) return C_INK;
      break;
    case EX_DIZZY:
      if (inCircle(x, y, 84, 153, 23) || inCircle(x, y, 156, 153, 23)) return C_WHITE;
      if (spiralEyePixel(x, y, 84, 153) || spiralEyePixel(x, y, 156, 153)) return C_INK;
      if (nearLine(x, y, 105, 211, 116, 218, 2) || nearLine(x, y, 124, 218, 135, 211, 2)) return C_INK;
      break;
    default:
      break;
  }

  // 默认鼻子和嘴
  if (inTriangle(x, y, 110, 181, 130, 181, 120, 194)) return C_NOSE;
  if (nearLine(x, y, 120, 193, 120, 206, 1)) return C_INK;

  // 圆形腮红（HAPPY 是心形腮红，SHY 是斜线腮红，画在 switch 里了，跳过）
  if (expression != EX_HAPPY && expression != EX_SHY &&
      (inCircle(x, y, 69, 190, 14) || inCircle(x, y, 171, 190, 14))) return C_BLUSH;

  // 脸颊深色绒毛纹
  if (nearLine(x, y, 78, 98, 85, 80, 2) ||
      nearLine(x, y, 96, 93, 99, 75, 2) ||
      nearLine(x, y, 162, 98, 155, 80, 2) ||
      nearLine(x, y, 144, 93, 141, 75, 2)) {
    return C_FUR_DARK;
  }

  // 耳朵粉色
  if (inTriangle(x, y, 50, 113, 69, 68, 90, 123)) return C_INNER_EAR;
  if (inTriangle(x, y, 190, 113, 171, 68, 150, 123)) return C_INNER_EAR;

  // 下巴/肚子白色
  if (inEllipse(x, y, 120, 211, 60, 50)) return C_WHITE;

  // 脚掌（先于头部填充检查，因为脚的位置落在头部椭圆里）
  if (inCircle(x, y, 56, 248, 17) || inCircle(x, y, 184, 248, 17)) {
    if (inCircle(x, y, 56, 255, 9) || inCircle(x, y, 184, 255, 9)) return C_WHITE;
    return C_FUR;
  }

  // 头部和耳朵的奶油色填充（兜底）
  if (inEllipse(x, y, 120, 178, 92, 104)) return C_FUR;
  if (inTriangle(x, y, 34, 123, 68, 46, 101, 135) ||
      inTriangle(x, y, 206, 123, 172, 46, 139, 135)) return C_FUR;

  // 地面阴影
  if (inEllipse(x, y, 120, 282, 76, 12)) return C_SHADOW;

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

void setup() {
  pinMode(TTP223_PIN, INPUT);
  pinMode(TFT_BL, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  digitalWrite(TFT_BL, HIGH);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_RST, HIGH);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Starting pixel cat expressions");

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL_PIN, ADC_11db);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  initDisplay();
  renderCat(EX_DEFAULT);
}

void loop() {
  static bool lastTouched = false;
  static int expression = EX_DEFAULT;
  static uint32_t lastSoilCheck = 0;
  static uint32_t lastRainFrame = 0;

  uint32_t now = millis();
  bool needRedraw = false;

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
    expression = (expression + 1) % EX_COUNT;
    needRedraw = true;
  }
  lastTouched = touched;

  if (needRedraw) renderCat(expression);
}
