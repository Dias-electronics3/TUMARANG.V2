#include <SPI.h>
#include <SD.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <RTClib.h>
#include <Wire.h>
#include <EEPROM.h>

// === HASIL KALIBRASI TOUCHSCREEN TFT 3,5 Inch  ===
#define YP A1
#define XM A2
#define YM 7
#define XP 6
#define TS_LEFT 915
#define TS_RT 200
#define TS_TOP 924
#define TS_BOT 190
#define MINPRESSURE 10
#define MAXPRESSURE 1000
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
RTC_DS3231 rtc;
MCUFRIEND_kbv tft;

// === WARNA ===
#define BLACK 0x0000
#define WHITE 0xFFFF
#define GRAY 0x8410
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define YELLOW 0xFFE0
#define ORANGE 0xFD20
#define CYAN 0x07FF
#define DARKGREEN 0x03E0
#define BROWN 0xA145


// === BUTTON PANEL ===
int PompaON = 24;
int PompaOFF = 26;

// === INDIKATOR STATUS KELEMBABAN TANAH ===
#define KERING 37
#define LEMBAB 35
#define BASAH 33

// === PIN RELAY===
#define relay1 25
#define relay2 27
#define relay3 29
#define relay4 31

// === SENSOR DS18B20 ===
#define ONE_WIRE_BUS A11
// === SENSOR MOISTURE ===
#define MOISTURE_ANALOG_PIN A10


const int EEPROM_BASE = 0;  // hanya butuh satu kali
int selectedSchedule = 0;   // jadwal yang sedang diedit

// === VARIABEL BACAAN TERAKHIR DARI SENSOR ===
int lastSecond = -1;
int lastMoisture = -1;
int moist;
// === VARIABEL BUTTON TERSAMBUNG KE GND (PULLUP) ===
int GNDPompaON = 0;
int GNDPompaOFF = 0;
// === UI POSITION DI TFT ===
int screenW = 480;
int screenH = 320;
int hamburgerX = 10;
int hamburgerY = 10;
//PEMBATASAN PENGIRIMAN SERIAL
unsigned long lastSend = 0;
const unsigned long intervalSend = 1000;
//SETTING WAKTU
int tempHour = 0;
int tempMinute = 0;
int tempSecond = 0;
//FLOAT SENSOR
float temp;
float lastTemperature = -1000.0;
//SETTING TIMER
struct JadwalPompa {
  int jamON, menitON, detikON;
  int jamOFF, menitOFF, detikOFF;
};

//RTC
bool jadwalAktif[3] = { false, false, false };
char lastTimeStr[9] = "";
// === STATUS POMPA ===
bool pompaStatus = true;
bool pompaTFT = true;
bool pompaESP = true;
bool aktif;
bool editingON = true;  // sedang edit waktu ON atau OFF
bool button = true;
JadwalPompa jadwal[3];  // 3 jadwal

// === MENU DAN MODE PADA LAYAR TFT ===
enum ScreenState { MENU_AWAL,
                   PILIHAN_MENU,
                   KONTROL_POMPA,
                   SET_WAKTU,
                   SET_RTC_TIME };
ScreenState currentScreen = MENU_AWAL;
enum ModePompa { MODE_SENSOR,
                 MODE_JADWAL,
                 MODE_MANUAL };

ModePompa modePompa = MODE_SENSOR;

// === DEKLARASI FUNCTION YANG DIGUNAKAN DALAM PROGRAM INI ===
void updateTemperature();
void updateMoisture();
void updateTime();
void tampilkanModePompa();
void updateTimeInSetWaktu();
void drawWaterDrop(int x, int y, int size, uint16_t color);
void handleRTCButtons();
void drawScheduleOptions();
void drawEditTimeUI();
void simpanJadwalKeEEPROM();
void bacaJadwalDariEEPROM();
void showSetRTCTime();
void resetAndRefreshMainScreen();
DateTime readTimeFromEEPROM();
void saveTimeToEEPROM(DateTime now);
void handleTouch(int x, int y);

// === PEMBACAAN SENSOR DS18B20 ===
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


void setup() {

  //SETTING AWAL SYSTEM
  Serial.begin(9600);
  pinMode(PompaON, INPUT_PULLUP);
  pinMode(PompaOFF, INPUT_PULLUP);
  pinMode(KERING, OUTPUT);
  pinMode(LEMBAB, OUTPUT);
  pinMode(BASAH, OUTPUT);
  pinMode(relay1, OUTPUT);
  digitalWrite(relay1, 1);  // Module Relay (AKTIF LOW)
  sensors.begin();

  // SETUP WAKTU RTC DAN BACKUP KETIKA RTC LOST POWER TIDAK KEHILANGAN WAKTU REALTIMENYA
  rtc.begin();
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  if (!rtc.begin() || rtc.lostPower()) {
    DateTime backup = readTimeFromEEPROM();
    rtc.adjust(backup);  // Set RTC ke waktu terakhir yang disimpan
  }


  //SETUP LAYAR TFT
  uint16_t ID = tft.readID();
  tft.begin(ID);
  tft.setRotation(1);     //LANSCAPE
  tft.fillScreen(BLACK);  // BAGROUND
  showMenuAwal();         // MENAMPILKAN FUNCTION MENU AWAL
  bacaJadwalDariEEPROM();
}

// === LOOP ===
void loop() {
  
  cekDanKendalikanPompa();
  Serial.println("pompaStatrus: " + String(pompaStatus));
  Serial.println("btn: " + String(button));

  if (digitalRead(PompaOFF) == LOW) button = true;
  if (digitalRead(PompaON) == LOW) button = false;

  if (button == false) {
    digitalWrite(relay1, button);
  }

  else digitalWrite(relay1, pompaStatus);

  if (modePompa == MODE_JADWAL) {
    checkPompaBySchedule();
  }
  // Pembacaan sensor selalu aktif di semua layar
  sensors.requestTemperatures();
  temp = sensors.getTempCByIndex(0);
  int val = analogRead(MOISTURE_ANALOG_PIN);
  moist = map(val, 1023, 0, 0, 100);

  // Kirim data ke Serial Monitor setiap 1 detik
  DateTime now = rtc.now();
  TSPoint p = ts.getPoint();
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
    int x = map(p.y, TS_TOP, TS_BOT, 0, tft.width());
    int y = map(p.x, TS_RT, TS_LEFT, 0, tft.height());
    handleTouch(x, y);
  }

  if (currentScreen == MENU_AWAL) {
    updateTime();
    updateTemperature();
    updateMoisture();
    tampilkanModePompa();
    checkPompaBySchedule();
    // serial();
    tft.setTextSize(1);
    tft.setTextColor(ORANGE);
    tft.setCursor(10, 300);
    tft.print("BY: ELEKTRONIKA SMKN 1 KATAPANG");
  }

  if (currentScreen == SET_WAKTU) {
    updateTimeInSetWaktu();
  }

  // === CEK JADWAL POMPA ===

  for (int i = 0; i < 3; i++) {
    JadwalPompa &j = jadwal[i];
    int nowSec = now.hour() * 3600 + now.minute() * 60 + now.second();
    int onSec = j.jamON * 3600 + j.menitON * 60 + j.detikON;
    int offSec = j.jamOFF * 3600 + j.menitOFF * 60 + j.detikOFF;

    if (onSec <= offSec) {
      jadwalAktif[i] = (nowSec >= onSec && nowSec < offSec);
    } else {
      jadwalAktif[i] = (nowSec >= onSec || nowSec < offSec);  // Lintasi tengah malam
    }
  }

  if (modePompa == MODE_JADWAL) {
    aktif = jadwalAktif[0] || jadwalAktif[1] || jadwalAktif[2];
    // digitalWrite(relay1, aktif ? LOW : HIGH);
  }

  if (currentScreen == PILIHAN_MENU) {
    updateTime();
    updateMoisture();
  }

  if (modePompa == MODE_JADWAL) {
    checkPompaBySchedule();
  } else if (modePompa == MODE_SENSOR) {
    cekDanKendalikanPompa();
  }
}

/////////////////////////////////////////////// FUNCTION MENAMPILKAN, MENGGAMBAR ICON PADA LAYAR TFT BAIK DI SETIAP MENU DAN MENU AWAL /////////////////////////////////////////////

// MENAMPILKAN MENU UTAMA DI LAYAR TFT
void showMenuAwal() {
  modePompa = MODE_SENSOR;
  tft.fillScreen(BLACK);

  hamburgerX = 10;
  hamburgerY = 10;
  drawHamburgerIcon(hamburgerX, hamburgerY);

  //JUDUL DI MENU UTAMA
  tft.setTextColor(ORANGE);
  tft.setTextSize(5);
  tft.setCursor(135, 15);
  tft.println("TUMARANG");

  // GAMBAR TERMOMETER DI MENU UTAMA
  tft.fillRoundRect(150, 190, 15, 50, 30, RED);
  tft.drawRoundRect(150, 140, 15, 100, 30, WHITE);
  for (int i = 0; i < 8; i++) {
    tft.drawFastHLine(150, 155 + i * 10, 8, WHITE);
  }

  currentScreen = MENU_AWAL;
}

// === GAMBAR BUTTON KEMBALI DI SETIAP MENUNYA ===
void drawBackButton() {
  tft.fillRect(10, screenH - 50, 100, 40, RED);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, screenH - 40);
  tft.print("Kembali");
}

// === TAMPILAN KETIKA ICON HUMBERGER DITEKAN ===
void showPilihanMenu() {
  drawHamburgerIcon(hamburgerX, hamburgerY);

  int menuX = hamburgerX;
  int menuY1 = hamburgerY + 60;   // Kontrol Pompa
  int menuY2 = menuY1 + 40 + 10;  // Jadwal

  tft.setTextColor(WHITE);
  tft.setTextSize(2);

  // Tombol Jadwal
  tft.fillRoundRect(menuX, menuY1, 130, 40, 6, BLUE);
  tft.setCursor(menuX + 28, menuY1 + 13);
  tft.print("Jadwal");

  // Tombol Set Time
  tft.fillRoundRect(menuX, menuY2, 130, 40, 6, BLUE);
  tft.setCursor(menuX + 20, menuY2 + 14);
  tft.print("Set Time");

  currentScreen = PILIHAN_MENU;
}



// === GAMBAR ICON KELEMBABAN DI MENU UTAMA ===
void drawWaterDrop(int x, int y, int size, uint16_t color) {
  tft.fillCircle(x, y, size, color);
  int x1 = x, y1 = y - size * 2;
  int x2 = x - size, y2 = y;
  int x3 = x + size, y3 = y;
  tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
}

// ===  MEREFRESH LAYAR KETIKA KELUAR DARI MENU DAN KEMBALI KE MENU UTAMA ===
void resetAndRefreshMainScreen() {
  lastTemperature = -1000.0;  // paksa tampil ulang suhu
  lastMoisture = -1;
  strcpy(lastTimeStr, "");

  showMenuAwal();  // gambar ulang UI

  delay(50);
  sensors.requestTemperatures();  // baca suhu baru
  updateTemperature();            // tampilkan suhu
  updateMoisture();               // tampilkan kelembaban
  updateTime();                   // tampilkan jam
}

// === JUDUL DI MENU SET JADWAL POMPA ===
void showSetWaktu() {
  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(2);
  tft.setCursor(160, 5);
  tft.println("SET WAKTU POMPA");

  drawBackButton();
  drawScheduleOptions();
  drawEditTimeUI();
  currentScreen = SET_WAKTU;
}

// === GAMBAR ICON HUMBERGER DI MENU UTAMA ===
void drawHamburgerIcon(int x, int y) {
  tft.fillRoundRect(x, y, 50, 50, 8, GRAY);
  int lineX = x + 8;
  int lineY = y + 8;
  for (int i = 0; i < 3; i++) {
    tft.fillRoundRect(lineX, lineY, 34, 5, 2, WHITE);
    lineY += 17;
  }
}

// === GAMBAR ICON JADWAL 1-3 PADA MENU SET JADWAL POMPA ===
void drawScheduleOptions() {
  tft.setTextSize(2);
  for (int i = 0; i < 3; i++) {
    int x = 20 + i * 150;
    int y = 40;
    tft.fillRoundRect(x, y, 120, 30, 8, (i == selectedSchedule) ? CYAN : GRAY);
    tft.setCursor(x + 20, y + 8);
    tft.setTextColor(BLACK);
    tft.print("Jadwal ");
    tft.print(i + 1);
  }
}

// === UI PADA MENU SET JADWAL POMPA ===
void drawEditTimeUI() {
  JadwalPompa j = jadwal[selectedSchedule];
  int yBase = 90;
  tft.setTextColor(WHITE);
  tft.setTextSize(2);

  // Mode ON/OFF toggle
  tft.fillRoundRect(20, yBase, 80, 30, 5, editingON ? GREEN : RED);
  tft.setCursor(30, yBase + 8);
  tft.print(editingON ? "ON " : "OFF");

  // Display waktu
  int jam = editingON ? j.jamON : j.jamOFF;
  int menit = editingON ? j.menitON : j.menitOFF;
  int detik = editingON ? j.detikON : j.detikOFF;

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", jam, menit, detik);
  tft.fillRect(120, yBase + 8, 100, 20, BLACK);
  tft.setCursor(120, yBase + 8);
  tft.print(buf);

  // Tombol + / -
  tft.fillRoundRect(20, yBase + 50, 50, 30, 5, BLUE);   // Jam +
  tft.fillRoundRect(80, yBase + 50, 50, 30, 5, BLUE);   // Menit +
  tft.fillRoundRect(140, yBase + 50, 50, 30, 5, BLUE);  // Detik +
  tft.fillRoundRect(20, yBase + 90, 50, 30, 5, RED);    // Jam -
  tft.fillRoundRect(80, yBase + 90, 50, 30, 5, RED);    // Menit -
  tft.fillRoundRect(140, yBase + 90, 50, 30, 5, RED);   // Detik -

  tft.setCursor(30, yBase + 58);
  tft.setTextColor(WHITE);
  tft.print("+");
  tft.setCursor(90, yBase + 58);
  tft.print("+");
  tft.setCursor(150, yBase + 58);
  tft.print("+");

  tft.setCursor(30, yBase + 98);
  tft.print("-");
  tft.setCursor(90, yBase + 98);
  tft.print("-");
  tft.setCursor(150, yBase + 98);
  tft.print("-");

  // Tombol KONFIRM
  tft.fillRoundRect(220, yBase + 60, 100, 40, 5, GREEN);
  tft.setCursor(230, yBase + 72);
  tft.setTextColor(BLACK);
  tft.print("KONFIRM");

  // Toggle antara waktu ON dan OFF
  tft.fillRoundRect(340, yBase + 60, 100, 40, 5, WHITE);
  tft.setCursor(350, yBase + 72);
  tft.setTextColor(BLACK);
  tft.print("STATUS");
}


// === MENANDAKAN MODE POMPA DI SETIAP SCREENNYA ===
void tampilkanModePompa() {
  tft.setTextSize(2);
  tft.setCursor(10, screenH - 25);  // pojok kiri bawah
  tft.setTextColor(WHITE, BLACK);   // BG BLACK supaya teks lama terhapus

  if (modePompa == MODE_SENSOR) {
    tft.print("");
  } else if (modePompa == MODE_JADWAL) {
    tft.print("");
  } else if (modePompa == MODE_MANUAL) {
    tft.print("");
  }
}


////////////////////////////////////////////////////////////// FUNCTION SISTEM TFT BEKERJA (TOUCHSCREEN, LOGIKA) //////////////////////////////////////////////////////////////////

// SEMUA SENTUHAN KOORDINAT DAN AKSI YANG DI LAKUKAN DI TFT
void handleTouch(int x, int y) {
  if (currentScreen == MENU_AWAL) {
    if (x >= hamburgerX && x <= hamburgerX + 10 && y >= hamburgerY && y <= hamburgerY + 55) {
      showPilihanMenu();
      checkPompaBySchedule();
      return;
    }
  } else if (currentScreen == PILIHAN_MENU) {
    int menuX = hamburgerX;
    int btnWidth = 130;
    int btnHeight = 40;
    int spacing = 10;

    int menuY1 = hamburgerY + 60;
    int menuY2 = menuY1 + btnHeight + spacing;

    if (x >= menuX && x <= menuX + btnWidth) {
      // Jadwal
      if (y >= menuY1 && y <= menuY1 + btnHeight) {
        showSetWaktu();
        return;
      }
      // Set RTC Time
      if (y >= menuY2 && y <= menuY2 + btnHeight) {
        showSetRTCTime();
        return;
      }
    }
    checkPompaBySchedule();
  }

  else if (currentScreen == SET_WAKTU) {
    if (x >= 10 && x <= 100 && y >= screenH - 50 && y <= screenH - 10) {
      resetAndRefreshMainScreen();
      return;
    }

    // Pilih Jadwal
    for (int i = 0; i < 3; i++) {
      int xBtn = 20 + i * 150;
      if (x >= xBtn - 10 && x <= xBtn + 130 && y >= 30 && y <= 90) {
        selectedSchedule = i;
        drawScheduleOptions();
        drawEditTimeUI();
        return;
      }
    }

    // Toggle edit ON/OFF
    if (x >= 20 && x <= 100 && y >= 90 && y <= 120) {
      editingON = !editingON;
      drawEditTimeUI();
      return;
    }

    // Ambil referensi ke jadwal saat ini
    JadwalPompa &j = jadwal[selectedSchedule];
    int *jam = editingON ? &j.jamON : &j.jamOFF;
    int *menit = editingON ? &j.menitON : &j.menitOFF;
    int *detik = editingON ? &j.detikON : &j.detikOFF;

    // Tombol +
    if (x >= 20 && x <= 70 && y >= 140 && y <= 170) (*jam) = (*jam + 1) % 24;
    else if (x >= 80 && x <= 130 && y >= 140 && y <= 170) (*menit) = (*menit + 1) % 60;
    else if (x >= 140 && x <= 190 && y >= 140 && y <= 170) (*detik) = (*detik + 1) % 60;

    // Tombol -
    else if (x >= 20 && x <= 70 && y >= 180 && y <= 210) (*jam) = (*jam - 1 + 24) % 24;
    else if (x >= 80 && x <= 130 && y >= 180 && y <= 210) (*menit) = (*menit - 1 + 60) % 60;
    else if (x >= 140 && x <= 190 && y >= 180 && y <= 210) (*detik) = (*detik - 1 + 60) % 60;

    // Tombol toggle ON/OFF
    else if (x >= 340 && x <= 440 && y >= 150 && y <= 190) {
      editingON = !editingON;
    }

    // Tombol KONFIRM (simpan ke EEPROM)
    else if (x >= 220 && x <= 320 && y >= 150 && y <= 190) {
      simpanJadwalKeEEPROM();
      modePompa = MODE_JADWAL;
    }
    drawEditTimeUI();
    updateTimeInSetWaktu();
  }

  else if (currentScreen == SET_RTC_TIME) {
    // Tombol Kembali
    if (x >= 10 && x <= 50 && y >= screenH - 50 && y <= screenH - 10) {
      resetAndRefreshMainScreen();
      return;
    }
    // Tombol SIMPAN
    if (x >= 350 && x <= 470 && y >= 260 && y <= 300) {
      DateTime now = rtc.now();
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHour, tempMinute, tempSecond));
      Serial.println("RTC updated.");
      resetAndRefreshMainScreen();
      return;
    }
    handleTouchSetRTCTime(x, y);

    // Jam +/-
    if (y >= 60 && y <= 90) {
      if (x >= 200 && x <= 230) {
        tempHour = (tempHour + 1) % 24;
        drawTempTimeOnly();

      } else if (x >= 240 && x <= 270) {
        tempHour = (tempHour - 1 + 24) % 24;
        drawTempTimeOnly();
      }
    }

    // Menit +/-
    else if (y >= 110 && y <= 140) {
      if (x >= 200 && x <= 230) {
        tempMinute = (tempMinute + 1) % 60;
        drawTempTimeOnly();
      } else if (x >= 240 && x <= 270) {
        tempMinute = (tempMinute - 1 + 60) % 60;
        drawTempTimeOnly();
      }
    }

    // Detik +/-
    else if (y >= 160 && y <= 190) {
      if (x >= 200 && x <= 230) {
        tempSecond = (tempSecond + 1) % 60;
        drawTempTimeOnly();
      } else if (x >= 240 && x <= 270) {
        tempSecond = (tempSecond - 1 + 60) % 60;
        drawTempTimeOnly();
      }
    }
  }
  if (currentScreen == SET_WAKTU) {
    updateTimeInSetWaktu();
  }
}

// === UPDATE WAKTU ===
void updateTime() {
  DateTime now = rtc.now();
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  if (strcmp(timeStr, lastTimeStr) != 0) {
    strcpy(lastTimeStr, timeStr);
    tft.setTextSize(3);
    tft.setTextColor(WHITE, BLACK);
    tft.setCursor(175, 60);
    tft.print(timeStr);

    // Simpan ke EEPROM setiap perubahan detik
    saveTimeToEEPROM(now);
  }
}


// === UPDATE WAKTU DI MENU SET JADWAL POMPA ===
void updateTimeInSetWaktu() {
  DateTime now = rtc.now();
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  tft.setTextSize(4);
  tft.setTextColor(WHITE, BLACK);
  tft.setCursor(screenW - 260, screenH - 70);
  tft.print(timeStr);
  tft.setTextSize(2);
  tft.setTextColor(WHITE, BLACK);
  tft.setCursor(screenW - 225, screenH - 30);
  tft.print("(Saat ini)");
}

// === UPDATE SUHU DI MENU UTAMA ===
void updateTemperature() {
  sensors.requestTemperatures();
  temp = sensors.getTempCByIndex(0);
  if (abs(temp - lastTemperature) > 0.1) {
    lastTemperature = temp;
    tft.setCursor(110, 270);
    tft.setTextColor(WHITE, BLACK);
    tft.setTextSize(2);
    tft.print(temp);
    tft.print(" C");
  }
}

// === UPDATE KELEMBABAN DAN ANIMASI ===
void updateMoisture() {
  int val = analogRead(MOISTURE_ANALOG_PIN);
  moist = map(val, 1023, 0, 0, 100);

  // Cek perubahan cukup signifikan
  if (abs(moist - lastMoisture) >= 2) {
    int cx = 330;
    int cy = 190;
    int radius = 50;

    int tinggiAirLama = map(lastMoisture, 0, 100, 0, radius * 2);
    int tinggiAirBaru = map(moist, 0, 100, 0, radius * 2);

    // Hapus hanya bagian lama yang melebihi area baru
    if (tinggiAirBaru < tinggiAirLama) {
      for (int y = tinggiAirBaru; y < tinggiAirLama; y++) {
        for (int x = -radius; x <= radius; x++) {
          int realY = cy + radius - y;
          if ((x * x + (realY - cy) * (realY - cy)) <= radius * radius) {
            tft.drawPixel(cx + x, realY, BLACK);
          }
        }
      }
    }

    // Tambahkan bagian air yang baru
    for (int y = 0; y < tinggiAirBaru; y++) {
      for (int x = -radius; x <= radius; x++) {
        int realY = cy + radius - y;
        if ((x * x + (realY - cy) * (realY - cy)) <= radius * radius) {
          tft.drawPixel(cx + x, realY, BLUE);
        }
      }
    }

    tft.drawCircle(cx, cy, radius, WHITE);
    drawWaterDrop(cx, cy + 10, 20, CYAN);

    // Update teks kelembaban
    tft.setTextSize(3);
    tft.setTextColor(WHITE, BLACK);
    tft.setCursor(310, 250);
    tft.print(moist);
    tft.print("%  ");

    // Tampilkan status tanah
    String status = "";
    uint16_t color = WHITE;

    if (modePompa == MODE_SENSOR) {
      if (moist < 30) {
        status = "KERING ";
        color = RED;
        digitalWrite(LEMBAB, 0);
        digitalWrite(KERING, 1);
        digitalWrite(BASAH, 0);

      } else if (moist <= 70) {
        status = "LEMBAB ";
        color = YELLOW;
        digitalWrite(LEMBAB, 1);
        digitalWrite(KERING, 0);
        digitalWrite(BASAH, 0);

      } else if (moist <= 100) {
        status = " BASAH ";
        color = GREEN;
        digitalWrite(LEMBAB, 0);
        digitalWrite(KERING, 0);
        digitalWrite(BASAH, 1);
      }
    }


    tft.setTextSize(2);
    tft.setTextColor(color, BLACK);
    tft.setCursor(298, 285);
    tft.print(status);

    // Simpan pembacaan terakhir
    lastMoisture = moist;
  }
}

// === AKSI POMPA KETIKA JADWAL SUDAH SESUAI DENGAN WAKTU YANG SUDAH DI SETTING ===
void checkPompaBySchedule() {
  DateTime now = rtc.now();
  int nowSec = now.hour() * 3600 + now.minute() * 60 + now.second();

  for (int i = 0; i < 3; i++) {
    auto &j = jadwal[i];
    int onSec = j.jamON * 3600 + j.menitON * 60 + j.detikON;
    int offSec = j.jamOFF * 3600 + j.menitOFF * 60 + j.detikOFF;
    aktif = (onSec <= offSec) ? (nowSec >= onSec && nowSec < offSec)
                              : (nowSec >= onSec || nowSec < offSec);
    if (aktif) {
      pompaStatus = false;
      return;
    } else {
      pompaStatus = true;
    }
  }
}

void cekDanKendalikanPompa() {
  DateTime now = rtc.now();

  for (int i = 0; i < 3; i++) {
    // Konversi waktu ON/OFF ke detik dalam sehari
    int waktuON = jadwal[i].jamON * 3600 + jadwal[i].menitON * 60 + jadwal[i].detikON;
    int waktuOFF = jadwal[i].jamOFF * 3600 + jadwal[i].menitOFF * 60 + jadwal[i].detikOFF;
    int waktuSekarang = now.hour() * 3600 + now.minute() * 60 + now.second();

    // Cek apakah waktu sekarang berada dalam rentang ON
    if (modePompa != MODE_SENSOR) {
      //digitalWrite(pompaStatus, pompaStatus ? LOW : HIGH);  // Aktifkan pompa i
    }
  }
}

// === PEMISAH MODE AGAR KONDISI POMPA TIDAK BERTABRAKAN (EROR) ===
void updatePompaStatus() {
  // Manual Mode: dikontrol oleh tombol atau ESP
  if (modePompa == MODE_MANUAL) {
    pompaStatus = (!pompaTFT);
  }

  // Jadwal Mode: dikontrol oleh waktu
  else if (modePompa == MODE_JADWAL) {
    checkPompaBySchedule();  // Akan mengatur pompaStatus berdasarkan waktu
  }
}

/////////////////////////////////////////////////////////////////////////////// EEPROM ////////////////////////////////////////////////////////////////////////////////////////////
void simpanJadwalKeEEPROM() {
  int addr = EEPROM_BASE;
  for (int i = 0; i < 3; i++) {
    EEPROM.put(addr, jadwal[i]);
    addr += sizeof(JadwalPompa);
  }
}

//EEPROM
void bacaJadwalDariEEPROM() {
  int addr = EEPROM_BASE;
  for (int i = 0; i < 3; i++) {
    EEPROM.get(addr, jadwal[i]);
    addr += sizeof(JadwalPompa);


    if (jadwal[i].jamON < 0 || jadwal[i].jamON > 23 || jadwal[i].menitON < 0 || jadwal[i].menitON > 59 || jadwal[i].detikON < 0 || jadwal[i].detikON > 59) {
      jadwal[i].jamON = 0;
      jadwal[i].menitON = 0;
      jadwal[i].detikON = 0;
    }
    if (jadwal[i].jamOFF < 0 || jadwal[i].jamOFF > 23 || jadwal[i].menitOFF < 0 || jadwal[i].menitOFF > 59 || jadwal[i].detikOFF < 0 || jadwal[i].detikOFF > 59) {
      jadwal[i].jamOFF = 0;
      jadwal[i].menitOFF = 0;
      jadwal[i].detikOFF = 0;
    }
  }
}

void saveTimeToEEPROM(DateTime now) {
  EEPROM.update(0, now.second());
  EEPROM.update(1, now.minute());
  EEPROM.update(2, now.hour());
  EEPROM.update(3, now.day());
  EEPROM.update(4, now.month());
  EEPROM.update(5, now.year() - 2000);
}

DateTime readTimeFromEEPROM() {
  int second = EEPROM.read(0);
  int minute = EEPROM.read(1);
  int hour = EEPROM.read(2);
  int day = EEPROM.read(3);
  int month = EEPROM.read(4);
  int year = EEPROM.read(5) + 2000;
  return DateTime(year, month, day, hour, minute, second);
}

//////////////////////////////////////////////////////////////////////////////// MENU SETTING RTC /////////////////////////////////////////////////////////////////////////////////

// === UI SET TIME RTC ===
void showSetRTCTime() {

  tft.fillScreen(BLACK);
  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  tft.setCursor(105, 20);
  tft.print("<SET WAKTU RTC>");
  DateTime now = rtc.now();
  tempHour = now.hour();
  tempMinute = now.minute();
  tempSecond = now.second();

  // Tampilkan waktu (HH:MM:SS) di tengah atas
  tft.setTextSize(4);
  int centerX = tft.width() / 2;
  String timeStr =
    (tempHour < 10 ? "0" : "") + String(tempHour) + ":" + (tempMinute < 10 ? "0" : "") + String(tempMinute) + ":" + (tempSecond < 10 ? "0" : "") + String(tempSecond);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(centerX - w / 2, 70);
  tft.print(timeStr);

  // Tombol + dan - di bawah waktu, berjajar horizontal
  tft.setTextSize(3);
  tft.setTextColor(BLACK);

  int btnWidth = 40;
  int btnHeight = 40;
  int spacing = 20;
  int startX = centerX - (btnWidth * 3 + spacing * 2) / 2;  // center 3 tombol
  int btnYplus = 120;
  int btnYminus = btnYplus + btnHeight + 10;

  // Jam +
  tft.fillRoundRect(startX, btnYplus, btnWidth, btnHeight, 8, GREEN);
  tft.setCursor(startX + 13, btnYplus + 10);
  tft.print("+");

  // Menit +
  tft.fillRoundRect(startX + (btnWidth + spacing), btnYplus, btnWidth, btnHeight, 8, GREEN);
  tft.setCursor(startX + (btnWidth + spacing) + 13, btnYplus + 10);
  tft.print("+");

  // Detik +
  tft.fillRoundRect(startX + 2 * (btnWidth + spacing), btnYplus, btnWidth, btnHeight, 8, GREEN);
  tft.setCursor(startX + 2 * (btnWidth + spacing) + 13, btnYplus + 10);
  tft.print("+");

  // Jam -
  tft.fillRoundRect(startX, btnYminus, btnWidth, btnHeight, 8, RED);
  tft.setCursor(startX + 13, btnYminus + 10);
  tft.print("-");

  // Menit -
  tft.fillRoundRect(startX + (btnWidth + spacing), btnYminus, btnWidth, btnHeight, 8, RED);
  tft.setCursor(startX + (btnWidth + spacing) + 13, btnYminus + 10);
  tft.print("-");

  // Detik -
  tft.fillRoundRect(startX + 2 * (btnWidth + spacing), btnYminus, btnWidth, btnHeight, 8, RED);
  tft.setCursor(startX + 2 * (btnWidth + spacing) + 13, btnYminus + 10);
  tft.print("-");

  // Tombol KEMBALI di kiri bawah
  tft.setTextSize(2);
  tft.setTextColor(WHITE);
  tft.fillRoundRect(10, tft.height() - 60, 120, 40, 10, RED);
  tft.setCursor(30, tft.height() - 50);
  tft.print("KEMBALI");

  // Tombol SIMPAN di kanan bawah
  tft.fillRoundRect(tft.width() - 130, tft.height() - 60, 120, 40, 10, BLUE);
  tft.setCursor(tft.width() - 110, tft.height() - 50);
  tft.print("SIMPAN");

  currentScreen = SET_RTC_TIME;
}

// === GAMBAR ICON KELEMBABAN DI MENU UTAMA ===
void updateTempTimeFromRTC() {
  DateTime now = rtc.now();
  tempHour = now.hour();
  tempMinute = now.minute();
  tempSecond = now.second();
}

// === SETTING TIME RTC ===
void handleTouchSetRTCTime(int x, int y) {
  bool updated = false;
  int btnWidth = 40;
  int btnHeight = 40;
  int spacing = 20;
  int centerX = tft.width() / 2;
  int startX = centerX - (btnWidth * 3 + spacing * 2) / 2;
  int btnYplus = 120;
  int btnYminus = btnYplus + btnHeight + 10;

  // "+" JAM
  if (x >= startX && x <= startX + btnWidth && y >= btnYplus && y <= btnYplus + btnHeight) {
    tempHour = (tempHour + 1) % 24;
    updated = true;
  }
  // "+" MENIT
  else if (x >= startX + (btnWidth + spacing) && x <= startX + (btnWidth + spacing) + btnWidth && y >= btnYplus && y <= btnYplus + btnHeight) {
    tempMinute = (tempMinute + 1) % 60;
    updated = true;
  }
  // "+" DETIK
  else if (x >= startX + 2 * (btnWidth + spacing) && x <= startX + 2 * (btnWidth + spacing) + btnWidth && y >= btnYplus && y <= btnYplus + btnHeight) {
    tempSecond = (tempSecond + 1) % 60;
    updated = true;
  }
  // "-" JAM
  else if (x >= startX && x <= startX + btnWidth && y >= btnYminus && y <= btnYminus + btnHeight) {
    tempHour = (tempHour - 1 + 24) % 24;
    updated = true;
  }
  // "-" MENIT
  else if (x >= startX + (btnWidth + spacing) && x <= startX + (btnWidth + spacing) + btnWidth && y >= btnYminus && y <= btnYminus + btnHeight) {
    tempMinute = (tempMinute - 1 + 60) % 60;
    updated = true;
  }
  // "-" DETIK
  else if (x >= startX + 2 * (btnWidth + spacing) && x <= startX + 2 * (btnWidth + spacing) + btnWidth && y >= btnYminus && y <= btnYminus + btnHeight) {
    tempSecond = (tempSecond - 1 + 60) % 60;
    updated = true;
  }

  // Tombol KEMBALI (kiri bawah)
  else if (x >= 10 && x <= 50 && y >= tft.height() - 60 && y <= tft.height() - 20) {
    resetAndRefreshMainScreen();
    return;
  }

  // Tombol SIMPAN (kanan bawah)
  // Tombol SIMPAN
  if (x >= 350 && x <= 470 && y >= 230 && y <= 270) {
    DateTime now = rtc.now();
    rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHour, tempMinute, tempSecond));
    Serial.println("RTC updated.");
    resetAndRefreshMainScreen();
    return;
  }

  if (updated) {
    drawTempTimeOnly();  // update waktu tanpa refresh full screen
  }
}


// === MENAMPILKAN JAM DI TENGAH MENU SETTING TIME RTC ===
void drawTempTimeOnly() {
  String timeStr =
    (tempHour < 10 ? "0" : "") + String(tempHour) + ":" + (tempMinute < 10 ? "0" : "") + String(tempMinute) + ":" + (tempSecond < 10 ? "0" : "") + String(tempSecond);

  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(4);
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  int centerX = tft.width() / 2;
  tft.setTextColor(WHITE, BLACK);  // Hapus teks lama
  tft.setCursor(centerX - w / 2, 70);
  tft.print(timeStr);
}
