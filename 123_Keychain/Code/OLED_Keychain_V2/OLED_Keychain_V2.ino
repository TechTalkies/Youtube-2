/* -------------------------------------------------
   ESP32 OLED Keychain - Ambient Face + Games v6
-------------------------------------------------*/

#include <U8g2lib.h>
#include <Wire.h>

// --- Display Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- Touch Settings ---
#define LEFT_TOUCH_PIN 9
#define RIGHT_TOUCH_PIN 8
#define TOUCH_THRESHOLD 25000

bool leftTouched = false;
bool rightTouched = false;
bool prevLeftTouched = false;
bool prevRightTouched = false;

uint32_t bothPressStart = 0;
bool bothHeldHandled = false;

// --- System States ---
enum SystemMode { AMBIENT_FACE,
                  MENU,
                  GAME_BRICKS,
                  GAME_DODGER };
SystemMode currentMode = AMBIENT_FACE;
int menuSelection = 0;
float menuScrollX = 0;
unsigned long lastMenuActivity = 0;  // Tracks timeout in the menu

// --- Easing and Animation Settings ---
const float EASING = 0.25;
const float ROUNDNESS = 0.45;
const unsigned long FRAME_MS = 33;

const float EYE_CY = 26;
const float MOUTH_CY = 55;
bool isBlinking = false;

float faceScale = 1.0f;
float faceOffsetX = 0.0f;
float faceOffsetY = 0.0f;

// -------------------------------------------------
// Shape Structure
// -------------------------------------------------
struct Eye {
  float x, y, w, h;
  float tx, ty, tw, th;
  float ox, oy;
  bool blinks = true;

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
    float sx = (x - (SCREEN_WIDTH / 2.0f)) * faceScale + (SCREEN_WIDTH / 2.0f) + faceOffsetX;
    float sy = (y - (SCREEN_HEIGHT / 2.0f)) * faceScale + (SCREEN_HEIGHT / 2.0f) + faceOffsetY;
    float sw = w * faceScale;
    float sh = h * faceScale;

    int ww = max(2, (int)roundf(sw));
    int hh = max(2, (int)roundf(sh));
    int left = (int)roundf(sx - sw / 2.0f);
    int top = (int)roundf(sy - sh / 2.0f);

    int minSide = min(ww, hh);
    int r = (int)(minSide * ROUNDNESS);
    r = min(r, minSide / 2 - 1);
    if (r < 0) r = 0;

    u8g2.drawRBox(left, top, ww, hh, r);
  }
};

Eye leftEye, rightEye, mouth;

enum Emotion { NEUTRAL,
               HAPPY,
               SAD,
               SURPRISED,
               SLEEPY,
               ANGRY,
               CONFUSED,
               DAMAGE,
               DEATH };
Emotion currentEmotion = NEUTRAL;
unsigned long lastActionTime = 0;
unsigned long actionInterval = 2000;
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;
unsigned long lastFrameTime = 0;
unsigned long expressionTimer = 0;

// --- Game 1: Bricks Variables ---
float paddleX = 64;
float paddleW = 24;
float ballX = 64, ballY = 40;
float ballVX = 2.5, ballVY = -2.5;
#define BRICK_ROWS 3
#define BRICK_COLS 6
bool bricks[BRICK_ROWS][BRICK_COLS];
bool ballActive = false;
int bricksRemaining = 0;
int faceSide = 1;
float targetFaceOffsetX = 0;
bool bricksLevelComplete = false;
bool bricksDamaged = false;
unsigned long bricksPauseTimer = 0;

// --- Game 2: Dodger Variables ---
float jumpY = 40;
float jumpVY = 0;
float gravity = 0.6;
float obsX = 128;
float obsW = 12, obsH = 15;
bool isJumping = false;
int dodgerScore = 0;
int dodgerLives = 3;
float cloudX[2] = { 128, 64 };
float cloudY[2] = { 8, 18 };
bool dodgerDead = false;
bool dodgerDamaged = false;
unsigned long dodgerPauseTimer = 0;

// -------------------------------------------------
// PROTOTYPES
// -------------------------------------------------
void setEmotion(Emotion emo);
void saccade();
void applyLookDirection(int direction);
void resetBricks();
void resetDodger();
void fullResetDodger();

// -------------------------------------------------
// SETUP
// -------------------------------------------------
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

// -------------------------------------------------
// MAIN LOOP
// -------------------------------------------------
void loop() {
  unsigned long currentMillis = millis();

  if (currentEmotion != DEATH && currentEmotion != DAMAGE) {
    if (!isBlinking && currentMillis - lastBlinkTime > blinkInterval) {
      isBlinking = true;
      lastBlinkTime = currentMillis;
      blinkInterval = random(2000, 6000);
    } else if (isBlinking && currentMillis - lastBlinkTime > 150) {
      isBlinking = false;
      lastBlinkTime = currentMillis;
    }
  } else {
    isBlinking = false;
  }

  if (expressionTimer > 0 && currentMillis > expressionTimer) {
    if (currentEmotion != DEATH && currentEmotion != DAMAGE) {
      setEmotion(NEUTRAL);
    }
    expressionTimer = 0;
  }

  if (currentMillis - lastFrameTime >= FRAME_MS) {
    lastFrameTime = currentMillis;

    leftTouched = touchRead(LEFT_TOUCH_PIN) > TOUCH_THRESHOLD;
    rightTouched = touchRead(RIGHT_TOUCH_PIN) > TOUCH_THRESHOLD;

    bool leftJustPressed = leftTouched && !prevLeftTouched;
    bool rightJustPressed = rightTouched && !prevRightTouched;

    // Handle Long Press Logic
    if (leftTouched && rightTouched) {
      if (bothPressStart == 0) bothPressStart = currentMillis;

      if (currentMillis - bothPressStart > 1500 && !bothHeldHandled) {
        bothHeldHandled = true;
        if (currentMode == MENU) {
          if (menuSelection == 0) {
            currentMode = GAME_BRICKS;
            resetBricks();
          } else {
            currentMode = GAME_DODGER;
            fullResetDodger();
          }
        } else {
          currentMode = MENU;
          faceScale = 1.0f;
          faceOffsetX = 0;
          faceOffsetY = 0;
          targetFaceOffsetX = 0;
          bricksLevelComplete = false;
          bricksDamaged = false;
          dodgerDead = false;
          dodgerDamaged = false;
          setEmotion(NEUTRAL);
          lastMenuActivity = currentMillis;  // Reset timer when entering menu
        }
      }
    } else {
      bothPressStart = 0;
      bothHeldHandled = false;
    }

    // State Machine
    switch (currentMode) {
      case AMBIENT_FACE: updateAmbientFace(currentMillis); break;
      case MENU: updateMenu(leftJustPressed, rightJustPressed, currentMillis); break;
      case GAME_BRICKS: updateBricks(leftJustPressed, rightJustPressed, currentMillis); break;
      case GAME_DODGER: updateDodger(leftJustPressed, rightJustPressed, currentMillis); break;
    }

    leftEye.update();
    rightEye.update();
    mouth.update();

    u8g2.clearBuffer();

    if (currentMode == MENU) {
      drawMenu();
    } else if (currentMode == GAME_BRICKS) {
      drawBricks();
      leftEye.draw();
      rightEye.draw();
      mouth.draw();
    } else if (currentMode == GAME_DODGER) {
      drawDodger();
      leftEye.draw();
      rightEye.draw();
      mouth.draw();
    } else {
      leftEye.draw();
      rightEye.draw();
      mouth.draw();
    }

    u8g2.sendBuffer();

    prevLeftTouched = leftTouched;
    prevRightTouched = rightTouched;
  }
}

// -------------------------------------------------
// STATE LOGIC
// -------------------------------------------------
void updateAmbientFace(unsigned long currentMillis) {
  faceScale = 1.0f;
  faceOffsetX = 0;
  faceOffsetY = 0;

  if (leftTouched && !rightTouched) applyLookDirection(-1);
  else if (rightTouched && !leftTouched) applyLookDirection(1);
  else {
    leftEye.ox = 0;
    rightEye.ox = 0;
    mouth.ox = 0;
    if (currentMillis - lastActionTime > actionInterval) {
      lastActionTime = currentMillis;
      actionInterval = random(1500, 4000);
      if (random(100) < 40) saccade();
      else setEmotion((Emotion)random(0, 7));
    }
  }
}

void updateMenu(bool leftJustPressed, bool rightJustPressed, unsigned long currentMillis) {
  if (leftTouched || rightTouched) {
    lastMenuActivity = currentMillis;  // Reset timeout on any touch
  }

  // Timeout: Return to ambient face after 8 seconds of inactivity
  if (currentMillis - lastMenuActivity > 8000) {
    currentMode = AMBIENT_FACE;
    faceScale = 1.0f;
    faceOffsetX = 0;
    faceOffsetY = 0;
    setEmotion(NEUTRAL);
    return;
  }

  if (!leftTouched || !rightTouched) {
    if (leftJustPressed || rightJustPressed) {
      menuSelection = !menuSelection;
    }
  }
  float targetScrollX = menuSelection * 128.0f;
  menuScrollX += (targetScrollX - menuScrollX) * EASING;
}

void drawMenu() {
  int x0 = 0 - (int)menuScrollX;
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(x0 + 26, 32, "BRICKS");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x0 + 15, 52, "Hold both to start");

  int x1 = 128 - (int)menuScrollX;
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(x1 + 22, 32, "DODGER");
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x1 + 15, 52, "Hold both to start");
}

// -------------------------------------------------
// GAME 1: BRICKS
// -------------------------------------------------
void resetBricks() {
  paddleX = 64;
  ballActive = false;
  faceScale = 0.35f;
  bricksRemaining = 0;
  faceSide = 1;
  bricksLevelComplete = false;
  bricksDamaged = false;

  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      bricks[r][c] = (random(10) > 2);
      if (bricks[r][c]) bricksRemaining++;
    }
  }
  if (bricksRemaining == 0) {
    bricks[0][0] = true;
    bricksRemaining = 1;
  }

  setEmotion(NEUTRAL);
}

void updateBricks(bool leftJustPressed, bool rightJustPressed, unsigned long currentMillis) {
  if (bricksLevelComplete) {
    if (currentMillis > bricksPauseTimer) resetBricks();
    return;
  }

  if (bricksDamaged) {
    if (currentMillis > bricksPauseTimer) {
      bricksDamaged = false;
      setEmotion(NEUTRAL);
    } else {
      return;
    }
  }

  if (leftTouched) {
    paddleX -= 4;
    faceSide = 1;
  }
  if (rightTouched) {
    paddleX += 4;
    faceSide = -1;
  }

  paddleX = constrain(paddleX, paddleW / 2, SCREEN_WIDTH - paddleW / 2);

  if (paddleX < 24) faceSide = 1;
  if (paddleX > SCREEN_WIDTH - 24) faceSide = -1;

  float offsetCenter = paddleX - (SCREEN_WIDTH / 2.0f);
  float sideShift = faceSide * (paddleW / 2.0f + 14.0f);
  targetFaceOffsetX = offsetCenter + sideShift;

  faceOffsetX += (targetFaceOffsetX - faceOffsetX) * 0.3f;
  faceOffsetY = 24;

  if (!ballActive) {
    ballX = paddleX;
    ballY = 54;
    if (leftJustPressed || rightJustPressed) {
      ballActive = true;
      ballVY = -2.5;
      ballVX = leftJustPressed ? -2.5 : 2.5;
      setEmotion(SURPRISED);
      expressionTimer = currentMillis + 500;
    }
  } else {
    ballX += ballVX;
    ballY += ballVY;

    leftEye.ox = (ballX - (faceOffsetX + 64)) * 0.15f;
    rightEye.ox = leftEye.ox;
    leftEye.oy = (ballY - 60) * 0.15f;
    rightEye.oy = leftEye.oy;

    if (ballX <= 0 || ballX >= SCREEN_WIDTH) ballVX = -ballVX;
    if (ballY <= 0) ballVY = -ballVY;

    if (ballY >= 56 && ballY <= 60 && ballX >= paddleX - paddleW / 2 && ballX <= paddleX + paddleW / 2) {
      ballVY = -abs(ballVY);
      ballVX = ((ballX - paddleX) / (paddleW / 2)) * 3.0;
      setEmotion(HAPPY);
      expressionTimer = currentMillis + 500;
    }

    int hitC = ballX / (SCREEN_WIDTH / BRICK_COLS);
    int hitR = ballY / 8;
    if (hitR >= 0 && hitR < BRICK_ROWS && hitC >= 0 && hitC < BRICK_COLS) {
      if (bricks[hitR][hitC]) {
        bricks[hitR][hitC] = false;
        ballVY = -ballVY;
        bricksRemaining--;

        if (bricksRemaining <= 0) {
          ballActive = false;
          bricksLevelComplete = true;
          setEmotion(HAPPY);
          bricksPauseTimer = currentMillis + 2000;
        } else {
          setEmotion(SURPRISED);
          expressionTimer = currentMillis + 300;
        }
      }
    }

    if (ballY > SCREEN_HEIGHT) {
      ballActive = false;
      bricksDamaged = true;
      setEmotion(DAMAGE);
      bricksPauseTimer = currentMillis + 1500;
    }
  }
}

void drawBricks() {
  u8g2.drawRBox(paddleX - paddleW / 2, 58, paddleW, 6, 2);
  if (ballActive || !bricksDamaged) {
    u8g2.drawDisc(ballX, ballY, 2);
  }

  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (bricks[r][c]) {
        int bw = SCREEN_WIDTH / BRICK_COLS;
        u8g2.drawBox(c * bw + 1, r * 8 + 1, bw - 2, 6);
      }
    }
  }
}

// -------------------------------------------------
// GAME 2: DODGER
// -------------------------------------------------
void fullResetDodger() {
  dodgerScore = 0;
  dodgerLives = 3;
  resetDodger();
}

void resetDodger() {
  jumpY = 40;
  jumpVY = 0;
  isJumping = false;
  obsX = 128;
  faceScale = 0.4f;
  faceOffsetX = -40;
  dodgerDead = false;
  dodgerDamaged = false;
  setEmotion(NEUTRAL);
}

void drawHeart(int x, int y) {
  u8g2.drawBox(x + 1, y, 2, 2);
  u8g2.drawBox(x + 4, y, 2, 2);
  u8g2.drawBox(x, y + 2, 7, 2);
  u8g2.drawBox(x + 1, y + 4, 5, 1);
  u8g2.drawBox(x + 2, y + 5, 3, 1);
  u8g2.drawPixel(x + 3, y + 6);
}

void updateDodger(bool leftJustPressed, bool rightJustPressed, unsigned long currentMillis) {
  if (dodgerDead) {
    if (currentMillis > dodgerPauseTimer) fullResetDodger();
    return;
  }

  if (dodgerDamaged) {
    if (currentMillis > dodgerPauseTimer) {
      dodgerDamaged = false;
      setEmotion(NEUTRAL);
    } else {
      return;
    }
  }

  cloudX[0] -= 0.3;
  if (cloudX[0] < -20) cloudX[0] = 128;
  cloudX[1] -= 0.6;
  if (cloudX[1] < -20) cloudX[1] = 128;

  if ((leftJustPressed || rightJustPressed) && !isJumping) {
    jumpVY = -6.5;
    isJumping = true;
    setEmotion(SURPRISED);
  }

  jumpVY += gravity;
  jumpY += jumpVY;

  if (jumpY >= 50) {
    jumpY = 50;
    jumpVY = 0;
    if (isJumping) {
      isJumping = false;
      setEmotion(NEUTRAL);
    }
  } else if (jumpVY > 1.0) {
    if (currentEmotion != SLEEPY) setEmotion(SLEEPY);
  }

  faceOffsetY = jumpY - (SCREEN_HEIGHT / 2.0f);

  obsX -= 3.5;
  if (obsX < -obsW) {
    obsX = 128;
    dodgerScore++;
    obsH = random(12, 22);
  }

  float faceRealX = (SCREEN_WIDTH / 2.0f) + faceOffsetX;
  if (obsX < faceRealX + 10 && obsX + obsW > faceRealX - 10) {
    if (jumpY + 12 > 64 - obsH) {
      dodgerLives--;
      if (dodgerLives <= 0) {
        dodgerDead = true;
        setEmotion(DEATH);
        dodgerPauseTimer = currentMillis + 2500;
      } else {
        dodgerDamaged = true;
        setEmotion(DAMAGE);
        dodgerPauseTimer = currentMillis + 1000;
        obsX = 128;
      }
    }
  }
}

void drawDodger() {
  for (int i = 0; i < 2; i++) {
    u8g2.drawRBox(cloudX[i], cloudY[i], 16, 8, 4);
    u8g2.drawRBox(cloudX[i] + 3, cloudY[i] - 4, 10, 8, 4);
  }

  u8g2.drawBox(0, 62, 128, 2);
  u8g2.drawTriangle(obsX + obsW / 2, 64 - obsH, obsX, 64, obsX + obsW, 64);

  for (int i = 0; i < dodgerLives; i++) {
    drawHeart(4 + (i * 10), 2);
  }
}

// -------------------------------------------------
// EMOTION & ANIMATION HELPERS
// -------------------------------------------------
void setEmotion(Emotion emo) {
  currentEmotion = emo;
  float cy = EYE_CY;
  float lx = (SCREEN_WIDTH / 2) - 24;
  float rx = (SCREEN_WIDTH / 2) + 24;
  float mx = SCREEN_WIDTH / 2;
  float my = MOUTH_CY;
  leftEye.ox = 0;
  leftEye.oy = 0;
  rightEye.ox = 0;
  rightEye.oy = 0;
  mouth.ox = 0;
  mouth.oy = 0;

  switch (emo) {
    case NEUTRAL:
      leftEye.tx = lx;
      leftEye.ty = cy;
      leftEye.tw = 24;
      leftEye.th = 40;
      rightEye.tx = rx;
      rightEye.ty = cy;
      rightEye.tw = 24;
      rightEye.th = 40;
      mouth.tx = mx;
      mouth.ty = my;
      mouth.tw = 16;
      mouth.th = 4;
      break;
    case HAPPY:
      leftEye.tx = lx;
      leftEye.ty = cy - 6;
      leftEye.tw = 28;
      leftEye.th = 20;
      rightEye.tx = rx;
      rightEye.ty = cy - 6;
      rightEye.tw = 28;
      rightEye.th = 20;
      mouth.tx = mx;
      mouth.ty = my;
      mouth.tw = 30;
      mouth.th = 9;
      break;
    case SAD:
      leftEye.tx = lx + 4;
      leftEye.ty = cy + 8;
      leftEye.tw = 20;
      leftEye.th = 24;
      rightEye.tx = rx - 4;
      rightEye.ty = cy + 8;
      rightEye.tw = 20;
      rightEye.th = 24;
      mouth.tx = mx;
      mouth.ty = my + 1;
      mouth.tw = 12;
      mouth.th = 4;
      break;
    case SURPRISED:
      leftEye.tx = lx;
      leftEye.ty = cy - 6;
      leftEye.tw = 20;
      leftEye.th = 44;
      rightEye.tx = rx;
      rightEye.ty = cy - 6;
      rightEye.tw = 20;
      rightEye.th = 44;
      mouth.tx = mx;
      mouth.ty = my + 1;
      mouth.tw = 10;
      mouth.th = 11;
      break;
    case SLEEPY:
      leftEye.tx = lx;
      leftEye.ty = cy + 12;
      leftEye.tw = 24;
      leftEye.th = 8;
      rightEye.tx = rx;
      rightEye.ty = cy + 12;
      rightEye.tw = 24;
      rightEye.th = 8;
      mouth.tx = mx;
      mouth.ty = my + 1;
      mouth.tw = 8;
      mouth.th = 3;
      break;
    case ANGRY:
      leftEye.tx = lx + 8;
      leftEye.ty = cy + 4;
      leftEye.tw = 22;
      leftEye.th = 22;
      rightEye.tx = rx - 8;
      rightEye.ty = cy + 4;
      rightEye.tw = 22;
      rightEye.th = 22;
      mouth.tx = mx;
      mouth.ty = my;
      mouth.tw = 22;
      mouth.th = 3;
      break;
    case CONFUSED:
      leftEye.tx = lx - 3;
      leftEye.ty = cy - 4;
      leftEye.tw = 27;
      leftEye.th = 32;
      rightEye.tx = rx + 5;
      rightEye.ty = cy + 5;
      rightEye.tw = 18;
      rightEye.th = 23;
      mouth.tx = mx;
      mouth.ty = my;
      mouth.tw = 10;
      mouth.th = 8;
      break;
    case DAMAGE:
      leftEye.tx = lx;
      leftEye.ty = cy + 8;
      leftEye.tw = 20;
      leftEye.th = 6;
      rightEye.tx = rx;
      rightEye.ty = cy - 4;
      rightEye.tw = 26;
      rightEye.th = 36;
      mouth.tx = mx + 2;
      mouth.ty = my + 2;
      mouth.tw = 8;
      mouth.th = 8;
      break;
    case DEATH:
      leftEye.tx = lx;
      leftEye.ty = cy + 12;
      leftEye.tw = 24;
      leftEye.th = 2;
      rightEye.tx = rx;
      rightEye.ty = cy + 12;
      rightEye.tw = 24;
      rightEye.th = 2;
      mouth.tx = mx;
      mouth.ty = my + 8;
      mouth.tw = 20;
      mouth.th = 4;
      break;
  }
}

void applyLookDirection(int direction) {
  // Increased from 11.0f to 26.0f for a much wider, dramatic turn
  float lookX = direction * 26.0f;

  leftEye.ox = lookX;
  rightEye.ox = lookX;

  // Add a slight vertical dip so it looks like it's leaning into the touch
  leftEye.oy = 4.0f;
  rightEye.oy = 4.0f;

  // Make the mouth stretch further to follow the eyes
  mouth.ox = lookX * 0.65f;
  mouth.oy = 2.0f;
}

void saccade() {
  float offsetX = random(-12, 13);
  float offsetY = random(-8, 9);
  leftEye.ox = offsetX;
  rightEye.ox = offsetX;
  leftEye.oy = offsetY;
  rightEye.oy = offsetY;
  mouth.ox = offsetX * 0.5f;
  mouth.oy = 0;
}