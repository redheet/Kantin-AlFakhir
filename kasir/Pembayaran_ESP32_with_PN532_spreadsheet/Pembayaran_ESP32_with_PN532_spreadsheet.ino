//Program by: Alim Mulyadi
//Created: 23/04/2024
//Program: Sistem Pembayaran RFID PN532 with Database Spreadsheet

//---Library yang digunakan---//
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <HTTPClient.h>

//---Pin & Settings Lainnya---//
// If using the breakout with SPI, define the pins for SPI communication.
#define PN532_SCK  (18)
#define PN532_MISO (19)
#define PN532_MOSI (23)
#define PN532_SS   (26)
// If using the breakout or shield with I2C, define just the pins connected
#define PN532_IRQ   (27)
//#define PN532_RESET (26)  // Not connected by default on the NFC Shield
#define i2c_keypad 0x38
#define i2c_lcd 0x27
// WiFi credentials
const char* ssid = "SMP Islam Modern AlFakhir";         // change SSID
const char* password = "SmpiAlFakhir*1";    // change password
// Google script ID and required credentials
String GOOGLE_SCRIPT_ID = "AKfycbyxE62g9zwDurxhy2q0CNhtOOPQ430LwD5qUQE0IcAkf6WBmTxQ1nCXYQdhmwInlrw82A";    // change Gscript ID

//---Mendefinisikan Keypad---///
const byte ROWS = 4;  //four rows
const byte COLS = 4;  //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 7, 6, 5, 4 };  //connect to the row pinouts of the keypad
byte colPins[COLS] = { 3, 2, 1, 0 };  //connect to the column pinouts of the keypad

//---Variable Lainnya yang digunakan---//
byte lcdEntriPos = 9;
const int DELAY_BETWEEN_CARDS = 150; //150
int irqCurr;
int irqPrev;
int count_display = 0;

String kartuHex = "";
String mode_alat = "cek saldo";
String menuEntriNilai;

boolean readerDisabled = false;
bool baca_kartu = true;
bool cekSaldo = true;
bool topup = false;
bool bayar = false;
bool batal = false;
bool auth_transaksi = false;
bool proses_transaksi = false;
bool sukses_transaksi = false;
bool warning_card = false;
bool entriNilai = false;

long timeLastCardRead = 0;
unsigned long ms_scan, ms_display;

struct Saldo {
  uint8_t hex[4];
  unsigned long sekarang;
  unsigned long transaksi;
  unsigned long tampung;
};
Saldo saldo;

byte panah[8] = {
  B10000,
  B11000,
  B11100,
  B11110,
  B11100,
  B11000,
  B10000,
  B00000
};

//---Mendefinisikan Library---//
// Use this line for a breakout with a SPI connection:
Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
// Or use this line for a breakout or shield with an I2C connection:
//Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);
LiquidCrystal_I2C lcd(i2c_lcd, 16, 2);
Keypad_I2C customKeypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS, i2c_keypad);


void setup() {
  Serial.begin(115200);
  EEPROM.begin(64);
  nfc.begin();
  Wire.begin();
  customKeypad.begin();  // GDY120705
  Wire.beginTransmission(0x3F);
  if (Wire.endTransmission()) {
    lcd = LiquidCrystal_I2C(0x27, 16, 2);
  }
  lcd.begin();
  lcd.backlight();
  lcd.createChar(1, panah);

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }

  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  nfc.SAMConfig();
  
  Serial.println();
  Serial.print("Connecting to wifi: ");
  Serial.println(ssid);
  Serial.flush();
  lcd.setCursor(0, 0);
  lcd.print(F("SSID: "));
  lcd.print(ssid);
  lcd.setCursor(0, 1);
  lcd.print("Connecting");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.print(".");
  }
  lcd.clear();
  delay(10);
  
  lcd.setCursor(0, 0);
  lcd.print(F("Kasir"));
  lcd.setCursor(0, 1);
  lcd.print(F("By: VYSE "));
  delay(1500);
  //digitalWrite(buzz, LOW);
  lcd.clear();
  delay(100);
  startListeningToNFC();
  ms_display = millis();
}

void loop() {
  if (baca_kartu == false) {
    if (millis() - ms_scan >= (DELAY_BETWEEN_CARDS + 100)) {
      //if (!cekSaldo) {
      //lcd.clear();
      //delay(50);
      //}
      if (mode_alat == "cek saldo") {
        baca_kartu = true;
      }
      else if (mode_alat == "topup") {
        if (topup) {
          baca_kartu = true;
        }
      }
      else if (mode_alat == "bayar") {
        if (bayar) {
          baca_kartu = true;
        }
      }
    }
  }

  if (readerDisabled) {
    if (millis() - timeLastCardRead > DELAY_BETWEEN_CARDS) {
      readerDisabled = false;
      startListeningToNFC();
      //cekSaldo = true;
    }
  } else {
    irqCurr = digitalRead(PN532_IRQ);

    // When the IRQ is pulled low - the reader has got something for us.
    if (irqCurr == LOW && irqPrev == HIGH) {
      Serial.println("Got NFC IRQ");
      //if(baca_kartu == true){
      handleCardDetected();
      //}
    }
    irqPrev = irqCurr;
  }

  cekMenu();
}

void cekMenu() {
  char keyp = customKeypad.getKey();  //-Pembacaan Keypad

  //---Cek Serial Keypad---//
  if (keyp != NO_KEY) {
    Serial.println(keyp);
  }

  if (mode_alat == "cek saldo") {
    //---Function Tampilan dengan Millis---///
    if (millis() - ms_display >= 2000) {
      if (count_display >= 2) {
        count_display = 0;
      } else {
        count_display++;
      }

      ms_display = millis();
    }

    if (cekSaldo) {
      if (count_display == 0) {
        lcd.setCursor(0, 0);
        lcd.print(F("      Scan      "));
      }
      else if (count_display == 1) {
        lcd.setCursor(0, 0);
        lcd.print(F("     A.Topup    "));
      }
      else if (count_display == 2) {
        lcd.setCursor(0, 0);
        lcd.print(F("     B.Bayar    "));
      }
      //---End Function Tampilan---///

      if (keyp == 'A') {
        lcd.clear();
        delay(50);
        baca_kartu = false;
        mode_alat = "topup";
        menuEntriNilai = "";
        entriNilai = true;
        lcdEntriPos = 9;
      }

      if (keyp == 'B') {
        lcd.clear();
        delay(50);
        baca_kartu = false;
        mode_alat = "bayar";
        menuEntriNilai = "";
        entriNilai = true;
        lcdEntriPos = 9;
      }
    }
  }
  else if (mode_alat == "topup") {
    if (auth_transaksi) {
      if (topup) {
        lcd.setCursor(0, 0);
        lcd.print(F("Topup Rp."));
        lcd.print(saldo.transaksi);
        lcd.setCursor(0, 1);
        lcd.write(1);
        lcd.print(F("(Tap Kartu)"));
      }
    }
    else {
      lcd.setCursor(0, 0);
      lcd.print(F("Topup Rp."));

      if (entriNilai) {
        if ((keyp >= '0') && (keyp <= '9') && keyp) {
          if (lcdEntriPos >= 16) {
            lcd.setCursor(0, 1);
            lcd.print(F("nilai lebih"));
            delay(2000);
            lcd.clear();
            delay(50);
            saldo.transaksi = 0;
            menuEntriNilai = "";
            entriNilai = true;
            lcdEntriPos = 9;
          } else {
            menuEntriNilai += keyp;
            lcd.setCursor(lcdEntriPos++, 0);
            lcd.print(keyp);
          }
          saldo.transaksi = menuEntriNilai.toInt();
        }
      }
    }

    if (keyp == '*') {
      if (saldo.transaksi != 0) {
        topup = true;
        auth_transaksi = true;
        baca_kartu = true;
      }
      else {
        menuEntriNilai = "";
        entriNilai = true;
        lcdEntriPos = 9;
        lcd.setCursor(0, 1);
        lcd.print(F("Input Nilai"));
        delay(1000);
        lcd.clear();
        //delay(50);
      }
    }

    if (keyp == '#') {
      batal = true;
    }
  }
  else if (mode_alat == "bayar") {
    if (auth_transaksi) {
      if (bayar) {
        lcd.setCursor(0, 0);
        lcd.print(F("Bayar Rp."));
        lcd.print(saldo.transaksi);
        lcd.setCursor(0, 1);
        lcd.write(1);
        lcd.print(F("(Tap Kartu)"));
      }
    }
    else {
      lcd.setCursor(0, 0);
      lcd.print(F("Bayar Rp."));

      if (entriNilai) {
        if ((keyp >= '0') && (keyp <= '9') && keyp) {
          if (lcdEntriPos >= 16) {
            lcd.setCursor(0, 1);
            lcd.print(F("nilai lebih"));
            delay(2000);
            lcd.clear();
            delay(50);
            saldo.transaksi = 0;
            menuEntriNilai = "";
            entriNilai = true;
            lcdEntriPos = 9;
          } else {
            menuEntriNilai += keyp;
            lcd.setCursor(lcdEntriPos++, 0);
            lcd.print(keyp);
          }
          saldo.transaksi = menuEntriNilai.toInt();
        }
      }
    }

    if (keyp == '*') {
      if (saldo.transaksi != 0) {
        bayar = true;
        auth_transaksi = true;
        baca_kartu = true;
      }
      else {
        menuEntriNilai = "";
        entriNilai = true;
        lcdEntriPos = 9;
        lcd.setCursor(0, 1);
        lcd.print(F("Input Nilai"));
        delay(1000);
        lcd.clear();
        //delay(50);
      }
    }

    if (keyp == '#') {
      batal = true;
    }
  }

  if (batal) {
    saldo.transaksi = 0;
    auth_transaksi = false;
    batal = false;
    mode_alat = "cek saldo";
    lcd.clear();
    delay(50);
  }


}
