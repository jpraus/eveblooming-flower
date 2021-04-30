#include "floower.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define LOG_TAG ""
#else
#include "esp_log.h"
static const char* LOG_TAG = "Floower";
#endif

#define NEOPIXEL_PIN 27
#define NEOPIXEL_PWR_PIN 25

#define TMC_EN_PIN 33
#define TMC_STEP_PIN 18
#define TMC_DIR_PIN 19
#define TMC_UART_RX_PIN 26
#define TMC_UART_TX_PIN 14
#define TMC_DRIVER_ADDRESS 0b00 // TMC2209 Driver address according to MS1 and MS2
#define TMC_R_SENSE 0.13f       // Match to your driver Rsense

#define BATTERY_ANALOG_PIN 36 // VP
#define USB_ANALOG_PIN 39 // VN
#define CHARGE_PIN 35

#define STATUS_NEOPIXEL_PIN 32

#define TOUCH_SENSOR_PIN 4
#define TOUCH_FADE_TIME 75 // 50
#define TOUCH_LONG_TIME_THRESHOLD 2000 // 2s to recognize long touch
#define TOUCH_HOLD_TIME_THRESHOLD 5000 // 5s to recognize hold touch
#define TOUCH_COOLDOWN_TIME 300 // prevent random touch within 300ms after last touch

unsigned long Floower::touchStartedTime = 0;
unsigned long Floower::touchEndedTime = 0;
unsigned long Floower::lastTouchTime = 0;

const HsbColor candleColor(0.042, 1.0, 1.0); // candle orange color

Floower::Floower(Config *config) : config(config), animations(2), pixels(7, NEOPIXEL_PIN), statusPixel(2, STATUS_NEOPIXEL_PIN), driver(&Serial, TMC_R_SENSE, TMC_DRIVER_ADDRESS) {}

void Floower::init() {
  // LEDs
  pixelsPowerOn = true; // to make setPixelsPowerOn effective
  setPixelsPowerOn(false);
  pinMode(NEOPIXEL_PWR_PIN, OUTPUT);

  pixelsColor = colorBlack;
  HsbColor pixelsOriginColor = colorBlack;
  HsbColor pixelsTargetColor = colorBlack;
  pixels.Begin();
  showColor(pixelsColor);
  pixels.Show();

  // configure ADC for battery level reading
  analogReadResolution(12); // se0t 12bit resolution (0-4095)
  analogSetAttenuation(ADC_11db); // set AREF to be 3.6V
  analogSetCycles(8); // num of cycles per sample, 8 is default optimal
  analogSetSamples(1); // num of samples

  // charge state input
  pinMode(CHARGE_PIN, INPUT);
  statusPixel.Begin();
  statusPixel.ClearTo(statusColor);
  statusPixel.Show();

  // this need to be done in init in order to enable deep sleep wake up
  enableTouch();
}

void Floower::initStepper() {
  // default stepper configuration
  //servoOpenAngle = config->servoOpen;
  //servoClosedAngle = config->servoClosed;
  //servoAngle = config->servoClosed;
  //servoOriginAngle = config->servoClosed;
  //servoTargetAngle = config->servoClosed;
  //petalsOpenLevel = -1; // 0-100% (-1 unknown)

  // stepper
  stepperPowerOn = true; // to make setStepperPowerOn effective
  setStepperPowerOn(false);
  pinMode(TMC_EN_PIN, OUTPUT);
}

void Floower::update() {
  animations.UpdateAnimations();
  unsigned long now = millis();
  handleTimers(now);

  // show pixels
  if (pixelsColor.B > 0) {
    setPixelsPowerOn(true);
    if (pixels.IsDirty() && pixels.CanShow()) {
      pixels.Show();
    }
  }
  else if (pixelsPowerOn) {
    pixels.Show();
    setPixelsPowerOn(false);
  }
  statusPixel.Show();

  if (touchStartedTime > 0) {
    unsigned int touchTime = now - touchStartedTime;
    unsigned long sinceLastTouch = millis() - lastTouchTime;

    if (!touchRegistered) {
      ESP_LOGD(LOG_TAG, "Touch Down");
      touchRegistered = true;
      if (touchCallback != nullptr) {
        touchCallback(FloowerTouchEvent::TOUCH_DOWN);
      }
    }
    if (!longTouchRegistered && touchTime > TOUCH_LONG_TIME_THRESHOLD) {
      ESP_LOGD(LOG_TAG, "Long Touch %d", touchTime);
      longTouchRegistered = true;
      if (touchCallback != nullptr) {
        touchCallback(FloowerTouchEvent::TOUCH_LONG);
      }
    }
    if (!holdTouchRegistered && touchTime > TOUCH_HOLD_TIME_THRESHOLD) {
      ESP_LOGD(LOG_TAG, "Hold Touch %d", touchTime);
      holdTouchRegistered = true;
      if (touchCallback != nullptr) {
        touchCallback(FloowerTouchEvent::TOUCH_HOLD);
      }
    }
    if (sinceLastTouch > TOUCH_FADE_TIME) {
      ESP_LOGD(LOG_TAG, "Touch Up %d", sinceLastTouch);
      touchStartedTime = 0;
      touchEndedTime = now;
      touchRegistered = false;
      longTouchRegistered = false;
      holdTouchRegistered = false;
      if (touchCallback != nullptr) {
        touchCallback(FloowerTouchEvent::TOUCH_UP);
      }
    }
  }
  else if (touchEndedTime > 0 && millis() - touchEndedTime > TOUCH_COOLDOWN_TIME) {
    ESP_LOGD(LOG_TAG, "Touch enabled");
    touchEndedTime = 0;
  }
}

void Floower::registerOutsideTouch() {
  touchISR();
}

void Floower::enableTouch() {
  detachInterrupt(TOUCH_SENSOR_PIN);
  touchAttachInterrupt(TOUCH_SENSOR_PIN, Floower::touchISR, config->personification.touchThreshold);
}

void Floower::touchISR() {
  lastTouchTime = millis();
  if (touchStartedTime == 0 && touchEndedTime == 0) {
    touchStartedTime = lastTouchTime;
  }
}

void Floower::onLeafTouch(FloowerOnLeafTouchCallback callback) {
  touchCallback = callback;
}

void Floower::onChange(FloowerChangeCallback callback) {
  changeCallback = callback;
}

void Floower::setPetalsOpenLevel(uint8_t level, int transitionTime) {
  if (level == petalsOpenLevel) {
    return; // no change, keep doing the old movement until done
  }
  petalsOpenLevel = level;

  /*if (level >= 100) {
    setPetalsAngle(servoOpenAngle, transitionTime);
  }
  else {
    float position = (servoOpenAngle - servoClosedAngle);
    position = position * level / 100.0;
    setPetalsAngle(servoClosedAngle + position, transitionTime);
  }*/
}

void Floower::setPetalsAngle(unsigned int angle, int transitionTime) {
  /*servoOriginAngle = servoAngle;
  servoTargetAngle = angle;

  ESP_LOGI(LOG_TAG, "Petals %d%% (%d) in %d", petalsOpenLevel, angle, transitionTime);

  // TODO: support transitionTime of 0
  animations.StartAnimation(0, transitionTime, [=](const AnimationParam& param){ servoAnimationUpdate(param); });

  if (changeCallback != nullptr) {
    changeCallback(petalsOpenLevel, pixelsTargetColor);
  }*/
}

void Floower::stepperAnimationUpdate(const AnimationParam& param) {
  /*servoAngle = servoOriginAngle + (servoTargetAngle - servoOriginAngle) * param.progress;

  setServoPowerOn(true);
  servo.write(servoAngle);

  if (param.state == AnimationState_Completed) {
    servoPowerOffTime = millis() + SERVO_POWER_OFF_DELAY;
  }*/
}

uint8_t Floower::getPetalsOpenLevel() {
  return petalsOpenLevel;
}

uint8_t Floower::getCurrentPetalsOpenLevel() {
  /*if (arePetalsMoving()) {
    float range = servoOpenAngle - servoClosedAngle;
    float current = servoAngle - servoClosedAngle;
    return (current / range) * 100;
  }*/
  return petalsOpenLevel;
}

int Floower::getPetalsAngle() {
  //return servoTargetAngle;
  return -1;
}

int Floower::getCurrentPetalsAngle() {
  //return servoAngle;
  return -1;
}

void Floower::transitionColorBrightness(double brightness, int transitionTime) {
  if (brightness == pixelsTargetColor.B) {
    return; // no change
  }
  transitionColor(pixelsTargetColor.H, pixelsTargetColor.S, brightness, transitionTime);
}

void Floower::transitionColor(double hue, double saturation, double brightness, int transitionTime) {
  if (hue == pixelsTargetColor.H && saturation == pixelsTargetColor.S && brightness == pixelsTargetColor.B) {
    return; // no change
  }

  // make smooth transition
  if (pixelsColor.B == 0) { // current color is black
    pixelsColor.H = hue;
    pixelsColor.S = saturation;
  }
  else if (brightness == 0) { // target color is black
    hue = pixelsColor.H;
    saturation = pixelsColor.S;
  }

  pixelsTargetColor = HsbColor(hue, saturation, brightness);
  interruptiblePixelsAnimation = false;

  ESP_LOGI(LOG_TAG, "Color %.2f,%.2f,%.2f", pixelsTargetColor.H, pixelsTargetColor.S, pixelsTargetColor.B);

  if (transitionTime <= 0) {
    pixelsColor = pixelsTargetColor;
    showColor(pixelsColor);
  }
  else {
    pixelsOriginColor = pixelsColor;
    animations.StartAnimation(1, transitionTime, [=](const AnimationParam& param){ pixelsTransitionAnimationUpdate(param); });  
  }

  if (changeCallback != nullptr) {
    changeCallback(petalsOpenLevel, pixelsTargetColor);
  }
}

void Floower::pixelsTransitionAnimationUpdate(const AnimationParam& param) {
  double diff = pixelsOriginColor.H - pixelsTargetColor.H;
  if (diff < 0.2 && diff > -0.2) {
    pixelsColor = HsbColor::LinearBlend<NeoHueBlendShortestDistance>(pixelsOriginColor, pixelsTargetColor, param.progress);
  }
  else {
    pixelsColor = RgbColor::LinearBlend(pixelsOriginColor, pixelsTargetColor, param.progress);
  }
  showColor(pixelsColor);
}

void Floower::flashColor(double hue, double saturation, int flashDuration) {
  pixelsTargetColor = HsbColor(hue, saturation, 1.0);
  pixelsColor = pixelsTargetColor;
  pixelsColor.B = 0;

  interruptiblePixelsAnimation = false;
  animations.StartAnimation(1, flashDuration, [=](const AnimationParam& param){ pixelsFlashAnimationUpdate(param); });
}

void Floower::pixelsFlashAnimationUpdate(const AnimationParam& param) {
  if (param.progress < 0.5) {
    pixelsColor.B = NeoEase::CubicInOut(param.progress * 2);
  }
  else {
    pixelsColor.B = NeoEase::CubicInOut((1 - param.progress) * 2);
  }

  showColor(pixelsColor);

  if (param.state == AnimationState_Completed) {
    if (pixelsTargetColor.B > 0) { // while there is something to show
      animations.RestartAnimation(param.index);
    }
  }
}

HsbColor Floower::getColor() {
  return pixelsTargetColor;
}

HsbColor Floower::getCurrentColor() {
  return pixelsColor;
}

void Floower::startAnimation(FloowerColorAnimation animation) {
  interruptiblePixelsAnimation = true;

  if (animation == RAINBOW) {
    pixelsOriginColor = pixelsColor;
    animations.StartAnimation(1, 10000, [=](const AnimationParam& param){ pixelsRainbowAnimationUpdate(param); });
  }
  else if (animation == RAINBOW_LOOP) {
    pixelsTargetColor = pixelsColor = colorWhite;
    animations.StartAnimation(1, 10000, [=](const AnimationParam& param){ pixelsRainbowLoopAnimationUpdate(param); });
  }
  else if (animation == CANDLE) {
    pixelsTargetColor = pixelsColor = candleColor; // candle orange
    for (uint8_t i = 0; i < 6; i++) {
      candleOriginColors[i] = pixelsTargetColor;
      candleTargetColors[i] = pixelsTargetColor;
    }
    animations.StartAnimation(1, 100, [=](const AnimationParam& param){ pixelsCandleAnimationUpdate(param); });
  }
}

void Floower::stopAnimation(bool retainColor) {
  animations.StopAnimation(1);
  if (retainColor) {
    pixelsTargetColor = pixelsColor;
  }
}

void Floower::pixelsRainbowAnimationUpdate(const AnimationParam& param) {
  float hue = pixelsOriginColor.H + param.progress;
  if (hue >= 1.0) {
    hue = hue - 1;
  }
  pixelsColor = HsbColor(hue, 1, pixelsOriginColor.B);
  showColor(pixelsColor);

  if (param.state == AnimationState_Completed) {
    if (pixelsTargetColor.B > 0) { // while there is something to show
      animations.RestartAnimation(param.index);
    }
  }
}

void Floower::pixelsRainbowLoopAnimationUpdate(const AnimationParam& param) {
  double hue = param.progress;
  double step = 1.0 / 6.0;
  pixels.SetPixelColor(0, HsbColor(param.progress, 1, config->colorBrightness));
  for (uint8_t i = 1; i < 7; i++, hue += step) {
    if (hue >= 1.0) {
      hue = hue - 1;
    }
    pixels.SetPixelColor(i, HsbColor(hue, 1, config->colorBrightness));
  }
  if (param.state == AnimationState_Completed) {
    animations.RestartAnimation(param.index);
  }
}

void Floower::pixelsCandleAnimationUpdate(const AnimationParam& param) {
  pixels.SetPixelColor(0, pixelsTargetColor);
  for (uint8_t i = 0; i < 6; i++) {
    pixels.SetPixelColor(i + 1, HsbColor::LinearBlend<NeoHueBlendShortestDistance>(candleOriginColors[i], candleTargetColors[i], param.progress));
  }

  if (param.state == AnimationState_Completed) {
    for (uint8_t i = 0; i < 6; i++) {
      candleOriginColors[i] = candleTargetColors[i];
      candleTargetColors[i] = HsbColor(candleColor.H, candleColor.S, random(20, 100) / 100.0);
    }
    animations.StartAnimation(param.index, random(10, 400), [=](const AnimationParam& param){ pixelsCandleAnimationUpdate(param); });
  }
}

void Floower::showColor(HsbColor color) {
  if (!lowPowerMode) {
    pixels.ClearTo(color);
  }
  else {
    pixels.ClearTo(colorBlack);
    pixels.SetPixelColor(0, color);
  }
}

bool Floower::isLit() {
  return pixelsPowerOn;
}

bool Floower::isAnimating() {
  return !animations.IsAnimating();
}

bool Floower::arePetalsMoving() {
  return animations.IsAnimationActive(0);
}

bool Floower::isChangingColor() {
  return (!interruptiblePixelsAnimation && animations.IsAnimationActive(1));
}

void Floower::acty() {
  // digitalWrite(ACTY_LED_PIN, HIGH);
  //actyOffTime = millis() + ACTY_BLINK_TIME;
}

bool Floower::setPixelsPowerOn(bool powerOn) {
  if (powerOn && !pixelsPowerOn) {
    pixelsPowerOn = true;
    ESP_LOGD(LOG_TAG, "LEDs power ON");
    digitalWrite(NEOPIXEL_PWR_PIN, LOW);
    delay(5); // TODO
    return true;
  }
  if (!powerOn && pixelsPowerOn) {
    pixelsPowerOn = false;
    ESP_LOGD(LOG_TAG, "LEDs power OFF");
    digitalWrite(NEOPIXEL_PWR_PIN, HIGH);
    return true;
  }
  return false; // no change
}

bool Floower::setStepperPowerOn(bool powerOn) {
  if (powerOn && !stepperPowerOn) {
    stepperPowerOn = true;
    ESP_LOGD(LOG_TAG, "Stepper power ON");
    digitalWrite(TMC_EN_PIN, HIGH);
    delay(5); // TODO
    return true;
  }
  if (!powerOn && stepperPowerOn) {
    stepperPowerOn = false;
    ESP_LOGD(LOG_TAG, "Stepper power OFF");
    digitalWrite(TMC_EN_PIN, LOW);
    return true;
  }
  return false; // no change
}

PowerState Floower::readPowerState() {
  float reading = analogRead(BATTERY_ANALOG_PIN); // 0-4095
  float voltage = reading * 0.00181; // 1/4069 for scale * analog reference voltage is 3.6V * 2 for using 1:1 voltage divider + adjustment
  uint8_t level = _min(_max(0, voltage - 3.3) * 111, 100); // 3.3 .. 0%, 4.2 .. 100%

  bool charging = digitalRead(CHARGE_PIN) == LOW;
  bool usbPowered = analogRead(USB_ANALOG_PIN) > 2000; // ~2900 is 5V

  ESP_LOGI(LOG_TAG, "Battery %.0f %.2fV %d%% %s", reading, voltage, level, charging ? "CHRG" : (usbPowered ? "USB" : ""));

  statusColor = charging ? colorRed : (usbPowered ? colorGreen : colorBlack);
  statusPixel.SetPixelColor(0, statusColor);

  powerState = {voltage, level, charging, usbPowered};
  return powerState;
}

void Floower::setLowPowerMode(bool lowPowerMode) {
  if (this->lowPowerMode != lowPowerMode) {
    this->lowPowerMode = lowPowerMode;
    showColor(pixelsColor);
  }
}

bool Floower::isLowPowerMode() {
  return lowPowerMode;
}

void Floower::handleTimers(unsigned long now) {
  
}
