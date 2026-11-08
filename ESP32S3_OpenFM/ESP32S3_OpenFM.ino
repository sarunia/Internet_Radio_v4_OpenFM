/*
  OPENFM HLS - SAMODZIELNY ODTWARZACZ TESTOWY
  ESP32-S3 N16R8 / ILI9488 IPS 480x320 / pilot NEC GPIO15 / PCM5102A
  Zamiast problematycznego HLSStream.h: wlasny odczyt MASTER/MEDIA/TS
  AAC-MPEG-TS -> MTSDecoder + AACDecoderHelix -> I2S0
  200 nazw stacji z GETRADIO; ID OFM z HTTPS redirect (z wyjatkiem 2 zapisanych).

  UWAGA: testowa weryfikacja HTTPS jest wylaczona (setInsecure).
  115200 baud, w terminalu komendy: g 1..200 | o OFM10 | s | r | + | - | m | h
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <DS_DIGII35pt7b.h>     // Oryginalna cyfrowa czcionka zegara z projektu użytkownika
#include <esp_system.h>
#include "OpenFmPlayer.h"
#include "OpenFmMetadata.h"
#include "OpenFmCatalog.h"

// Te same piny TFT i pilota co w Twoim glownym projekcie.
static constexpr int PIN_TFT_CS=5, PIN_TFT_DC=4;
static constexpr int PIN_TFT_SCK=11, PIN_TFT_MISO=13, PIN_TFT_MOSI=12;
static constexpr int PIN_IR=15;
static constexpr uint16_t WIDTH=480, HEIGHT=320;
SPIClass spi(HSPI);
GFXcanvas16 canvas(WIDTH,HEIGHT);

static void cmd(uint8_t c) {digitalWrite(PIN_TFT_DC,LOW);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(c);digitalWrite(PIN_TFT_CS,HIGH);}
static void dataByte(uint8_t d) {digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(d);digitalWrite(PIN_TFT_CS,HIGH);}
static void window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
  cmd(0x2a); digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(x0>>8);spi.transfer(x0);spi.transfer(x1>>8);spi.transfer(x1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2b);digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(y0>>8);spi.transfer(y0);spi.transfer(y1>>8);spi.transfer(y1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2c);
}
static void tftBegin(){
  pinMode(PIN_TFT_CS,OUTPUT);pinMode(PIN_TFT_DC,OUTPUT);
  digitalWrite(PIN_TFT_CS,HIGH);
  spi.begin(PIN_TFT_SCK,PIN_TFT_MISO,PIN_TFT_MOSI,-1);
  // Oryginalne radio ustawialo 40 MHz. Bez beginTransaction() SPI dziala
  // ze znacznie nizszym zegarem i odswiezanie canvasu jest bardzo wolne.
  spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  cmd(0x01);delay(120);
  cmd(0xc0);dataByte(0x17);dataByte(0x15);
  cmd(0xc1);dataByte(0x41);
  cmd(0xc5);dataByte(0x00);dataByte(0x12);dataByte(0x80);
  cmd(0x36);dataByte(0x28);
  cmd(0x21); // IPS inversion - jak w dotychczasowym projekcie
  cmd(0x3a);dataByte(0x66); // RGB666
  cmd(0xb0);dataByte(0x00);
  cmd(0xb1);dataByte(0x50);
  cmd(0xb4);dataByte(0x02);
  cmd(0xb6);dataByte(0x02);dataByte(0x02);dataByte(0x3b);
  cmd(0xb7);dataByte(0xc6);
  cmd(0xf7);dataByte(0xa9);dataByte(0x51);dataByte(0x2c);dataByte(0x82);
  cmd(0x11);delay(120);cmd(0x13);cmd(0x29);delay(100);
  if(!canvas.getBuffer()) Serial.println("[TFT] Uwaga: brak RAM canvas");
}
static void drawTft(){
  if(!canvas.getBuffer())return;
  window(0,0,WIDTH-1,HEIGHT-1);
  digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  static uint8_t line[WIDTH*3];
  for(int y=0;y<HEIGHT;y++) {
    const uint16_t* pix=canvas.getBuffer()+y*WIDTH;
    for(int x=0;x<WIDTH;x++) {
      uint16_t c=pix[x];line[x*3]=(c>>8)&0xF8;
      line[x*3+1]=(c>>3)&0xFC;line[x*3+2]=(c<<3)&0xF8;
    }
    spi.writeBytes(line,sizeof(line));
  }
  digitalWrite(PIN_TFT_CS,HIGH);
}

// ---------------------------------------------------------------------------
// Wysylanie tylko zmienionego prostokata z CANVAS do ILI9488.
// CALY canvas jest nadal w PSRAM, lecz zamiast 460 KB wysylamy zwykle kilka KB.
// Ten sposob znacznie skraca czas blokowania glownej petli i obslugi pilota.
// ---------------------------------------------------------------------------
static void pushCanvasRegion(int x,int y,int w,int h) {
  if(!canvas.getBuffer())return;
  x=constrain(x,0,(int)WIDTH-1); y=constrain(y,0,(int)HEIGHT-1);
  w=min(w,(int)WIDTH-x); h=min(h,(int)HEIGHT-y);
  if(w<=0||h<=0)return;
  window(x,y,x+w-1,y+h-1);
  digitalWrite(PIN_TFT_DC,HIGH); digitalWrite(PIN_TFT_CS,LOW);
  static uint8_t line[WIDTH*3];
  for(int row=0;row<h;row++){
    const uint16_t* pix=canvas.getBuffer()+(y+row)*WIDTH+x;
    for(int px=0;px<w;px++){
      uint16_t c=pix[px];
      line[px*3]=(c>>8)&0xF8;
      line[px*3+1]=(c>>3)&0xFC;
      line[px*3+2]=(c<<3)&0xF8;
    }
    spi.writeBytes(line,w*3);
  }
  digitalWrite(PIN_TFT_CS,HIGH);
}

// Pilot NEC, bez dodatkowej biblioteki: pomiar mark/space na przerwaniu GPIO15.
static volatile uint32_t rawBits=0;
static volatile uint8_t bitIndex=0;
static volatile uint8_t irState=0; // 0=start, 1=lead space, 2=data
static volatile uint32_t lastTransition=0;
static volatile uint32_t pendingIr=0;
static volatile bool irReady=false;
static void IRAM_ATTR irChange(){
  uint32_t now=micros(),dt=now-lastTransition;lastTransition=now;
  bool level=digitalRead(PIN_IR);
  if(level) { // koniec stanu LOW
    if(dt>8200 && dt<10500) {irState=1;bitIndex=0;rawBits=0;}
  } else { // koniec stanu HIGH (space)
    if(irState==1){if(dt>3500&&dt<5700) irState=2; else irState=0;}
    else if(irState==2) {
      if(dt>350&&dt<950) {} // 0
      else if(dt>1250&&dt<2150) rawBits|=(1UL<<bitIndex);
      else {irState=0;return;}
      if(++bitIndex==32){pendingIr=rawBits;irReady=true;irState=0;}
    }
  }
}

// 200 pozycji z rzeczywistymi nazwami i slugami GETRADIO.
// ID nie jest rowne numerowi pozycji. Nieznane ID sa ustalane poprzez HTTPS
// z Location przekierowania; nie zgadujemy OFM1...OFM200.
static constexpr int COUNT=200;
static String channelIdForSlot(int slot) {return OpenFmCatalog::knownId(slot);}

static String currentArtist;
static String currentTitle;
static uint32_t shownMetadataVersion=0; // tylko zmiany ramek ID3 odswiezaja panel

static int station=0,volume=14;
static bool pendingPlay=false;
static bool pendingStop=false; // sygnalizujemy STOP tylko przy pierwszej zmianie
static bool muted=false;
static String ownId;
static String lastCommand;
static String serialLine;
static uint32_t lastInfoDraw=0,lastClockPoll=0,lastStopReport=0,lastIr=0;
static uint32_t switchRequestedAt=0;
static bool screenDirty=true;              // Wymuszenie przebudowania tekstów po zmianie kanału/komendy.
static bool clockDrawn=false;               // Po starcie zawsze rysujemy zegar.
static String lastClockText;                // Porównujemy HH:MM:SS, nie liczbę milisekund.
static String lastHeadlineSignature;         // Zmiana nazwy/statusu uruchamia tylko górny obszar.
static String lastAudioSignature;            // Parametry odświeżamy wyłącznie po zmianie danych.

// Zamknij stary strumien asynchronicznie; nowy uruchamia sie dopiero po busy()==false.
// Nie blokujemy odczytu przyciskow, gdy HTTPS pobiera jeszcze segment.
static void requestSwitch(){
  pendingPlay=true;
  screenDirty=true;                      // Nowa stacja musi pojawić się od razu na TFT.
  switchRequestedAt=millis();
  if(OpenFmPlayer::busy()) {
    if(!pendingStop) {
      Serial.println("[STEROWANIE] Zatrzymuję poprzedni strumień HLS...");
      OpenFmPlayer::stop();
      pendingStop=true;
    }
  } else {
    pendingStop=false;
  }
}
static void selectStation(int idx){
  station=(idx+COUNT)%COUNT;
  ownId="";
  currentArtist="";currentTitle="";
  Serial.printf("[STACJA] Wybrano %d/%d: %s (%s)\n",station+1,COUNT,
                OpenFmCatalog::entry(station).name,channelIdForSlot(station).c_str());
  requestSwitch();
}
static void customStation(String id){
  id.trim();id.toUpperCase();if(!id.startsWith("OFM"))id="OFM"+id;
  if(id.length()<4)return;
  ownId=id;
  currentArtist="";currentTitle="";
  Serial.println("[STACJA] Własny kanał: "+id);
  requestSwitch();
}
static void playCurrent(){
  String id=ownId.length()?ownId:OpenFmCatalog::resolveId(station);
  if(id.isEmpty()) {
    Serial.printf("[STACJA] Brak zgodnego strumienia OpenFM dla pozycji %d (%s). Uzyj n/p.\n",
                  station+1,OpenFmCatalog::entry(station).name);
    return;
  }
  screenDirty=true; // teraz mozemy pokazac odkryte OFMxxx
  Serial.println("[TEST] Uruchamiam "+id);
  if(!OpenFmPlayer::start(id,muted?0:volume))Serial.println("[TEST] Nie udalo sie wystartowac HLS");
}
static void printHelp(){
 Serial.println("\n====== OPENFM HLS PLAYER ======");
 Serial.println("g 1..200 - wybierz pozycje; 1..9 - szybki wybor");
 Serial.println("Katalog 200 nazw; OFM jest ustalane z przekierowania GETRADIO. Nie wszystkie pozycje musza byc HLS.");
 Serial.println("o OFM10 - dowolny identyfikator OFM");
 Serial.println("n/p - nastepny/poprzedni kanal");
 Serial.println("meta wykonawca|tytul - PODGLAD TESTOWY metadanych (nie dane OpenFM)");
 Serial.println("s - stop, r - start, + / - - glosnosc, m - pamiec, h - pomoc");
 Serial.println("Pilot NEC GPIO15: lewo/prawo, VOL+/VOL-, STOP, OK, MUTE");
 Serial.println("Nie wypisujemy adresow z tokenami do Serial.");
 Serial.println("================================\n");
}
static void commandLine(String input){
  input.trim();if(!input.length())return;
  lastCommand=input;
  if(input.startsWith("g ") || input.startsWith("G ")){
    String n=input.substring(2);n.trim();int v=n.toInt();
    if(v>=1&&v<=COUNT)selectStation(v-1);
    else Serial.println("[STACJA] Dozwolone numery: 1..200");
  }
  else if(input.startsWith("meta ")){
    String line=input.substring(5);int divider=line.indexOf('|');
    if(divider>=0){
      currentArtist=line.substring(0,divider);currentTitle=line.substring(divider+1);
      currentArtist.trim();currentTitle.trim();screenDirty=true;
      Serial.println("[META] Dane reczne tylko do testu wyswietlacza.");
    } else Serial.println("[META] Uzyj: meta wykonawca|tytul");
  }
  else if(input.length()==1 && input[0]>='1' && input[0]<='9') selectStation(input[0]-'1');
  else if(input.startsWith("o ")||input.startsWith("O "))customStation(input.substring(2));
  else if(input=="n")selectStation(station+1);
  else if(input=="p")selectStation(station-1);
  else if(input=="s"){pendingPlay=false;pendingStop=OpenFmPlayer::busy();OpenFmPlayer::stop();}
  else if(input=="r")requestSwitch();
  else if(input=="+"){volume=min(21,volume+1);muted=false;OpenFmPlayer::setVolume(volume);}
  else if(input=="-"){volume=max(0,volume-1);muted=false;OpenFmPlayer::setVolume(volume);}
  else if(input=="m")OpenFmPlayer::stats();
  else if(input=="h")printHelp();
}
static void handleRemote(){
 if(!irReady)return;
 noInterrupts();uint32_t ir=pendingIr;irReady=false;interrupts();
 uint8_t address=ir&255,invAddr=(ir>>8)&255,key=(ir>>16)&255,invKey=(ir>>24)&255;
 if((uint8_t)(key^invKey)!=0xff) {Serial.println("[IR] Niepoprawna ramka NEC");return;}
 // Adres NEC bywa rozszerzony; w pilocie rozpoznajemy kod komendy.
 if(millis()-lastIr<120)return;lastIr=millis();
 Serial.printf("[IR] addr=0x%02X addr2=0x%02X cmd=0x%02X\n",address,invAddr,key);
 switch(key){
  case 0x26: // w prawo - pilot Twojego głównego radia
  case 0x22: // w dół - następna pozycja
    selectStation(station+1);break;
  case 0x27: // w lewo
  case 0x30: // w górę - poprzednia pozycja
    selectStation(station-1);break;
  case 0x28:volume=min(21,volume+1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case 0x24:volume=max(0,volume-1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case 0x33:pendingPlay=false;pendingStop=OpenFmPlayer::busy();OpenFmPlayer::stop();break; // STOP
  case 0x25:requestSwitch();break; // OK - restart wybranej stacji
  case 0x29:muted=!muted;OpenFmPlayer::setVolume(muted?0:volume);break;
  case 0x18:selectStation(station+1);break; // FF+ z pilota
  case 0x19:selectStation(station-1);break; // FF- z pilota
  default:break;
 }
}
// ============================================================================
// EKRAN ILI9488: CANVAS - UKŁAD Z ORYGINALNEGO RADIA
// ============================================================================
// Oryginalny projekt rysował całość do GFXcanvas16 i następnie wysyłał bufor
// do ILI9488. Zachowujemy tę metodę (brak migotania), lecz dla responsywności
// rozdzielamy ekran na 3 małe obszary. Pełnych 480x320 nie wysyłamy co sekundę.
// Każda funkcja rysuje najpierw w pamięci, a potem wysyła TYLKO swój prostokąt.
//
//  0..188 px  : nazwa stacji, numer, status i głośność - tylko przy zmianie.
// 188..260 px : aktualne parametry strumienia - tylko gdy wartości się zmienią.
// 260..320 px : liczniki po lewej i cyfrowy zegar po prawej, osobno.
//
// Dzięki temu praca sieciowa i dekoder AAC nie czekają na wielokrotne wysłanie
// setek kilobajtów obrazu po SPI.
// ============================================================================

// Naglowek: numer pozycji jest PIERWSZA LINIA, bez nazw kanalow.
// Wykonawca/tytul sa rezerwowane na przyszle sprawdzone zrodlo metadanych.
// Nie parsujemy ich z tokenu HLS, bo nie ma ich w jego odpowiedzi JSON.
static void renderHeadline(bool force=false) {
  if(!canvas.getBuffer()) return;
  const String status = pendingPlay ? "ZMIANA..." : OpenFmPlayer::state();
  const String id = ownId.length() ? ownId : channelIdForSlot(station);
  const String stationName = ownId.length() ? "Reczny kanal OpenFM" : String(OpenFmCatalog::entry(station).name);
  const String signature = String(station)+"|"+id+"|"+stationName+"|"+status+"|"+
                           String(muted?0:volume)+"|"+currentArtist+"|"+currentTitle;
  if(!force && signature==lastHeadlineSignature)return;
  lastHeadlineSignature=signature;

  canvas.fillRect(0,0,WIDTH,188,0x0000);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(3);
  canvas.setTextColor(0x07EF);  // Wiosenna zieleń (RGB565)
  canvas.setCursor(12,10);
  canvas.printf("Open FM stacja %03d/%03d",station+1,COUNT); // stala pozycja pierwszej linii
  canvas.drawFastHLine(8,49,WIDTH-16,0x39E7);

  canvas.setTextSize(2);
  canvas.setTextColor(0xFEA0);  // Złoty (RGB565)
  canvas.setCursor(12,56);
  // Przy 2x (12 px/znak) miesci sie ok. 38 znakow od x=12.
  // Zostawiamy priorytet nazwie. OFM ID wypisujemy w monitorze Serial.
  canvas.print(stationName.substring(0,38));

  // 34 znaki przy GFX 2x zajmuja mniej niz szerokosc panelu 480 px.
  canvas.setTextColor(0xBDF7);
  canvas.setCursor(12,83);
  canvas.print("Wykonawca: ");
  canvas.print(currentArtist.isEmpty()?"--":currentArtist.substring(0,27));
  canvas.setCursor(12,110);
  canvas.print("Tytul: ");
  canvas.print(currentTitle.isEmpty()?"--":currentTitle.substring(0,31));

  canvas.setTextColor(0xFD20);
  canvas.setCursor(12,137);
  canvas.print("Stan: ");canvas.print(status);
  canvas.setTextColor(0x07E0);
  canvas.setCursor(12,163);
  canvas.print("VOL: ");canvas.print(muted?0:volume);canvas.print("/21");
  pushCanvasRegion(0,0,WIDTH,188);
}

static void renderAudioInfo(bool force=false) {
  if (!canvas.getBuffer()) return;
  const uint32_t kbps = OpenFmPlayer::bitrateKbps();            // Pomiar TS, nie AAC.
  const uint32_t hz = OpenFmPlayer::sampleRate();               // Parametr z Helix.
  const uint8_t bits = OpenFmPlayer::bitsPerSample();           // Rozdzielczość PCM.
  const uint8_t channels = OpenFmPlayer::channels();           // Kanały audio.
  const uint32_t segments = OpenFmPlayer::segments();          // Licznik pobranych segmentów.
  const uint32_t segmentBytes = OpenFmPlayer::lastSegmentBytes(); // Bajty ostatniego kompletnego segmentu TS.

  // Licznik segmentów zmienia się co kilka sekund; nie rysujemy nic, gdy
  // żadna wartość nie zmieniła się od poprzedniego kadru.
  const String signature = String(kbps) + "|" + String(hz) + "|" + String(bits)
                          + "|" + String(channels) + "|" + String(segments)
                          + "|" + String(segmentBytes);
  if (!force && signature == lastAudioSignature) return;
  lastAudioSignature = signature;

  canvas.fillRect(0, 188, WIDTH, 72, 0x0000);
  canvas.fillRect(0, 260, 243, 60, 0x0000);                   // LEWA część dolnego paska czarna.
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);                                      // Każdy napis >= 2x GFX.
  canvas.setTextColor(0xFFFF);

  canvas.setCursor(12, 193);
  canvas.print("TS: ");
  if (kbps) canvas.print(kbps); else canvas.print("--");
  canvas.print(" kb/s");

  canvas.setCursor(12, 224);
  canvas.print("Hz: ");
  if (hz) canvas.print(hz); else canvas.print("--");
  canvas.print("  PCM: ");
  if (bits) canvas.print(bits); else canvas.print("--");
  canvas.print(" bit");

  canvas.setTextColor(0xBDF7);
  // Trzy wiersze w lewym panelu 243x60 px; czcionka GFX 2x (wys. 16 px).
  // Bajty seg. to DOKLADNY rozmiar ostatniego CALKOWICIE pobranego pliku TS.
  // Nie jest to narastajace receivedBytes z raportu [AUDIO] na porcie Serial.
  canvas.setCursor(12, 262);
  canvas.print("Segmenty: ");
  canvas.print(segments);
  canvas.setCursor(12, 282);
  canvas.print("Bajty seg.: ");
  canvas.print(segmentBytes);
  canvas.setCursor(12, 302);
  canvas.print("Kanaly: ");
  if (channels) canvas.print(channels); else canvas.print("--");

  pushCanvasRegion(0, 188, WIDTH, 72);                         // Tylko parametry u góry.
  pushCanvasRegion(0, 260, 243, 60);                          // Tylko liczniki u dołu.
}

// ============================================================================
// ZEGAR - DOSLOWNIE TA SAMA CZCIONKA I BAZA PISANIA CO W ORYGINALE.
// Pozycja bazowa jest STALA: 245,310. Nie wyrownujemy do prawej strony!
// Nie ma tez awaryjnej zamiany na zwykla czcionke GFX przy szerszej cyfrze.
// Kadr zegara jest wysylany przez SPI 40 MHz tylko przy zmianie sekundy.
// ============================================================================
static void renderClock(bool force=false) {
  if (!canvas.getBuffer()) return;
  time_t now = time(nullptr);
  if (now < 1600000000) return;
  struct tm t;
  localtime_r(&now, &t);
  char timeText[12];
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  if (!force && lastClockText == timeText) return;
  lastClockText = timeText;

  // Wierna pozycja z pierwotnego drawClock() w radio_v3_ILI9488_canvas.ino.
  // Rysowanie jest stale lewostronne niezaleznie od szerokosci cyfr.
  static constexpr int CLOCK_X = 245;
  static constexpr int CLOCK_Y = 260;
  static constexpr int CLOCK_W = WIDTH - CLOCK_X;
  static constexpr int CLOCK_H = HEIGHT - CLOCK_Y;
  canvas.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, 0x0000);
  canvas.setFont(&DS_DIGII35pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor(0x07FF);
  canvas.setTextWrap(false);
  canvas.setCursor(245, 310);  // Oryginalna stala baza, bez przeliczania szerokosci.
  canvas.print(timeText);
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  pushCanvasRegion(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  clockDrawn = true;
}

// Oddzielne zadanie odczytuje ID3 z MPEG-TS; ekran pobiera tylko gotowy wynik.
// Bez HTTPS, dekodowania ani operacji blokujacych w petli TFT.
static void updateOpenFmMetadata() {
  String artist,title;
  uint32_t version=0;
  OpenFmMetadata::snapshot(artist,title,version);
  if(version==shownMetadataVersion) return;
  shownMetadataVersion=version;
  currentArtist=artist;
  currentTitle=title;
  screenDirty=true;
}

static void screenLoop() {
  updateOpenFmMetadata();
  // Reakcja na pilota nie musi czekać na kolejny cykl zegara.
  const bool force = screenDirty;
  if (force) screenDirty = false;
  renderHeadline(force);

  // Odpytywanie parametrów częściej niż co sekundę nie poprawia czytelności,
  // bo same segmenty HLS są pobierane w odstępach kilku sekund.
  if (force || millis() - lastInfoDraw >= 1200) {
    lastInfoDraw = millis();
    renderAudioInfo(force);
  }

  // Czas odczytujemy około 10 razy na sekundę, lecz do SPI trafia nowy obraz
  // dokładnie przy zmianie sekundy (zwykle raz na 1000 ms).
  if (!clockDrawn || millis() - lastClockPoll >= 100) {
    lastClockPoll = millis();
    renderClock(force);
  }
}

void setup(){
 Serial.begin(115200);delay(400);
 Serial.printf("[BOOT] reset=%d heap=%u psram=%u\n",(int)esp_reset_reason(),(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
 tftBegin();
 pinMode(PIN_IR,INPUT_PULLUP);
 lastTransition=micros();attachInterrupt(digitalPinToInterrupt(PIN_IR),irChange,CHANGE);
 WiFi.mode(WIFI_STA);
 WiFiManager wm;
 wm.setConfigPortalTimeout(150);
 if(!wm.autoConnect("ESP32S3-OpenFM-TEST")){
    Serial.println("[WiFi] Nie polaczono. Restart za 3s.");delay(3000);ESP.restart();
 }
 Serial.printf("[WiFi] IP=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());
 // Polska strefa czasowa, automatyczna zmiana CET/CEST jak w glownym radiu.
 configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.google.com");
 printHelp();
 Serial.println("[AUTO] Wi-Fi gotowe. Uruchamiam pierwszą stację listy.");
 selectStation(0);
 // Jednorazowe wyczyszczenie całości usuwa także pozostałości z poprzedniego
 // programu oraz niepożądane szare tło dolnego paska.
 canvas.fillScreen(0x0000);
 drawTft();
 screenDirty = true;
 screenLoop();
}
void loop(){
 while(Serial.available()){
   char c=Serial.read();
   if(c=='\n'||c=='\r') {if(serialLine.length()){commandLine(serialLine);serialLine="";}}
   else if(serialLine.length()<350)serialLine+=c;
 }
 handleRemote();
 if(pendingPlay && !OpenFmPlayer::busy()){
   pendingPlay=false;pendingStop=false;playCurrent();
 }
 if(pendingStop && !OpenFmPlayer::busy())pendingStop=false;
 // Gdy HTTPS faktycznie utknie, nie podmieniamy zasobow na sile.
 // Serial wyjasni dlaczego pilot czeka, a kolejne strzalki wybieraja ostatni kanal.
 if(pendingPlay && OpenFmPlayer::busy() && millis()-switchRequestedAt>6500 &&
    millis()-lastStopReport>5000){
   lastStopReport=millis();
   Serial.printf("[STEROWANIE] Oczekiwanie na STOP %lu ms; %s\n",
       (unsigned long)(millis()-switchRequestedAt),OpenFmPlayer::state().c_str());
 }
 screenLoop();
 delay(3);
}
