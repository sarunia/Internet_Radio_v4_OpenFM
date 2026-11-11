/*
 * ============================================================================
 *  ESP32-S3 OpenFM — moduł odczytu metadanych ID3 ze strumienia MPEG-TS / HLS
 *  Plik: OpenFmMetadata.cpp
 * ============================================================================
 *
 *  ZADANIE MODUŁU
 *  - Odbiera kolejne porcje bajtów segmentów MPEG-TS pobieranych przez odtwarzacz.
 *  - Rozpoznaje 188-bajtowe pakiety TS oraz tablice PAT i PMT.
 *  - Odczytuje z PMT numer PID strumienia metadanych (stream_type 0x15).
 *  - Pomija nagłówek PES i składa tag ID3, nawet gdy obejmuje wiele pakietów TS.
 *  - Interpretuje ramki TIT2 (tytuł) i TPE1 (wykonawca).
 *  - Udostępnia tekst UTF-8 ekranowi za pomocą bezpiecznego snapshotu.
 *
 *  PRZEPŁYW DANYCH
 *  OpenFmPlayer::downloadSegment() -> feed() -> parsePacket() -> parsePsi()
 *     -> feedId3() -> parseId3() -> frameText() -> publish() -> snapshot()
 *
 *  UWAGI TECHNICZNE
 *  - Metadane są niezależne od dekodowania AAC i regulacji częstotliwości I2S.
 *  - Rozmiary buforów są stałe; przekroczenie limitu kończy zbieranie tagu.
 *  - Sekcja PAT/PMT musi zmieścić się w jednym pakiecie (ograniczenie kodu).
 *  - Rozszerzony nagłówek ID3 jest obecnie pomijany.
 *  - Połączenie ID3 z nazwą stacji nie jest realizowane tutaj: nazwa pochodzi
 *    z osobnego katalogu OpenFmCatalog.
 *  - Nie wpisujemy tu wersji bibliotek, bo plik ich nie identyfikuje.
 *
 *  Dokumentacja dotyczy bieżącej implementacji; nie modyfikuje jej działania.
 * ============================================================================
 */
#include "OpenFmMetadata.h"
#include <string.h>

// Publiczna przestrzeń nazw: funkcje reset(), feed(), snapshot(), diagnostics().
// Ich deklaracje znajdują się w OpenFmMetadata.h.
namespace OpenFmMetadata {
// Anonimowa przestrzeń nazw ogranicza widoczność parserów, buforów i liczników
// do tego pliku; pozostałe moduły korzystają wyłącznie z publicznego API.
namespace {
// ============================================================================
// STAŁE, BUFORY I STAN WEWNĘTRZNY
// ============================================================================
// PID 0x1FFF oznacza w MPEG-TS pakiet pusty; używamy go jako wartości
// „nie wykryto jeszcze PID” dla tablic PMT oraz metadanych.
constexpr uint16_t INVALID_PID = 0x1FFF;
// Maksymalny rozmiar jednego składanego tagu ID3 (łącznie z nagłówkiem).
// Zapobiega nieograniczonemu zajmowaniu pamięci przez nieprawidłowe dane.
constexpr size_t ID3_LIMIT = 8192;
// Maksymalny rozmiar tablic tekstowych w BAJTACH UTF-8, a nie znakach.
// Polskie litery często zajmują więcej niż jeden bajt.
constexpr size_t TEXT_LIMIT = 160;
// Bufor pojedynczego pakietu transportowego MPEG-TS o stałej długości 188 B.
uint8_t packet[188];
// Liczba bajtów dotychczas zgromadzonych w bieżącym pakiecie TS.
size_t packetFill = 0;
// PID tablicy PMT wyznaczany po odczycie tablicy PAT.
uint16_t pmtPid = INVALID_PID;
// PID pakietów z metadanymi ID3, wykrywany w tablicy PMT.
uint16_t metaPid = INVALID_PID;
// Bufor składania tagu ID3 z danych PES, również pomiędzy pakietami TS.
uint8_t id3[ID3_LIMIT];
// Faktyczna liczba zebranych bajtów tagu ID3.
size_t id3Count = 0;
// Docelowy rozmiar tagu wyliczony z 10-bajtowego nagłówka ID3v2.
size_t id3Wanted = 0;
// Informacja, czy trwa gromadzenie kompletnego tagu ID3.
bool collecting = false;
// Liczniki diagnostyczne obowiązujące od ostatniego wywołania reset().
uint32_t packetCount = 0;
uint32_t tagCount = 0;
uint32_t metaPackets = 0, pesStarts = 0, pesBad = 0, id3Headers = 0;
// Łączna liczba bajtów części danych PES przekazanych parserowi ID3.
// Nie jest to liczba znaków tekstu ani rozmiar całego strumienia audio.
uint32_t pesPayloadBytes = 0;
// Liczba kolejnych znaków rozpoznanej sygnatury „ID3” (I, D, 3).
uint8_t syncMatch = 0;
// Włącza jednorazowy podgląd pierwszych bajtów payloadu PES w Serial.
bool firstPesPreview = false;
// Ostatnio opublikowany wykonawca, przechowywany jako łańcuch UTF-8.
char currentArtist[TEXT_LIMIT] = "";
// Ostatnio opublikowany tytuł odtwarzanego utworu.
char currentTitle[TEXT_LIMIT] = "";
// Numer wersji metadanych rośnie tylko wtedy, gdy zmieni się tekst.
// Ekran może dzięki temu pomijać niepotrzebne ponowne rysowanie.
uint32_t currentVersion = 0;
// Blokada sekcji krytycznej chroni wspólne bufory i numer wersji przed
// równoczesnym dostępem zadania pobierania HLS i zadania obsługi ekranu.
portMUX_TYPE metadataMux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief Odczytuje 32-bitową liczbę „synchsafe” z czterech bajtów.
 * @param p Wskaźnik na cztery bajty nagłówka/ramki ID3.
 * @return Wartość, w której każdy bajt przenosi 7 bitów danych.
 *
 * W ID3v2.4 rozmiary są kodowane jako 4 x 7 bitów, aby unikać wartości
 * mogących kolidować z niektórymi sekwencjami synchronizacji audio.
 */
uint32_t syncsafe(const uint8_t *p) {
  return ((uint32_t)(p[0] & 0x7f) << 21) | ((uint32_t)(p[1] & 0x7f) << 14) |
         ((uint32_t)(p[2] & 0x7f) << 7) | (uint32_t)(p[3] & 0x7f);
}
/**
 * @brief Odczytuje czterobajtową liczbę w kolejności big-endian.
 * @param p Wskaźnik na cztery kolejne bajty.
 * @return 32-bitowy rozmiar pola, używany dla ramek ID3v2.3.
 */
uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

// Unicode -> UTF-8 (tylko poprawne skalarne punkty kodowe).
/**
 * @brief Dopisuje do obiektu String punkt kodowy Unicode zakodowany jako UTF-8.
 * @param s Docelowy napis Arduino String.
 * @param cp Wartość punktu kodowego Unicode.
 *
 * Funkcja odrzuca U+0000, wartości ponad U+10FFFF oraz zakres zastępczy
 * UTF-16 (surrogates). Obsługuje sekwencje UTF-8 od 1 do 4 bajtów.
 */
void addUtf8(String& s, uint32_t cp) {
  if(cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
  if(cp < 0x80) s += char(cp);
  else if(cp < 0x800) {s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 63));}
  else if(cp < 0x10000) {s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6)&63)); s += char(0x80 | (cp&63));}
  else {s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12)&63)); s += char(0x80 | ((cp >> 6)&63)); s += char(0x80 | (cp&63));}
}

/**
 * @brief Odczytuje zawartość tekstowej ramki ID3 do napisu UTF-8.
 * @param data Dane ramki; pierwszy bajt określa kodowanie tekstu.
 * @param size Liczba dostępnych bajtów tej ramki.
 * @return Odczytany tekst po usunięciu białych znaków na brzegach.
 *
 * Obsługiwane oznaczenia kodowania ID3:
 *   0 — ISO-8859-1, zamieniane na UTF-8;
 *   1 — UTF-16 z opcjonalnym BOM;
 *   2 — UTF-16BE;
 *   3 — UTF-8.
 *
 * Limity długości ograniczają użycie pamięci na mikrokontrolerze.
 */
String frameText(const uint8_t *data, size_t size) {
  String out;
  if(size < 2) return out;
  const uint8_t encoding = data[0];
  const uint8_t *p = data + 1;
  size_t len = size - 1;
  if(encoding == 0) { // ID3 ISO-8859-1
    for(size_t i=0; i<len && out.length()<TEXT_LIMIT-5; ++i) {
      if(p[i] == 0) break;
      addUtf8(out, p[i]);
    }
  } else if(encoding == 3) { // ID3 UTF-8
    for(size_t i=0; i<len && out.length()<TEXT_LIMIT-5; ++i) {
      if(p[i] == 0) break;
      out += char(p[i]);
    }
  } else if(encoding == 1 || encoding == 2) { // UTF-16 z BOM lub BE
    bool little = false;
    if(len >= 2 && p[0]==0xFF && p[1]==0xFE) { little=true; p+=2; len-=2; }
    else if(len >= 2 && p[0]==0xFE && p[1]==0xFF) { p+=2; len-=2; }
    for(size_t i=0; i+1<len && out.length()<TEXT_LIMIT-5; i+=2) {
      uint16_t u=little ? uint16_t(p[i]) | (uint16_t(p[i+1])<<8)
                        : (uint16_t(p[i])<<8) | uint16_t(p[i+1]);
      if(u == 0) break;
      if(u>=0xD800 && u<=0xDBFF && i+3<len) {
        uint16_t v=little ? uint16_t(p[i+2]) | (uint16_t(p[i+3])<<8)
                          : (uint16_t(p[i+2])<<8) | uint16_t(p[i+3]);
        if(v>=0xDC00 && v<=0xDFFF) {
          addUtf8(out,0x10000 + ((uint32_t(u)-0xD800)<<10) + (v-0xDC00));
          i+=2; continue;
        }
      }
      addUtf8(out,u);
    }
  }
  out.trim();
  return out;
}

/**
 * @brief Udostępnia nowy zestaw wykonawca/tytuł pozostałej części radia.
 * @param artist Tekst wykonawcy, potencjalnie pusty.
 * @param title Tekst tytułu, potencjalnie pusty.
 *
 * Jeśli TPE1 jest puste, a TIT2 ma format „Wykonawca - Tytuł”, funkcja
 * próbuje podzielić taki tekst. Porównanie z poprzednimi wartościami odbywa
 * się pod blokadą; numer wersji rośnie tylko przy rzeczywistej zmianie.
 */
void publish(String artist, String title) {
  artist.trim(); title.trim();
  if(title.isEmpty() && artist.isEmpty()) return;
  // OpenFM moze umieszczac "Wykonawca - Tytul" w samym TIT2.
  if(artist.isEmpty() && title.length()) {
    int sep=title.indexOf(" - ");
    if(sep>0 && sep<70 && sep+3<(int)title.length()) {
      artist=title.substring(0,sep);
      title=title.substring(sep+3);
    }
  }
  char nextArtist[TEXT_LIMIT] = {}, nextTitle[TEXT_LIMIT] = {};
  artist.toCharArray(nextArtist,sizeof(nextArtist));
  title.toCharArray(nextTitle,sizeof(nextTitle));
  bool changed=false;
  portENTER_CRITICAL(&metadataMux);
  if(strcmp(nextArtist,currentArtist)!=0 || strcmp(nextTitle,currentTitle)!=0) {
    memcpy(currentArtist,nextArtist,sizeof(currentArtist));
    memcpy(currentTitle,nextTitle,sizeof(currentTitle));
    ++currentVersion;
    changed=true;
  }
  portEXIT_CRITICAL(&metadataMux);
  if(changed) Serial.printf("[ID3] Nowe metadane: wykonawca=%s | tytul=%s\n",nextArtist,nextTitle);
}

/**
 * @brief Analizuje kompletny tag ID3v2.3 lub ID3v2.4 zgromadzony w buforze.
 *
 * Wymaga sygnatury „ID3” i pełnego nagłówka o rozmiarze co najmniej 10 B.
 * Przechodzi przez ramki, kontroluje poprawność ich długości, a następnie
 * przetwarza tylko TIT2 (tytuł) i TPE1 (wykonawca). Inne ramki pomija.
 * Tag z rozszerzonym nagłówkiem nie jest aktualnie interpretowany.
 */
void parseId3() {
  if(id3Count < 10 || memcmp(id3,"ID3",3)!=0) return;
  const uint8_t version=id3[3];
  if(version!=3 && version!=4) {
    Serial.printf("[ID3] Nieobslugiwana wersja ID3v2.%u\n",version); return;
  }
  size_t end=10 + syncsafe(id3+6);
  if(end>id3Count) return;
  if(id3[5]&0x40) { // extended header; bezpiecznie pomin gdy obecny
    Serial.println("[ID3] Rozszerzony naglowek: pomijam tag"); return;
  }
  String artist,title;
  for(size_t off=10;off+10<=end;) {
    const uint8_t* f=id3+off;
    if(f[0]==0) break; // padding
    bool valid=true;
    for(int i=0;i<4;i++) if(!((f[i]>='A'&&f[i]<='Z')||(f[i]>='0'&&f[i]<='9')))valid=false;
    if(!valid) break;
    const size_t len=version==4 ? syncsafe(f+4) : be32(f+4);
    if(len==0 || len>end-off-10) break;
    if(memcmp(f,"TIT2",4)==0) title=frameText(f+10,len);
    if(memcmp(f,"TPE1",4)==0) artist=frameText(f+10,len);
    off+=10+len;
  }
  ++tagCount;
  publish(artist,title);
}

// Szukamy sygnatury ID3 takze wtedy, gdy nie jest pierwszym bajtem PES.
// Utrzymujemy stan miedzy kolejnymi pakietami TS, bez zmiany toru AAC.
/**
 * @brief Składa tagi ID3 z kolejnych fragmentów danych strumienia PES.
 * @param p Początek aktualnego fragmentu danych.
 * @param n Rozmiar aktualnego fragmentu w bajtach.
 * @param start Czy rozpoczyna się nowa jednostka PES.
 *
 * Parser wyszukuje sygnaturę „ID3” również wewnątrz payloadu PES, a nie
 * tylko na jego początku. Stan wyszukiwania utrzymuje pomiędzy fragmentami.
 * Po zebraniu deklarowanej długości wywołuje parseId3().
 */
void feedId3(const uint8_t *p,size_t n,bool start) {
  if(start) {id3Count=0;id3Wanted=0;collecting=false;syncMatch=0;}
  for(size_t i=0;i<n;i++) {
    const uint8_t c=p[i];
    if(!collecting) {
      if(c == (const uint8_t)"ID3"[syncMatch]) ++syncMatch;
      else syncMatch=(c=='I')?1:0;
      if(syncMatch==3) {
        collecting=true; id3Count=3; id3Wanted=0;
        id3[0]='I';id3[1]='D';id3[2]='3';
        syncMatch=0; ++id3Headers;
        Serial.printf("[ID3] Sygnatura ID3 wykryta (nr=%lu)\n",(unsigned long)id3Headers);
      }
      continue;
    }
    if(id3Count>=ID3_LIMIT) {collecting=false;id3Count=0;continue;}
    id3[id3Count++]=c;
    if(id3Count==10) {
      id3Wanted=10+syncsafe(id3+6);
      if(id3Wanted<10 || id3Wanted>ID3_LIMIT) {
        Serial.printf("[ID3] Nieprawidlowy rozmiar tagu: %lu\n",(unsigned long)id3Wanted);
        collecting=false;id3Count=0;continue;
      }
    }
    if(id3Wanted && id3Count>=id3Wanted) {
      parseId3();collecting=false;id3Count=0;id3Wanted=0;
    }
  }
}

/**
 * @brief Odczytuje tablice sygnalizacyjne MPEG-TS: PAT i PMT.
 * @param pid PID bieżącego pakietu transportowego.
 * @param p Dane payloadu po nagłówku TS.
 * @param n Liczba bajtów payloadu.
 * @param start Flaga początku jednostki payloadu (PUSI).
 *
 * PAT (PID 0) wskazuje PID tablicy PMT. PMT wskazuje strumienie programu,
 * m.in. metadane ID3 oznaczone typem stream_type = 0x15.
 * Uwaga: implementacja zakłada jednopakietową sekcję PAT/PMT.
 */
void parsePsi(uint16_t pid,const uint8_t* p,size_t n,bool start) {
  if(!start || n<4) return;
  const size_t pointer=p[0];
  if(pointer+1>=n) return;
  p+=pointer+1; n-=pointer+1;
  if(n<12) return;
  size_t sectionLen=((p[1]&0x0F)<<8)|p[2];
  if(sectionLen+3>n) return; // jednopakietowe PAT/PMT
  if(pid==0 && p[0]==0x00) { // PAT
    size_t finish=sectionLen+3-4;
    for(size_t j=8;j+4<=finish;j+=4) {
      uint16_t program=(uint16_t(p[j])<<8)|p[j+1];
      if(program!=0) {
        uint16_t found=((uint16_t(p[j+2])&0x1F)<<8)|p[j+3];
        if(found!=pmtPid) {pmtPid=found; Serial.printf("[ID3] PAT: PMT PID=0x%X\n",pmtPid);}
        return;
      }
    }
  }
  if(pid==pmtPid && p[0]==0x02) { // PMT
    size_t sectionEnd=sectionLen+3-4;
    size_t progInfoLen=((p[10]&0x0F)<<8)|p[11];
    size_t offset=12+progInfoLen;
    while(offset+5<=sectionEnd) {
      uint8_t streamType=p[offset];
      uint16_t streamPid=((uint16_t(p[offset+1])&0x1F)<<8)|p[offset+2];
      size_t esInfoLen=((p[offset+3]&0x0F)<<8)|p[offset+4];
      if(streamType==0x15 && streamPid!=metaPid) { // ID3 metadata w MPEG-TS (HLS)
        metaPid=streamPid;
        Serial.printf("[ID3] PMT: metadata PID=0x%X (stream_type=0x15)\n",metaPid);
      }
      offset+=5+esInfoLen;
    }
  }
}

/**
 * @brief Przetwarza jeden kompletny pakiet MPEG-TS (188 bajtów).
 * @param ts Wskaźnik na początek pakietu zaczynającego się bajtem 0x47.
 *
 * Weryfikuje synchronizację i nagłówek, pomija pole adaptacyjne oraz kieruje
 * payload do parsera PAT/PMT lub parsera metadanych. Dla PID metadanych
 * usuwa nagłówek PES i przekazuje właściwe dane do feedId3().
 * Pakiety audio nie są modyfikowane ani dekodowane przez ten moduł.
 */
void parsePacket(const uint8_t* ts) {
  if(ts[0]!=0x47 || (ts[1]&0x80)) return;
  uint16_t pid=((uint16_t(ts[1])&0x1F)<<8)|ts[2];
  const bool start=(ts[1]&0x40)!=0;
  const uint8_t control=(ts[3]>>4)&3;
  if(control==0 || control==2) return;
  size_t offset=4;
  if(control==3) {
    size_t len=ts[offset]; offset+=1+len;
    if(offset>188) return;
  }
  if(offset>=188) return;
  const uint8_t *p=ts+offset;
  size_t count=188-offset;
  if(pid==0 || pid==pmtPid) {parsePsi(pid,p,count,start);return;}
  if(pid!=metaPid) return;
  ++metaPackets;
  if(start) {
    ++pesStarts;
    // PES: 00 00 01 stream_id PES_packet_length flags header_data_length
    if(count<9 || p[0]!=0 || p[1]!=0 || p[2]!=1) {
      ++pesBad;
      if(pesBad<=3) Serial.printf("[ID3] Nieoczekiwany poczatek PES: %02X %02X %02X %02X\n", p[0],p[1],p[2],p[3]);
      collecting=false;return;
    }
    size_t header=9+p[8];
    if(header>count) {++pesBad;collecting=false;return;}
    if(!firstPesPreview) {
      firstPesPreview=true;
      Serial.printf("[ID3] PES stream_id=0x%02X header=%u payload=%u pierwsze:",p[3],(unsigned)header,(unsigned)(count-header));
      for(size_t k=header;k<count && k<header+16;++k) Serial.printf(" %02X",p[k]);
      Serial.println();
    }
    pesPayloadBytes+=(uint32_t)(count-header);
    feedId3(p+header,count-header,true);
  } else {
    pesPayloadBytes+=(uint32_t)count;
    feedId3(p,count,false);
  }
}
}

/**
 * @brief Czyści stan parsera przed uruchomieniem nowej stacji OpenFM.
 *
 * Zeruje identyfikatory PID, liczniki diagnostyczne oraz częściowo zebrane
 * pakiety/tagi. Opublikowane metadane są czyszczone pod blokadą, a wersja
 * zwiększana, aby ekran usunął wykonawcę i tytuł poprzedniej stacji.
 */
void reset() {
  packetFill=0;pmtPid=INVALID_PID;metaPid=INVALID_PID;
  id3Count=0;id3Wanted=0;collecting=false;packetCount=tagCount=0;
  metaPackets=pesStarts=pesBad=id3Headers=pesPayloadBytes=0;
  syncMatch=0;firstPesPreview=false;
  portENTER_CRITICAL(&metadataMux);
  currentArtist[0]=0;currentTitle[0]=0;++currentVersion;
  portEXIT_CRITICAL(&metadataMux);
}

/**
 * @brief Dopisuje dowolny fragment strumienia MPEG-TS do parsera.
 * @param data Wskaźnik na pobrane bajty segmentu HLS.
 * @param size Liczba bajtów; fragment nie musi kończyć się na granicy TS.
 *
 * Funkcja buforuje niepełny pakiet i wywołuje parsePacket() po uzyskaniu
 * dokładnie 188 bajtów. Przy pustym buforze szuka bajtu synchronizacji 0x47.
 */
void feed(const uint8_t* data,size_t size) {
  if(!data || !size) return;
  for(size_t i=0;i<size;i++) {
    if(packetFill==0 && data[i]!=0x47) continue; // poczatek synchronizacji TS
    packet[packetFill++]=data[i];
    if(packetFill==188) {
      parsePacket(packet);
      packetFill=0;
      ++packetCount;
    }
  }
}

/**
 * @brief Kopiuje ostatnie opublikowane metadane dla interfejsu użytkownika.
 * @param artist [out] Nazwa wykonawcy w UTF-8.
 * @param title [out] Tytuł utworu w UTF-8.
 * @param version [out] Numer ostatniej zmiany metadanych.
 * @return true, jeśli wykonawca lub tytuł nie jest pusty.
 *
 * Krótka sekcja krytyczna chroni odczyt przed równoczesnym zapisem.
 */
bool snapshot(String& artist,String& title,uint32_t& version) {
  char a[TEXT_LIMIT],t[TEXT_LIMIT];
  portENTER_CRITICAL(&metadataMux);
  memcpy(a,currentArtist,sizeof(a)); memcpy(t,currentTitle,sizeof(t));
  version=currentVersion;
  portEXIT_CRITICAL(&metadataMux);
  artist=String(a);title=String(t);
  return a[0] || t[0];
}
/**
 * @brief Wypisuje do portu Serial liczniki diagnostyczne parsera.
 *
 * TS           - liczba kompletnych pakietów transportowych;
 * PMT          - PID tablicy PMT;
 * PID          - PID metadanych ID3;
 * pakiety_meta - liczba pakietów TS należących do PID metadanych;
 * PES          - liczba rozpoczętych jednostek PES;
 * PES_bad      - liczba nieprawidłowych początków PES;
 * bajty_PES    - liczba bajtów payloadu PES przekazanych parserowi;
 * ID3_headers  - liczba wykrytych sygnatur/nagłówków ID3;
 * tagi         - liczba przetworzonych kompletnych tagów ID3.
 */
void diagnostics() {
  Serial.printf("[ID3] TS=%lu PMT=0x%X PID=0x%X pakiety_meta=%lu PES=%lu PES_bad=%lu bajty_PES=%lu ID3_headers=%lu tagi=%lu\n",
    (unsigned long)packetCount,pmtPid,metaPid,(unsigned long)metaPackets,
    (unsigned long)pesStarts,(unsigned long)pesBad,(unsigned long)pesPayloadBytes,
    (unsigned long)id3Headers,(unsigned long)tagCount);
}
}
