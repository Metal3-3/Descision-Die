#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
#define BTN_PIN   D10      // push button module OUT
#define AD0_PIN   D1       // MPU AD0 (driven LOW -> address 0x68)
#define BTN_ACTIVE_HIGH true   // set false if your button module pulls LOW when pressed

// ---------- Display ----------
#define SCREEN_W 128
#define SCREEN_H 64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

// ---------- MPU6050 ----------
#define MPU_ADDR 0x68

// ---------- Shake tuning ----------
const float SHAKE_THRESHOLD_G = 0.6;   // deviation from 1g that counts as "shaking"
const uint16_t SHAKE_END_MS   = 400;   // how long it must be still before result
const uint16_t MIN_SHAKE_MS   = 300;   // ignore tiny bumps shorter than this

// ---------- Modes ----------
enum Mode { MODE_DIE, MODE_8BALL, MODE_COIN, MODE_RANDOM, MODE_COUNT };
const char* modeNames[] = { "DIE (D6)", "MAGIC 8-BALL", "COIN FLIP", "RANDOM MIX" };

const char* answers8[] = {
  "YES", "NO", "MAYBE", "DEFINITELY", "NOT LIKELY", "ASK LATER", "LOOKS GOOD", "DOUBTFUL"
};
const uint8_t NUM_ANSWERS = sizeof(answers8) / sizeof(answers8[0]);

Mode mode = MODE_DIE;
bool hasResult = false;
String resultText = "";
int dieValue = 1;

bool shaking = false;
uint32_t shakeStart = 0, lastShakeMs = 0, lastAnim = 0;
uint32_t entropy = 0;

// ---------- Button ----------
bool lastBtn = false;
uint32_t lastBtnChange = 0;

bool readButton() {
  bool raw = digitalRead(BTN_PIN);
  return BTN_ACTIVE_HIGH ? raw : !raw;
}

// ---------- MPU helpers ----------
void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) return false;
  int16_t x = (Wire.read() << 8) | Wire.read();
  int16_t y = (Wire.read() << 8) | Wire.read();
  int16_t z = (Wire.read() << 8) | Wire.read();
  entropy ^= ((uint32_t)(x ^ y ^ z) << (micros() & 7)) + micros();
  ax = x / 4096.0;   // +/-8g range -> 4096 LSB/g
  ay = y / 4096.0;
  az = z / 4096.0;
  return true;
}

// ---------- Drawing ----------
void drawCentered(const String &s, uint8_t size, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_W - w) / 2, y);
  display.print(s);
}

void drawHeader() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(modeNames[mode]);
  display.setCursor(110, 0);
  display.print(mode + 1);
  display.print("/");
  display.print(MODE_COUNT);
  display.drawFastHLine(0, 10, SCREEN_W, SSD1306_WHITE);
}

void drawDie(int v) {
  static const uint16_t masks[7] = { 0, 16, 257, 273, 325, 341, 365 };
  const int bx = 40, by = 14, bs = 48;
  display.drawRoundRect(bx, by, bs, bs, 6, SSD1306_WHITE);
  for (int i = 0; i < 9; i++) {
    if (masks[v] & (1 << i)) {
      int col = i % 3, row = i / 3;
      display.fillCircle(bx + 11 + col * 13, by + 11 + row * 13, 4, SSD1306_WHITE);
    }
  }
}

void drawResultText(const String &s) {
  uint8_t size = (s.length() <= 6) ? 3 : (s.length() <= 10 ? 2 : 1);
  if (s.length() > 6 && s.length() <= 10) size = 2;
  drawCentered(s, size, 30);
}

void render(bool animating, int animDie, const String &animText) {
  display.clearDisplay();
  drawHeader();

  if (animating) {
    if (mode == MODE_DIE) drawDie(animDie);
    else drawResultText(animText);
  } else if (!hasResult) {
    drawCentered("SHAKE ME!", 2, 30);
  } else {
    if (mode == MODE_DIE) drawDie(dieValue);
    else drawResultText(resultText);
  }
  display.display();
}

// ---------- Result generation ----------
String randomMixResult() {
  switch (random(5)) {       // each of the 5 outcomes equally likely
    case 0: return "HEADS";
    case 1: return "TAILS";
    case 2: return "YES";
    case 3: return "NO";
    default: return String(random(1, 1000001));   // 1 - 1,000,000
  }
}

String makeText(Mode m) {
  switch (m) {
    case MODE_8BALL:  return answers8[random(NUM_ANSWERS)];
    case MODE_COIN:   return random(2) ? "HEADS" : "TAILS";
    case MODE_RANDOM: return randomMixResult();
    default:          return "";
  }
}

void finalizeRoll() {
  randomSeed(entropy ^ micros());
  if (mode == MODE_DIE) dieValue = random(1, 7);
  else resultText = makeText(mode);
  hasResult = true;
  render(false, 0, "");
}

// ---------- Setup ----------
void setup() {
  pinMode(BTN_PIN, INPUT);
  pinMode(AD0_PIN, OUTPUT);
  digitalWrite(AD0_PIN, LOW);

  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) delay(100);   // OLED not found
  }
  display.clearDisplay();
  display.display();

  mpuWrite(0x6B, 0x00);   // wake up
  mpuWrite(0x1C, 0x10);   // accel range +/-8g
  mpuWrite(0x1A, 0x03);   // low-pass filter

  randomSeed(analogRead(A0) ^ micros());
  render(false, 0, "");
}

// ---------- Loop ----------
void loop() {
  uint32_t now = millis();

  // Button: press cycles mode
  bool btn = readButton();
  if (btn != lastBtn && now - lastBtnChange > 40) {
    lastBtnChange = now;
    lastBtn = btn;
    if (btn) {
      mode = (Mode)((mode + 1) % MODE_COUNT);
      hasResult = false;
      render(false, 0, "");
    }
  }

  // Shake detection
  float ax, ay, az;
  if (readAccel(ax, ay, az)) {
    float mag = sqrt(ax * ax + ay * ay + az * az);
    if (fabs(mag - 1.0) > SHAKE_THRESHOLD_G) {
      lastShakeMs = now;
      if (!shaking) {
        shaking = true;
        shakeStart = now;
      }
    }
  }

  // Rolling animation while shaking
  if (shaking) {
    if (now - lastAnim > 80) {
      lastAnim = now;
      render(true, random(1, 7), makeText(mode == MODE_DIE ? MODE_RANDOM : mode));
    }
    // Shake finished?
    if (now - lastShakeMs > SHAKE_END_MS) {
      shaking = false;
      if (lastShakeMs - shakeStart >= MIN_SHAKE_MS) finalizeRoll();
      else render(false, 0, "");   // was just a bump
    }
  }

  delay(5);
}