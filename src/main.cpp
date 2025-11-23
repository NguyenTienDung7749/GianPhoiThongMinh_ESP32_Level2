#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <ESP32Servo.h>
#include <DHT.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1

// Chân I2C OLED (đúng mạch bạn đang dùng)
#define OLED_SDA 8
#define OLED_SCL 9

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= SERVO MG996R =================
Servo mg996;

const int SERVO_PIN    = 13;    // chân PWM điều khiển servo
const int STOP_PWM     = 1500;  // xung dừng

// ĐÃ ĐẢO CHIỀU THEO YÊU CẦU:
// FORWARD = quay ra (phơi), REVERSE = quay vào (thu)
const int FORWARD_PWM  = 1300;  // quay ra
const int REVERSE_PWM  = 1700;  // quay vào

const int MOVE_TIME    = 3000;  // thời gian quay mỗi lần (ms)

// Cờ báo servo đang quay hay không (để không đổi màn hình khi đang chạy)
bool isMovingServo = false;

// ================= CẢM BIẾN ÁNH SÁNG & MƯA =================
const int LDR_DO   = 3;   // DO cảm biến quang (HIGH = tối, LOW = sáng)
const int RAIN_AO  = 6;   // AO cảm biến mưa (analog)

// Ngưỡng mưa – bạn đã đo AO ~1500 khi có vài giọt
int RAIN_THRESHOLD = 2000; // < ngưỡng -> coi là có mưa

// ================= DHT22 =================
// DATA DHT22 nối với GPIO 2
const int DHT_PIN  = 2;
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

// Đọc DHT 5s/lần (khuyến cáo của DHT22)
unsigned long lastDhtRead = 0;
const unsigned long DHT_INTERVAL = 5000; // ms

float lastTemp = NAN;  // °C
float lastHum  = NAN;  // %

// ================= LED TRẠNG THÁI =================
const int LED_THU  = 4;  // LED đỏ – thu
const int LED_PHOI = 5;  // LED xanh – phơi

// ================= TRẠNG THÁI GIÀN PHƠI =================
// false = thu vào (IN), true = phơi ngoài (OUT)
bool isOut = false;

// Nhớ trạng thái môi trường trước đó
bool lastIsBright  = false;
bool lastIsRaining = false;

// Nhớ trạng thái đã in ra Serial (giảm spam)
bool lastDebugBright = false;
bool lastDebugRain   = false;
bool lastDebugOut    = false;

// Lý do di chuyển – để hiển thị chữ cho đúng
enum MoveReason {
  REASON_BRIGHT,       // do trời sáng
  REASON_DARK,         // do trời tối
  REASON_RAIN,         // do vừa mưa
  REASON_RAIN_CLEARED  // do vừa tạnh mưa
};

// ================= LUÂN PHIÊN MÀN HÌNH OLED =================
// 10s: trạng thái ("Dang Phoi" / "Thu Xong")
// 10s: nhiệt độ
// 10s: độ ẩm
enum DisplayMode {
  DISPLAY_STATUS,
  DISPLAY_TEMP,
  DISPLAY_HUM
};

DisplayMode displayMode = DISPLAY_STATUS;
unsigned long lastDisplayToggle = 0;
const unsigned long DISPLAY_INTERVAL = 10000; // 10 giây

// ============== HÀM OLED CƠ BẢN ==============

// In 1 chuỗi với font FreeSansBold12pt7b, căn giữa màn hình
void showCenteredBig(const char *text) {
  display.clearDisplay();
  display.setRotation(0);
  display.setFont(&FreeSansBold12pt7b);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int16_t x = (SCREEN_WIDTH - w) / 2;
  int16_t y = (SCREEN_HEIGHT + h) / 2;

  display.setCursor(x, y);
  display.println(text);
  display.display();
}

// Hiển thị 2 dòng to: dòng 1 và dòng 2, đều căn giữa
void showTwoLineBig(const char *line1, const char *line2) {
  display.clearDisplay();
  display.setRotation(0);
  display.setFont(&FreeSansBold12pt7b);
  display.setTextColor(SSD1306_WHITE);

  // Dòng 1 (ví dụ "Nhiet Do")
  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
  int16_t xLine1 = (SCREEN_WIDTH - w1) / 2;
  int16_t yLine1 = 22; // hơi cao một chút

  // Dòng 2 (ví dụ "28*C")
  int16_t x2, y2;
  uint16_t w2, h2;
  display.getTextBounds(line2, 0, 0, &x2, &y2, &w2, &h2);
  int16_t xLine2 = (SCREEN_WIDTH - w2) / 2;
  int16_t yLine2 = 50; // thấp hơn

  display.setCursor(xLine1, yLine1);
  display.println(line1);

  display.setCursor(xLine2, yLine2);
  display.println(line2);

  display.display();
}

// Màn hình trạng thái ổn định: "Dang Phoi" hoặc "Thu Xong"
void showStatusStable() {
  if (isOut) {
    showCenteredBig("Dang Phoi");
  } else {
    showCenteredBig("Thu Xong");
  }
}

// Màn hình nhiệt độ – chữ to: "Nhiet Do" + "28*C"
void showTempBig() {
  if (isnan(lastTemp)) {
    showCenteredBig("DHT ERR");
    return;
  }

  char value[16];
  // Ví dụ: 28.3 -> "28*C"
  snprintf(value, sizeof(value), "%.0f*C", lastTemp);

  showTwoLineBig("Nhiet Do", value);
}

// Màn hình độ ẩm – chữ to: "Do Am" + "75%"
void showHumBig() {
  if (isnan(lastHum)) {
    showCenteredBig("DHT ERR");
    return;
  }

  char value[16];
  // Ví dụ: 75.2 -> "75%"
  snprintf(value, sizeof(value), "%.0f%%", lastHum);

  showTwoLineBig("Do Am", value);
}

// ============== HÀM ĐIỀU KHIỂN SERVO ==============

// Quay servo với LED nháy trong thời gian MOVE_TIME
void moveServoWithBlink(int pwm, int ledBlinkPin) {
  unsigned long start = millis();
  bool ledState = false;

  isMovingServo = true; // đang quay -> tạm thời không đổi màn hình luân phiên

  // Tắt LED còn lại để khỏi lẫn
  if (ledBlinkPin == LED_THU) {
    digitalWrite(LED_PHOI, LOW);
  } else {
    digitalWrite(LED_THU, LOW);
  }

  mg996.writeMicroseconds(pwm);

  while (millis() - start < MOVE_TIME) {
    digitalWrite(ledBlinkPin, ledState);
    ledState = !ledState;
    delay(200);
  }

  mg996.writeMicroseconds(STOP_PWM);
  isMovingServo = false;
}

// Di chuyển RA (phơi)
void moveOut(MoveReason reason) {
  // Hiển thị lý do lúc bắt đầu
  if (reason == REASON_RAIN_CLEARED) {
    showCenteredBig("Tanh");
    delay(700);
  } else if (reason == REASON_BRIGHT) {
    showCenteredBig("Sang");
    delay(700);
  }

  // Thông báo đang phơi ra
  showCenteredBig("Phoi Ra");

  // Quay servo + nháy LED xanh
  moveServoWithBlink(FORWARD_PWM, LED_PHOI);

  // Trạng thái ổn định: đang phơi
  digitalWrite(LED_THU, LOW);
  digitalWrite(LED_PHOI, HIGH);
  showStatusStable(); // "Dang Phoi"

  // Sau khi dừng: bắt đầu chu kỳ màn hình từ STATUS
  displayMode = DISPLAY_STATUS;
  lastDisplayToggle = millis();
}

// Di chuyển VÀO (thu)
void moveIn(MoveReason reason) {
  // Hiển thị lý do lúc bắt đầu
  if (reason == REASON_RAIN) {
    showCenteredBig("Mua");
    delay(700);
  } else if (reason == REASON_DARK) {
    showCenteredBig("Toi");
    delay(700);
  }

  // Thông báo đang thu vào
  showCenteredBig("Thu Vao");

  // Quay servo + nháy LED đỏ
  moveServoWithBlink(REVERSE_PWM, LED_THU);

  // Trạng thái ổn định: đã thu xong
  digitalWrite(LED_PHOI, LOW);
  digitalWrite(LED_THU, HIGH);
  showStatusStable(); // "Thu Xong"

  // Sau khi dừng: bắt đầu chu kỳ màn hình từ STATUS
  displayMode = DISPLAY_STATUS;
  lastDisplayToggle = millis();
}

// ============== SETUP & LOOP ==============

void setup() {
  Serial.begin(115200);
  delay(1000);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Servo
  mg996.setPeriodHertz(50);
  mg996.attach(SERVO_PIN, 500, 2500);
  mg996.writeMicroseconds(STOP_PWM);

  // I/O
  pinMode(LDR_DO, INPUT);
  pinMode(RAIN_AO, INPUT);
  pinMode(LED_THU, OUTPUT);
  pinMode(LED_PHOI, OUTPUT);

  // DHT22
  dht.begin();

  // Đọc trạng thái ban đầu ánh sáng + mưa
  bool isDarkStart    = (digitalRead(LDR_DO) == HIGH);
  bool isBrightStart  = !isDarkStart;
  int  rainStart      = analogRead(RAIN_AO);
  bool isRainingStart = (rainStart < RAIN_THRESHOLD);

  lastIsBright  = isBrightStart;
  lastIsRaining = isRainingStart;

  lastDebugBright = isBrightStart;
  lastDebugRain   = isRainingStart;
  lastDebugOut    = isOut;

  // Trạng thái ban đầu LED + OLED
  if (isOut) {
    digitalWrite(LED_PHOI, HIGH);
    digitalWrite(LED_THU, LOW);
  } else {
    digitalWrite(LED_THU, HIGH);
    digitalWrite(LED_PHOI, LOW);
  }
  showStatusStable();

  // Bắt đầu chu kỳ hiển thị từ STATUS
  displayMode = DISPLAY_STATUS;
  lastDisplayToggle = millis();

  Serial.println("He thong gian phoi start.");
}

void loop() {
  unsigned long now = millis();

  // ===== CẢM BIẾN ÁNH SÁNG & MƯA =====
  bool isDark    = (digitalRead(LDR_DO) == HIGH);
  bool isBright  = !isDark;          // DO: HIGH = tối, LOW = sáng
  int  rainVal   = analogRead(RAIN_AO);
  bool isRaining = (rainVal < RAIN_THRESHOLD);

  // LOGIC CHÍNH:
  // Sáng + không mưa -> phơi ngoài (OUT)
  // Còn lại          -> thu vào (IN)
  bool shouldOut = (isBright && !isRaining);

  // In debug khi có thay đổi
  if (isBright != lastDebugBright ||
      isRaining != lastDebugRain ||
      isOut    != lastDebugOut) {

    Serial.print("Bright=");
    Serial.print(isBright ? "YES" : "NO");
    Serial.print(" | Raining=");
    Serial.print(isRaining ? "YES" : "NO");
    Serial.print(" | isOut=");
    Serial.println(isOut ? "OUT" : "IN");

    lastDebugBright = isBright;
    lastDebugRain   = isRaining;
    lastDebugOut    = isOut;
  }

  // ===== QUYẾT ĐỊNH DI CHUYỂN GIÀN PHƠI =====
  if (shouldOut && !isOut) {
    // Đang ở trong -> nên ra ngoài
    MoveReason reason;
    if (lastIsRaining && !isRaining) {
      reason = REASON_RAIN_CLEARED; // vừa tạnh mưa
    } else {
      reason = REASON_BRIGHT;       // trời vừa sáng
    }
    moveOut(reason);
    isOut = true;
  }
  else if (!shouldOut && isOut) {
    // Đang ở ngoài -> nên thu vào
    MoveReason reason;
    if (!lastIsRaining && isRaining) {
      reason = REASON_RAIN;         // vừa bắt đầu mưa
    } else {
      reason = REASON_DARK;         // trời tối
    }
    moveIn(reason);
    isOut = false;
  }

  // Lưu trạng thái để so sánh cho vòng sau
  lastIsBright  = isBright;
  lastIsRaining = isRaining;

  // ===== ĐỌC DHT22 ĐỊNH KỲ (5s/lần) =====
  if (now - lastDhtRead >= DHT_INTERVAL) {
    lastDhtRead = now;

    float h = dht.readHumidity();
    float t = dht.readTemperature(); // °C

    if (!isnan(h) && !isnan(t)) {
      lastHum  = h;
      lastTemp = t;

      Serial.print("DHT22 -> Temp: ");
      Serial.print(t);
      Serial.print(" *C  | Hum: ");
      Serial.print(h);
      Serial.println(" %");
    } else {
      Serial.println("DHT22 read error");
    }
  }

  // ===== LUÂN PHIÊN MÀN HÌNH OLED (CHỈ KHI SERVO ĐANG ĐỨNG YÊN) =====
  if (!isMovingServo) {
    if (now - lastDisplayToggle >= DISPLAY_INTERVAL) {
      lastDisplayToggle = now;

      // Chuyển sang mode tiếp theo
      if (displayMode == DISPLAY_STATUS) {
        displayMode = DISPLAY_TEMP;
      } else if (displayMode == DISPLAY_TEMP) {
        displayMode = DISPLAY_HUM;
      } else {
        displayMode = DISPLAY_STATUS;
      }

      // Hiển thị theo mode
      if (displayMode == DISPLAY_STATUS) {
        showStatusStable();
      } else if (displayMode == DISPLAY_TEMP) {
        showTempBig();
      } else { // DISPLAY_HUM
        showHumBig();
      }
    }
  }

  delay(200); // vòng lặp đủ chậm, không load CPU nhiều
}