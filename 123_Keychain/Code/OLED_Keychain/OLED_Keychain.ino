/* -------------------------------------------------
Copyright (c)
Arduino project by Tech Talkies YouTube Channel.
https://www.youtube.com/@techtalkies1
-------------------------------------------------*/

#include <U8g2lib.h>
#include <Wire.h>

// --- Display Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// --- Easing and Animation Settings ---
const float EASING = 0.25;

// Corner roundness as a fraction of the smaller side (0.5 = full pill/capsule)
const float ROUNDNESS = 0.45;

// Frame cap so easing speed is consistent
const unsigned long FRAME_MS = 33;  // ~30 fps

// Eyes sit higher to leave room for the mouth
const float EYE_CY = 26;
const float MOUTH_CY = 55;

bool isBlinking = false;

// --- Shape Structure (used for eyes and mouth) ---
struct Eye {
  float x, y, w, h;      // Current rendering values
  float tx, ty, tw, th;  // Target values (emotion base shape)
  float ox, oy;          // Saccade offset (kept separate so it never accumulates)
  bool blinks = true;    // Mouth sets this to false

  void snap() {
    x = tx + ox;
    y = ty + oy;
    w = tw;
    h = th;
  }

  void update() {
    float eh = (blinks && isBlinking) ? 2 : th;
    x += (tx + ox - x) * EASING;
    y += (ty + oy - y) * EASING;
    w += (tw - w) * EASING;
    h += (eh - h) * EASING;
  }

  void draw() {
    int ww = max(2, (int)roundf(w));
    int hh = max(2, (int)roundf(h));
    int left = (int)roundf(x - w / 2.0f);
    int top = (int)roundf(y - h / 2.0f);

    left = constrain(left, 0, SCREEN_WIDTH - ww);
    top = constrain(top, 0, SCREEN_HEIGHT - hh);

    // u8g2 needs w,h >= 2*(r+1) or drawRBox draws nothing/garbage
    int minSide = min(ww, hh);
    int r = (int)(minSide * ROUNDNESS);
    r = min(r, minSide / 2 - 1);
    if (r < 0) r = 0;

    u8g2.drawRBox(left, top, ww, hh, r);
  }
};

Eye leftEye, rightEye, mouth;

// --- Emotion States ---
enum Emotion { NEUTRAL,
               HAPPY,
               SAD,
               SURPRISED,
               SLEEPY,
               ANGRY };
Emotion currentEmotion = NEUTRAL;

unsigned long lastActionTime = 0;
unsigned long actionInterval = 2000;

unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;

unsigned long lastFrameTime = 0;

void setup() {
  Serial.begin(115200);
  delay(250);

  u8g2.begin();
  Wire.setClock(400000);

  mouth.blinks = false;

  setEmotion(NEUTRAL);
  leftEye.snap();
  rightEye.snap();
  mouth.snap();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Handle Spontaneous Blinking
  if (!isBlinking && currentMillis - lastBlinkTime > blinkInterval) {
    isBlinking = true;
    lastBlinkTime = currentMillis;
    blinkInterval = random(2000, 6000);
  } else if (isBlinking && currentMillis - lastBlinkTime > 150) {
    isBlinking = false;
    lastBlinkTime = currentMillis;
  }

  // 2. State Machine: Randomly pick new emotion or look around
  if (currentMillis - lastActionTime > actionInterval) {
    lastActionTime = currentMillis;
    actionInterval = random(1500, 4000);

    int action = random(100);

    if (action < 40) {
      saccade();
    } else {
      int nextEmo = random(0, 6);
      setEmotion((Emotion)nextEmo);
    }
  }

  // 3. Frame-limited update + render
  if (currentMillis - lastFrameTime >= FRAME_MS) {
    lastFrameTime = currentMillis;

    leftEye.update();
    rightEye.update();
    mouth.update();

    u8g2.clearBuffer();
    leftEye.draw();
    rightEye.draw();
    mouth.draw();
    u8g2.sendBuffer();
  }
}

// --- Animation Helpers ---

void setEmotion(Emotion emo) {
  currentEmotion = emo;
  float cy = EYE_CY;
  float lx = (SCREEN_WIDTH / 2) - 24;
  float rx = (SCREEN_WIDTH / 2) + 24;
  float mx = SCREEN_WIDTH / 2;
  float my = MOUTH_CY;

  // New emotion starts with everything centered again
  leftEye.ox = leftEye.oy = 0;
  rightEye.ox = rightEye.oy = 0;
  mouth.ox = mouth.oy = 0;

  switch (emo) {
    case NEUTRAL:
      setEyeTargets(lx, cy, 24, 40, rx, cy, 24, 40);
      setMouthTargets(mx, my, 16, 4);
      break;
    case HAPPY:
      setEyeTargets(lx, cy - 6, 28, 20, rx, cy - 6, 28, 20);
      setMouthTargets(mx, my, 30, 9);  // wide open grin
      break;
    case SAD:
      setEyeTargets(lx + 4, cy + 8, 20, 24, rx - 4, cy + 8, 20, 24);
      setMouthTargets(mx, my + 1, 12, 4);  // small and low
      break;
    case SURPRISED:
      setEyeTargets(lx, cy - 6, 20, 44, rx, cy - 6, 20, 44);
      setMouthTargets(mx, my + 1, 10, 11);  // round "o"
      break;
    case SLEEPY:
      setEyeTargets(lx, cy + 12, 24, 8, rx, cy + 12, 24, 8);
      setMouthTargets(mx, my + 1, 8, 3);  // tiny
      break;
    case ANGRY:
      setEyeTargets(lx + 8, cy + 4, 22, 22, rx - 8, cy + 4, 22, 22);
      setMouthTargets(mx, my, 22, 3);  // thin flat line
      break;
  }
}

// Saccade: sets (not accumulates) a small offset; mouth follows at half strength
void saccade() {
  float offsetX = random(-12, 13);
  float offsetY = random(-8, 9);

  leftEye.ox = rightEye.ox = offsetX;
  leftEye.oy = rightEye.oy = offsetY;
  mouth.ox = offsetX * 0.5f;
  mouth.oy = 0;
}

void setEyeTargets(float lx, float ly, float lw, float lh,
                   float rx, float ry, float rw, float rh) {
  leftEye.tx = lx;
  leftEye.ty = ly;
  leftEye.tw = lw;
  leftEye.th = lh;
  rightEye.tx = rx;
  rightEye.ty = ry;
  rightEye.tw = rw;
  rightEye.th = rh;
}

void setMouthTargets(float x, float y, float w, float h) {
  mouth.tx = x;
  mouth.ty = y;
  mouth.tw = w;
  mouth.th = h;
}