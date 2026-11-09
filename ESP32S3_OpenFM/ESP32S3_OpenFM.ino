/*=============================================================================
  PROJEKT : OPENFM HLS INTERNET RADIO - ESP32-S3
  PLIK    : ESP32S3_OpenFM.ino
  ROLA    : Glowny szkic Arduino - inicjalizacja, sterowanie, pilot i ekran TFT

  OPIS PROJEKTU
  -----------------------------------------------------------------------------
  Odtwarzacz radia internetowego OpenFM dla modulu ESP32-S3 N16R8. Szkic
  odpowiada za uruchomienie urzadzenia, konfiguracje Wi-Fi, odbior pilota NEC,
  obsluge polecen z portu szeregowego oraz prezentacje informacji na ekranie
  ILI9488 480x320. Wlasciwy silnik pobierania HLS, dekodowanie AAC i wyjscie
  I2S znajduja sie w osobnym module OpenFmPlayer. Metadane utworow ID3v2
  analizuje OpenFmMetadata, a nazwy i identyfikatory kanalow udostepnia
  OpenFmCatalog.

  TOR AUDIO (modul OpenFmPlayer, nie ten plik)
  -----------------------------------------------------------------------------
  OpenFM API / token -> playlista HLS MASTER -> MEDIA -> segmenty MPEG-TS
  -> MTSDecoder + AACDecoderHelix -> VolumeStream -> I2S -> PCM5102A.
  Czestotliwosc probkowania jest rozpoznawana na podstawie danych dekodera AAC;
  konfiguracja I2S powinna odpowiadac rzeczywistemu sample rate dekodera.

  SPRZET I POLACZENIA
  -----------------------------------------------------------------------------
  Modul       : ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM - zgodnie z konfiguracja)
  Wyswietlacz : ILI9488 IPS, 480 x 320, magistrala SPI / format RGB666 na TFT
  Pilot       : odbiornik IR NEC, GPIO15 (dekodowanie programowe przez przerwanie)
  DAC         : PCM5102A, wyjscie I2S (piny BCLK/LRCK/DATA konfigurowane w OpenFmPlayer)
  SPI TFT     : SCK=GPIO11, MOSI=GPIO12, MISO=GPIO13, CS=GPIO5, DC=GPIO4
  Zegar       : DS_DIGII35pt7b - niestandardowa czcionka dostarczona osobno

  SRODOWISKO I BIBLIOTEKI
  -----------------------------------------------------------------------------
  Platforma   : Arduino IDE + pakiet plytek ESP32 firmy Espressif
  ESP32 core  : 3.3.12 (wersja widoczna w przekazanym logu kompilacji)
  Arduino IDE : numer wersji nie jest zapisany w szkicu - uzupelnic wg instalacji
  WiFiManager : biblioteka WiFiManager (wersja nieustalona na podstawie zalaczonych plikow)
  Adafruit GFX: biblioteka Adafruit GFX Library (wersja nieustalona)
  SPI         : biblioteka SPI dolaczana z pakietem ESP32 Arduino core
  Font        : DS_DIGII35pt7b.h - plik projektu, nie standardowa czcionka GFX
  Moduly      : OpenFmPlayer, OpenFmResolver, OpenFmMetadata, OpenFmCatalog
  AudioTools / CodecHelix / CodecMTS sa wykorzystywane w OpenFmPlayer.cpp.
  Ich dokladnych wersji nie da sie ustalic z samego pliku INO. Przed publikacja
  nalezy wpisac numery wersji z Arduino IDE -> Library Manager / plikow biblioteki.

  PAMIEC I WYDAJNOSC
  -----------------------------------------------------------------------------
  GFXcanvas16 przechowuje obraz w formacie RGB565 (16 bit/piksel). Dla 480x320
  bufor wymaga ok. 307 200 bajtow. Wysylanie fragmentow ekranu ogranicza czas
  zajecia SPI i pozwala petli glownej szybko obslugiwac pilot oraz Serial.
  Wysylanie do ILI9488 odbywa sie z zegarem SPI 40 MHz.

  OBSLUGA UZYTKOWNIKA
  -----------------------------------------------------------------------------
  Port szeregowy : 115200 bit/s
  Komendy        : g 1..200, o OFM10, n, p, s, r, +, -, m, h
  Test ekranu    : meta wykonawca|tytul (dane reczne, tylko do testu)
  Pilot NEC      : gora/dol lista, OK zatwierdza, lewo/prawo zmienia stacje, VOL, STOP, MUTE

  BEZPIECZENSTWO I OGRANICZENIA
  -----------------------------------------------------------------------------
  W modulach sieciowych wystepuje WiFiClientSecure::setInsecure(). Oznacza to,
  ze certyfikat TLS serwera nie jest weryfikowany. To ustawienie ulatwia testy,
  ale nie jest zalecane w urzadzeniu produkcyjnym. Tokenow HLS nie nalezy
  wypisywac do logow ani umieszczac na stale w kodzie lub repozytorium.

  WAZNE
  -----------------------------------------------------------------------------
  Ten plik jest glownym szkicem i korzysta z pozostalych plikow projektu. Nie
  jest samodzielny: do kompilacji musza byc obecne odpowiednie pliki .h/.cpp,
  w tym OpenFmPlayer, OpenFmResolver, OpenFmMetadata i OpenFmCatalog oraz font.

  Wersja dokumentacji: 1.0
  Data opracowania     : 2026-11-08
=============================================================================*/
// Podstawowe API Arduino: GPIO, czas, Serial, typy i funkcje pomocnicze.
#include <Arduino.h>
// Sterownik Wi-Fi ESP32; wymagany do polaczenia z siecia lokalna.
#include <WiFi.h>
// Portal konfiguracyjny Wi-Fi - zapis i ponowne wykorzystanie danych sieci.
#include <WiFiManager.h>
// Sprzetowa magistrala SPI uzywana do komunikacji z kontrolerem ILI9488.
#include <SPI.h>
// Biblioteka grafiki: GFXcanvas16, tekst, linie i prostokaty.
#include <Adafruit_GFX.h>
// Wlasna czcionka cyfrowa zegara - musi byc dostepna jako osobny plik naglowkowy.
#include <DS_DIGII35pt7b.h>
// Funkcje systemowe ESP-IDF, m.in. identyfikacja przyczyny resetu.
#include <esp_system.h>
// Interfejs silnika HLS/AAC/I2S - implementacja znajduje sie w OpenFmPlayer.cpp.
#include "OpenFmPlayer.h"
// Interfejs parsera ID3v2 wydzielajacego wykonawce i tytul z MPEG-TS.
#include "OpenFmMetadata.h"
// Interfejs katalogu nazw stacji, slugow i identyfikatorow OFM.
#include "OpenFmCatalog.h"

// ---------------------------------------------------------------------------
// KONFIGURACJA SPRZETOWA - piny zgodne z bazowym projektem radia.
// Zmiana polaczen wymaga aktualizacji ponizszych stalych oraz konfiguracji I2S
// w OpenFmPlayer.cpp. Nie zmieniac pinow bez sprawdzenia polaczen plytki.
// ---------------------------------------------------------------------------
static constexpr int PIN_TFT_CS=5, PIN_TFT_DC=4;             // CS i D/C wyswietlacza.
static constexpr int PIN_TFT_SCK=11, PIN_TFT_MISO=13, PIN_TFT_MOSI=12; // Linie SPI TFT.
static constexpr int PIN_IR=15;                              // Wyjscie odbiornika podczerwieni.
static constexpr uint16_t WIDTH=480, HEIGHT=320;            // Rozdzielczosc panelu ILI9488.
SPIClass spi(HSPI);                                       // Osobny obiekt magistrali SPI ESP32.
GFXcanvas16 canvas(WIDTH,HEIGHT);                         // Bufor obrazu RGB565 w pamieci RAM/PSRAM.

// Wysyla pojedynczy bajt polecenia kontrolera ILI9488 (D/C=LOW).
static void cmd(uint8_t c) {digitalWrite(PIN_TFT_DC,LOW);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(c);digitalWrite(PIN_TFT_CS,HIGH);}
// Wysyla pojedynczy bajt parametru polecenia ILI9488 (D/C=HIGH).
static void dataByte(uint8_t d) {digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(d);digitalWrite(PIN_TFT_CS,HIGH);}
// Ustawia prostokat pamieci GRAM kontrolera: kolumny, wiersze i start zapisu RAM.
static void window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
  cmd(0x2a); digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(x0>>8);spi.transfer(x0);spi.transfer(x1>>8);spi.transfer(x1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2b);digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(y0>>8);spi.transfer(y0);spi.transfer(y1>>8);spi.transfer(y1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2c);
}
// Inicjalizacja GPIO, SPI 40 MHz i rejestrow kontrolera ILI9488.
// Sekwencja wartosci rejestrow jest dopasowana do konkretnego modulu IPS.
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
// Pelne odswiezenie ekranu z bufora canvas. Uzywane glownie przy starcie.
// GFX przechowuje RGB565, a ILI9488 jest tutaj konfigurowany na 18-bit RGB666,
// dlatego kazdy piksel jest rozbijany na trzy bajty przed wyslaniem po SPI.
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
// Odswieza tylko wskazany fragment canvasu. Wspolrzedne sa przycinane do
// granic ekranu, aby nie wyslac danych poza obszar 480x320.
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

// ---------------------------------------------------------------------------
// ODBIOR PILOTA NEC - dekodowanie czasow impulsow bez dodatkowej biblioteki.
// Przerwanie jedynie sklada 32-bitowa ramke; interpretacja przycisku odbywa sie
// pozniej w handleRemote(), juz poza ISR.
// ---------------------------------------------------------------------------
static volatile uint32_t rawBits=0;             // Skladana 32-bitowa ramka NEC (LSB first).
static volatile uint8_t bitIndex=0;             // Numer kolejnego bitu ramki NEC: 0..31.
static volatile uint8_t irState=0; // 0=start, 1=lead space, 2=data
static volatile uint32_t lastTransition=0;      // Znacznik czasu ostatniego zbocza IR w mikrosekundach.
static volatile uint32_t pendingIr=0;           // Ostatnia kompletna ramka przekazana do loop().
static volatile bool irReady=false;             // Flaga synchronizujaca ISR z petla glowna.
// ISR: rejestruje czas pomiedzy zboczami sygnalu IR i buduje kod NEC.
// IRAM_ATTR umieszcza funkcje w pamieci dostepnej podczas obslugi przerwan.
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

// ---------------------------------------------------------------------------
// KATALOG STACJI I STAN APLIKACJI
// ---------------------------------------------------------------------------
// Numer pozycji (1..200) jest indeksem katalogu, a nie numerem OFM.
// To OpenFmCatalog decyduje, czy ID jest juz znane, czy wymaga rozwiazania.
// W wariancie katalogu statycznego znaneId() zwraca ID lokalnie bez HTTP.
// ---------------------------------------------------------------------------
// 200 pozycji z nazwami i slugami katalogu.
// ID nie jest rowne numerowi pozycji. OpenFmCatalog udostepnia identyfikatory
// potwierdzone w katalogu; brakujace pozycje nie moga byc uzupelniane zgadywaniem.
static constexpr int COUNT=200;

// LISTA STACJI (jak w poprzednim radiu): wybor jest niezalezny od odtwarzania.
// Gora/dol przesuwaja podswietlenie, OK uruchamia wybrana stacje.
// Po 12 s bez przyciskow wracamy na ekran radia bez zmiany odtwarzania.
static constexpr int STATION_LIST_ROWS = 8;       // Liczba widocznych wierszy na TFT 480x320.
static constexpr uint32_t STATION_LIST_TIMEOUT_MS = 12000;
static bool stationListActive = false;            // Czy widoczna jest lista zamiast ekranu radia.
static bool stationListDirty = false;             // Przerysowanie tylko przy zmianie zaznaczenia.
static int stationListSelection = 0;              // Zaznaczony indeks 0..199 (nie zmienia station).
static int stationListFirst = 0;                  // Pierwszy widoczny indeks okna listy.
static uint32_t stationListLastAction = 0;        // Czas ostatniego ruchu/otwarcia menu.

// Zwraca znany identyfikator OFM dla pozycji; nie uruchamia odtwarzania.
static String channelIdForSlot(int slot) {return OpenFmCatalog::knownId(slot);}

static String currentArtist;                         // Ostatni wykonawca odczytany z ID3v2.
static String currentTitle;                          // Ostatni tytul odczytany z ID3v2.
static uint32_t shownMetadataVersion=0;         // Ostatnia wersja ID3 juz pokazana na ekranie.

static int station=0,volume=14;                      // Biezacy indeks katalogu (od 0) i glosnosc 0..21.
static bool pendingPlay=false;                      // Odtwarzanie nowej stacji czeka na zakonczenie STOP.
static bool pendingStop=false;                      // Czy wyslano juz zadanie zatrzymania starej sesji. // sygnalizujemy STOP tylko przy pierwszej zmianie
static bool muted=false;                             // MUTE zachowuje poprzednia wartosc volume.
static String ownId;                                // Reczny identyfikator OFM z komendy Serial.
static String lastCommand;
static String serialLine;                            // Bufor pojedynczej komendy z monitora portu szeregowego.
static uint32_t lastInfoDraw=0,lastClockPoll=0,lastStopReport=0,lastIr=0; // Znaczniki czasu z millis().
static uint32_t switchRequestedAt=0;             // Czas rozpoczecia zmiany stacji - diagnostyka oczekiwania na STOP.
static bool screenDirty=true;                       // Flaga: wymus ponowne narysowanie panelu tekstowego.              // Wymuszenie przebudowania tekstów po zmianie kanału/komendy.
static bool clockDrawn=false;                       // Informuje, czy zegar zostal juz narysowany po starcie.               // Po starcie zawsze rysujemy zegar.
static String lastClockText;                // Porównujemy HH:MM:SS, nie liczbę milisekund.
static String lastHeadlineSignature;         // Zmiana nazwy/statusu uruchamia tylko górny obszar.
static String lastAudioSignature;            // Parametry odświeżamy wyłącznie po zmianie danych.

// Zamknij stary strumien asynchronicznie; nowy uruchamia sie dopiero po busy()==false.
// Nie blokujemy odczytu przyciskow, gdy HTTPS pobiera jeszcze segment.
// Zleca zmiane stacji bez blokowania petli glownej. Najpierw zatrzymuje
// aktualny strumien; nowy kanal wystartuje dopiero po OpenFmPlayer::busy()==false.
static void selectStation(int idx);  // Potrzebne dla zatwierdzenia wyboru z listy.

// Otwiera przewijana liste i zaznacza aktualnie grajaca stacje.
// Nie zatrzymuje HLS; metadane i odtwarzanie pracuja w tle.
static void openStationList() {
  if (stationListActive) return;
  stationListActive = true;
  stationListSelection = station;
  stationListFirst = max(0, stationListSelection - STATION_LIST_ROWS / 2);
  stationListFirst = min(stationListFirst, COUNT - STATION_LIST_ROWS);
  stationListLastAction = millis();
  stationListDirty = true;
  Serial.printf("[LISTA] Otwarta, wybrano %d/%d\n", stationListSelection + 1, COUNT);
}

// Zamyka liste i wymusza jednokrotne odtworzenie wszystkich paneli TFT.
static void closeStationList() {
  if (!stationListActive) return;
  stationListActive = false;
  stationListDirty = false;
  screenDirty = true;
  clockDrawn = false;
  lastHeadlineSignature = "";
  lastAudioSignature = "";
  lastClockText = "";
}

// Przewija zaznaczenie z zawijaniem konca listy do poczatku (i odwrotnie).
// Nie zmienia aktualnie odtwarzanej stacji az do potwierdzenia OK.
static void scrollStationList(int direction) {
  if (!stationListActive) openStationList();
  stationListSelection = (stationListSelection + direction + COUNT) % COUNT;
  if (stationListSelection < stationListFirst)
    stationListFirst = stationListSelection;
  else if (stationListSelection >= stationListFirst + STATION_LIST_ROWS)
    stationListFirst = stationListSelection - STATION_LIST_ROWS + 1;
  stationListFirst = constrain(stationListFirst, 0, COUNT - STATION_LIST_ROWS);
  stationListLastAction = millis();
  stationListDirty = true;
  Serial.printf("[LISTA] Zaznaczono %d/%d: %s\n", stationListSelection + 1,
                COUNT, OpenFmCatalog::entry(stationListSelection).name);
}

// Zatwierdza podswietlony kanal. Ponowny wybor grajacej stacji nie restartuje HLS.
static void confirmStationList() {
  if (!stationListActive) return;
  const int selected = stationListSelection;
  closeStationList();
  if (selected != station || ownId.length()) selectStation(selected);
  else Serial.println("[LISTA] Wybrano obecnie grajaca stacje, bez restartu.");
}

// Rysuje liste 8 stacji z podswietleniem wyboru, numeracja i wskaznikiem ID.
// Uzywa istniejacego canvasu i pushCanvasRegion; bez nowej biblioteki.
static void drawStationList() {
  if (!stationListActive || !stationListDirty || !canvas.getBuffer()) return;
  stationListDirty = false;
  canvas.fillRect(0, 0, WIDTH, HEIGHT, 0x0000);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);
  canvas.setTextColor(0xFEA0);                     // Zloty naglowek.
  canvas.setCursor(10, 7);
  canvas.printf("STACJE OPEN FM  %03d/%03d", stationListSelection + 1, COUNT);
  canvas.drawFastHLine(8, 34, WIDTH - 16, 0x39E7);

  for (int row = 0; row < STATION_LIST_ROWS; ++row) {
    const int index = stationListFirst + row;
    if (index >= COUNT) break;
    const int y = 42 + row * 31;
    const bool selected = (index == stationListSelection);
    if (selected) canvas.fillRoundRect(4, y - 3, WIDTH - 8, 29, 4, 0x18C3);
    canvas.setTextColor(selected ? 0x07FF : 0x07E0); // Czerwony wybor, zielone nazwy.
    canvas.setCursor(10, y + 3);
    canvas.printf("%03d ", index + 1);
    // Nazwy z katalogu sa skrocone do szerokosci ekranu (2x GFX ~12px/znak).
    const String name = String(OpenFmCatalog::entry(index).name);
    canvas.print(name.substring(0, 30));
    if (OpenFmCatalog::knownId(index).isEmpty()) { // ID jeszcze niepotwierdzone.
      canvas.setTextColor(0xFD20);
      canvas.setCursor(449, y + 3);
      canvas.print("?");
    }
  }
  canvas.setTextSize(1);
  canvas.setTextColor(0xFFFF);
  canvas.setCursor(9, 298);
  canvas.print("GORA/DOL: wybor   OK: graj   STOP: zamknij");
  // Menu nie odswieza sie co sekunde, lecz tylko po nacisnieciu przycisku.
  pushCanvasRegion(0, 0, WIDTH, HEIGHT);
}

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
// Wybiera pozycje katalogu z zawijaniem: po 200 wraca do 1, przed 1 do 200.
// Czysci metadane poprzedniego utworu i zleca przelaczenie odtwarzacza.
static void selectStation(int idx){
  closeStationList();
  station=(idx+COUNT)%COUNT;
  ownId="";
  currentArtist="";currentTitle="";
  Serial.printf("[STACJA] Wybrano %d/%d: %s (%s)\n",station+1,COUNT,
                OpenFmCatalog::entry(station).name,channelIdForSlot(station).c_str());
  requestSwitch();
}
// Umozliwia uruchomienie dowolnego kanalu OFM wpisanego recznie w Serial.
// Akceptuje zapis OFM10 oraz skrocony zapis 10.
static void customStation(String id){
  closeStationList();
  id.trim();id.toUpperCase();if(!id.startsWith("OFM"))id="OFM"+id;
  if(id.length()<4)return;
  ownId=id;
  currentArtist="";currentTitle="";
  Serial.println("[STACJA] Własny kanał: "+id);
  requestSwitch();
}
// Uruchamia aktualnie wybrana pozycje: pobiera jej OFM ID z katalogu,
// a nastepnie przekazuje je do asynchronicznego silnika HLS.
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
// Wyswietla liste polecen dostepnych w monitorze portu szeregowego.
static void printHelp(){
 Serial.println("\n====== OPENFM HLS PLAYER ======");
 Serial.println("g 1..200 - wybierz pozycje; 1..9 - szybki wybor");
 Serial.println("Katalog 200 nazw; odtwarzane sa pozycje z potwierdzonym ID OFM. Nie wszystkie pozycje musza byc HLS.");
 Serial.println("o OFM10 - dowolny identyfikator OFM");
 Serial.println("n/p - nastepny/poprzedni kanal");
 Serial.println("meta wykonawca|tytul - PODGLAD TESTOWY metadanych (nie dane OpenFM)");
 Serial.println("s - stop, r - start, + / - - glosnosc, m - pamiec, h - pomoc");
 Serial.println("Pilot: GORA/DOL = lista; OK = wybor; LEWO/PRAWO = natychmiastowa zmiana");
 Serial.println("Na liscie: STOP = zamknij; brak ruchu przez 12 s = powrot.");
 Serial.println("VOL+/VOL-, MUTE, STOP i FF+/- jak dotychczas.");
 Serial.println("Nie wypisujemy adresow z tokenami do Serial.");
 Serial.println("================================\n");
}
// Parser polecen tekstowych. Wywolanie nastepuje po odebraniu znaku CR/LF.
// Polecenia stacji trafiaja do selectStation(), a glosnosc do OpenFmPlayer.
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
// Odbiera gotowa ramke NEC przygotowana przez ISR i mapuje kod przycisku
// na zmiane stacji, glosnosci, STOP, restart lub MUTE.
static void handleRemote(){
 if(!irReady)return;
 noInterrupts();uint32_t ir=pendingIr;irReady=false;interrupts();
 uint8_t address=ir&255,invAddr=(ir>>8)&255,key=(ir>>16)&255,invKey=(ir>>24)&255; // Bajty adresu/komendy NEC.
 if((uint8_t)(key^invKey)!=0xff) {Serial.println("[IR] Niepoprawna ramka NEC");return;}
 // Adres NEC bywa rozszerzony; w pilocie rozpoznajemy kod komendy.
 if(millis()-lastIr<120)return;lastIr=millis();
 Serial.printf("[IR] addr=0x%02X addr2=0x%02X cmd=0x%02X\n",address,invAddr,key);
 switch(key){
  case 0x26: selectStation(station+1); break; // PRAWO: od razu nastepna stacja.
  case 0x27: selectStation(station-1); break; // LEWO: od razu poprzednia stacja.
  case 0x22: scrollStationList(+1); break;  // DOL: nastepna pozycja na liscie.
  case 0x30: scrollStationList(-1); break;  // GORA: poprzednia pozycja na liscie.
  case 0x28: volume=min(21,volume+1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case 0x24: volume=max(0,volume-1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case 0x33: // STOP: zamyka liste; poza lista zachowuje dotychczasowy STOP audio.
    if(stationListActive) closeStationList();
    else {pendingPlay=false;pendingStop=OpenFmPlayer::busy();OpenFmPlayer::stop();}
    break;
  case 0x25: // OK: wybiera wskazana stacje; bez listy zachowuje restart.
    if(stationListActive) confirmStationList();
    else requestSwitch();
    break;
  case 0x29: muted=!muted;OpenFmPlayer::setVolume(muted?0:volume);break;
  case 0x18: selectStation(station+1);break; // FF+ jak dotychczas.
  case 0x19: selectStation(station-1);break; // FF- jak dotychczas.
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

// Gorny panel TFT: numer pozycji, nazwa, wykonawca, tytul, stan i glosnosc.
// Dane wykonawcy/tytulu pochodza z OpenFmMetadata::snapshot(), a nie z tokenu.
// Nazwa kanalu jest pobierana z lokalnego katalogu OpenFmCatalog.
// Wykonawca i tytul sa aktualizowane z parsera ID3v2 w module OpenFmMetadata.
// Token HLS sluzy do autoryzacji playlisty; nie jest zrodlem metadanych utworu.
// Rysuje panel gorny tylko wtedy, gdy zmienil sie jego podpis (signature)
// albo wywolujacy jawnie wymusil odswiezenie. Zapobiega niepotrzebnym zapisom SPI.
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
  canvas.setTextColor(0x07EF);  // Kolor RGB565: wiosenna zielen.
  canvas.setCursor(12,10);
  canvas.printf("Open FM stacja %03d/%03d",station+1,COUNT); // stala pozycja pierwszej linii
  canvas.drawFastHLine(8,49,WIDTH-16,0x39E7);

  canvas.setTextSize(2);
  canvas.setTextColor(0xFEA0);  // Kolor RGB565: zloty.
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

  canvas.setTextColor(0xFD20);  // Kolor RGB565: pomaranczowy dla statusu.
  canvas.setCursor(12,137);
  canvas.print("Stan: ");canvas.print(status);
  canvas.setTextColor(0x07E0);  // Kolor RGB565: intensywna zielen dla glosnosci.
  canvas.setCursor(12,163);
  canvas.print("VOL: ");canvas.print(muted?0:volume);canvas.print("/21");
  pushCanvasRegion(0,0,WIDTH,188);
}

// Rysuje parametry odebrane z silnika audio: bitrate MPEG-TS, sample rate AAC,
// rozdzielczosc PCM, liczbe kanalow oraz liczniki segmentow i bajtow ostatniego TS.
// Dane pobierane sa przez interfejs OpenFmPlayer; ekran nie ingeruje w dekoder.
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
  canvas.fillRect(0, 260, 245, 60, 0x0000);                   // LEWA część dolnego paska czarna.
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
  pushCanvasRegion(0, 260, 245, 60);                          // Tylko liczniki u dołu.
}

// ============================================================================
// ZEGAR - DOSLOWNIE TA SAMA CZCIONKA I BAZA PISANIA CO W ORYGINALE.
// Pozycja bazowa jest STALA: 245,310. Nie wyrownujemy do prawej strony!
// Nie ma tez awaryjnej zamiany na zwykla czcionke GFX przy szerszej cyfrze.
// Kadr zegara jest wysylany przez SPI 40 MHz tylko przy zmianie sekundy.
// ============================================================================
// Rysuje zegar z synchronizowanego czasu systemowego (NTP). Pozycja bazowa
// czcionki DS_DIGII35pt7b jest stala, aby szerokosc cyfr nie powodowala skakania.
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
  canvas.setTextColor(0x07FF);  // Kolor RGB565: cyjan dla zegara.
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
// Pobiera atomowy snapshot metadanych z modulu OpenFmMetadata.
// Numer wersji pozwala odswiezyc ekran tylko wtedy, gdy zmienil sie tag ID3.
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

// Nieblokujaca obsluga obrazu: metadane, panel tekstowy, parametry audio i zegar.
// Zegar jest sprawdzany co 100 ms, ale wysylany na TFT dopiero po zmianie sekundy.
static void screenLoop() {
  updateOpenFmMetadata();
  if (stationListActive) {
    // Tak jak w starym radiu: po 12 s bezczynnosci przywracamy ekran grania.
    if (millis() - stationListLastAction >= STATION_LIST_TIMEOUT_MS)
      closeStationList();
    else { drawStationList(); return; }
  }
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

// ---------------------------------------------------------------------------
// SETUP - jednorazowa inicjalizacja urzadzenia po wlaczeniu lub resecie.
// Kolejnosc: Serial -> TFT -> IR -> Wi-Fi -> NTP -> wybor pierwszej stacji.
// ---------------------------------------------------------------------------
void setup(){
 Serial.begin(115200);delay(400);                         // Uruchomienie diagnostyki Serial.
 Serial.printf("[BOOT] reset=%d heap=%u psram=%u\n",(int)esp_reset_reason(),(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram()); // Stan pamieci i przyczyna resetu.
 tftBegin();                                             // Inicjalizacja wyswietlacza i canvasu.
 pinMode(PIN_IR,INPUT_PULLUP);                           // Odbiornik IR w stanie spoczynkowym HIGH.
 lastTransition=micros();attachInterrupt(digitalPinToInterrupt(PIN_IR),irChange,CHANGE); // Start dekodowania NEC.
 WiFi.mode(WIFI_STA);                                   // ESP32 pracuje jako klient sieci (Station).
 WiFiManager wm;                                       // Obiekt zarzadzajacy portalem konfiguracji Wi-Fi.
 wm.setConfigPortalTimeout(150);                       // Portal konfiguracyjny zamyka sie po 150 s.
 if(!wm.autoConnect("ESP32S3-OpenFM-TEST")){            // Laczy z zapisana siecia lub uruchamia portal.
    Serial.println("[WiFi] Nie polaczono. Restart za 3s.");delay(3000);ESP.restart();
 }
 Serial.printf("[WiFi] IP=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());
 // Polska strefa czasowa, automatyczna zmiana CET/CEST jak w glownym radiu.
 configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.google.com"); // Polska strefa CET/CEST + synchronizacja NTP.
 printHelp();
 Serial.println("[AUTO] Wi-Fi gotowe. Uruchamiam pierwszą stację listy.");
 selectStation(0);                                     // Automatyczny start od pierwszej pozycji katalogu.
 // Jednorazowe wyczyszczenie całości usuwa także pozostałości z poprzedniego
 // programu oraz niepożądane szare tło dolnego paska.
 canvas.fillScreen(0x0000);                             // Czyszczenie calego bufora RGB565.
 drawTft();                                             // Jednorazowe wyslanie czarnego ekranu do TFT.
 screenDirty = true;                                   // Wymus pierwsze narysowanie paneli.
 screenLoop();
}
// ---------------------------------------------------------------------------
// LOOP - glowna petla sterujaca. Nie wykonuje tu pobierania HLS ani dekodowania;
// te zadania pracuja w module OpenFmPlayer na osobnych zadaniach FreeRTOS.
// Petla odbiera Serial/pilot, realizuje odlozone przelaczenie i odswieza ekran.
// ---------------------------------------------------------------------------
void loop(){
 while(Serial.available()){                              // Odbior znakow komendy bez blokowania petli.
   char c=Serial.read();
   if(c=='\n'||c=='\r') {if(serialLine.length()){commandLine(serialLine);serialLine="";}}
   else if(serialLine.length()<350)serialLine+=c;
 }
 handleRemote();                                       // Obsluga kompletnej ramki pilota NEC.
 if(pendingPlay && !OpenFmPlayer::busy()){              // Nowa stacja dopiero po zakonczeniu poprzedniej sesji.
   pendingPlay=false;pendingStop=false;playCurrent();
 }
 if(pendingStop && !OpenFmPlayer::busy())pendingStop=false; // Zwolnienie flagi po zakonczeniu STOP.
 // Gdy HTTPS faktycznie utknie, nie podmieniamy zasobow na sile.
 // Serial wyjasni dlaczego pilot czeka, a kolejne strzalki wybieraja ostatni kanal.
 if(pendingPlay && OpenFmPlayer::busy() && millis()-switchRequestedAt>6500 &&
    millis()-lastStopReport>5000){
   lastStopReport=millis();
   Serial.printf("[STEROWANIE] Oczekiwanie na STOP %lu ms; %s\n",
       (unsigned long)(millis()-switchRequestedAt),OpenFmPlayer::state().c_str());
 }
 screenLoop();                                         // Aktualizacja paneli i zegara bez pelnego odswiezania.
 delay(3);                                              // Krotkie oddanie czasu innym zadaniom FreeRTOS.
}
