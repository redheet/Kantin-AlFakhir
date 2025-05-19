void startListeningToNFC() {
  // Reset our IRQ indicators
  irqPrev = irqCurr = HIGH;

  //Serial.println("Starting passive read for an ISO14443A Card ...");
  if (!nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A)) {
    //Serial.println("No card found. Waiting...");
    //baca_kartu = true;
  } else {
    //Serial.println("Card already present.");
    //if(baca_kartu == true){
    handleCardDetected();
    //}
  }
}

void handleCardDetected() {
  uint8_t success = false;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

  // read the NFC tag's info
  success = nfc.readDetectedPassiveTargetID(uid, &uidLength);
  Serial.println(success ? "Read successful" : "Read failed (not a card?)");

  if (baca_kartu == true) {
    //Serial.print("Seems to be a Mifare Classic card #");
    if (cekSaldo or topup or bayar) {
      cekSaldo = false;
      for (uint8_t i = 0; i < uidLength; i++) {
        //readCard[i] = mfrc522.uid.uidByte[i];
        //lcd.print(readCard[i], HEX);
        if (uid[i] < 0x10) kartuHex += "0";
        kartuHex += String(uid[i], HEX);
      }
      if (success) {
        lcd.clear();
        delay(10);
        lcd.setCursor(0, 0);
        lcd.print(F("Kartu :"));
        lcd.print(kartuHex);
        // Display some basic information about the card
        Serial.println("Found an ISO14443A card");
        Serial.print("  UID Length: "); Serial.print(uidLength, DEC); Serial.println(" bytes");
        Serial.print("  UID Value: ");
        nfc.PrintHex(uid, uidLength);

        if (uidLength == 4) {
          // We probably have a Mifare Classic card ...
          uint8_t key_data[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          uint8_t data[16];
          success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, key_data);

          if (success) {
            Serial.println("Sector 1 (Blocks 4..7) has been authenticated");
            success = nfc.mifareclassic_ReadDataBlock(4, data);

            if (success) {
              Serial.println("Reading Block 4:");
              nfc.PrintHexChar(data, 16);
              Serial.println("");
              for (int i = 0; i < 4; i++) {
                saldo.hex[i] = data[i];
                saldo.sekarang |= ((unsigned long)saldo.hex[i] << (24 - (i * 8)));
              }
              //saldo.sekarang = ((unsigned long)saldo.hex[0] << 24) | ((unsigned long)saldo.hex[1] << 16) | ((unsigned long)saldo.hex[2] << 8) | saldo.hex[3];

              lcd.setCursor(0, 1);
              lcd.print(F("Saldo:RP."));
              lcd.print(saldo.sekarang);
              //cekSaldo = true;

              if (topup or bayar) {
                delay(1000);
                if (topup) {
                  topup = false;
                  saldo.tampung = saldo.sekarang + saldo.transaksi;
                  proses_transaksi = true;
                }
                if (bayar) {
                  topup = false;
                  if (saldo.sekarang < saldo.transaksi) {
                    proses_transaksi = false;
                    sukses_transaksi = false;
                  }
                  else {
                    saldo.tampung = saldo.sekarang - saldo.transaksi;
                    proses_transaksi = true;
                  }
                }

                if (proses_transaksi) {
                  for (int i = 0; i < 4; i++) {
                    saldo.hex[i] = (saldo.tampung >> (8 * (3 - i)));
                    data[i] = saldo.hex[i];
                  }
                  //memcpy(data, (const uint8_t[]){saldo.hex[0], saldo.hex[1], saldo.hex[2], saldo.hex[3], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, sizeof data);
                  //memcpy(data, (const uint8_t[]){ 'a', 'd', 'a', 'f', 'r', 'u', 'i', 't', '.', 'c', 'o', 'm', 0, 0, 0, 0 }, sizeof data);

                  success = nfc.mifareclassic_WriteDataBlock (4, data);

                  success = nfc.mifareclassic_ReadDataBlock(4, data);

                  if (success) {
                    if (data[0] == saldo.hex[0]) {
                      saldo.sekarang = saldo.tampung;
                      if (mode_alat == "topup") {
                        lcd.setCursor(0, 1);
                        lcd.print(F("Berhasil Topup  "));
                      }
                      if (mode_alat == "bayar") {
                        lcd.setCursor(0, 1);
                        lcd.print(F("Berhasil Bayar  "));
                      }
                      delay(1000);
                      lcd.setCursor(0, 1);
                      lcd.print(F("                "));
                      lcd.setCursor(0, 1);
                      lcd.print(F("Saldo:Rp."));
                      lcd.print(saldo.sekarang);
                      delay(1000);
                      sukses_transaksi = true;
                    }
                  }
                  else {
                    sukses_transaksi = false;
                  }
                }
              }
            }
            else {
              //Serial.println("Ooops ... unable to read the requested block.  Try another key?");
              //cekSaldo = true;
              sukses_transaksi = false;
            }
          }
          else {
            //Serial.println("Ooops ... authentication failed: Try another key?");
            //cekSaldo = true;
            sukses_transaksi = false;
            warning_card = true;
          }
        }
        Serial.println("");

        if (sukses_transaksi) {
          String urlFinal = "";
          if (mode_alat == "topup") {
            urlFinal = "https://script.google.com/macros/s/" + GOOGLE_SCRIPT_ID + "/exec?" + "kartu=" + kartuHex + "&topup=" + saldo.transaksi + "&bayar=" + 0 + "&saldo=" + saldo.sekarang;
          }
          else if (mode_alat = "bayar") {
            urlFinal = "https://script.google.com/macros/s/" + GOOGLE_SCRIPT_ID + "/exec?" + "kartu=" + kartuHex + "&topup=" + 0 + "&bayar=" + saldo.transaksi + "&saldo=" + saldo.sekarang;
          }

          lcd.clear();
          delay(10);
          lcd.setCursor(0, 0);
          lcd.print("Mengirim Data");
          for (int i = 0; i < 3; i++) {
            lcd.print(".");
            delay(150);
          }

          Serial.print("POST data to spreadsheet:");
          Serial.println(urlFinal);
          HTTPClient http;
          http.begin(urlFinal.c_str());
          http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
          int httpCode = http.GET();
          Serial.print("HTTP Status Code: ");
          Serial.println(httpCode);
          //---------------------------------------------------------------------
          //getting response from google sheet
          String payload;
          if (httpCode > 0) {
            payload = http.getString();
            Serial.println("Payload: " + payload);
          }
          //---------------------------------------------------------------------
          http.end();
          
          saldo.sekarang = 0;
          saldo.tampung = 0;
          saldo.transaksi = 0;
          //topup = false;
          auth_transaksi = false;
          sukses_transaksi = false;
          topup = false;
          bayar = false;
          mode_alat = "cek saldo";
        }
        else {
          if (!warning_card && uidLength == 4) {
            if (mode_alat == "topup") {
              topup = true;
              lcd.setCursor(0, 1);
              lcd.print(F("                "));
              lcd.setCursor(0, 1);
              lcd.print(F("GAGAL Topup"));
            }
            if (mode_alat == "bayar") {
              bayar = true;
              lcd.setCursor(0, 1);
              lcd.print(F("                "));
              lcd.setCursor(0, 1);
              lcd.print(F("GAGAL Bayar"));
              if (!proses_transaksi) {
                delay(500);
                lcd.setCursor(0, 1);
                lcd.print(F("                "));
                lcd.setCursor(0, 1);
                lcd.print(F("Saldo Kurang"));
              }
            }
          }
          else {
            warning_card = false;
            lcd.setCursor(0, 1);
            lcd.print(F("                "));
            lcd.setCursor(0, 1);
            lcd.print(F("Kartu tdk benar"));
          }
        }
        delay(2000);
        timeLastCardRead = millis();
        ms_scan = millis();
        kartuHex = "";
        readerDisabled = true;
        baca_kartu = false;
        lcd.clear();
        delay(10);
        cekSaldo = true;
        saldo.sekarang = 0;
      }
    }
    kartuHex = "";
  }
  else {
    //baca_kartu = false;
    readerDisabled = true;
    ms_scan = millis();
    timeLastCardRead = millis();
    //cekSaldo = true;
  }
}
