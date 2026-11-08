// Nazwy oraz slugi z publicznego katalogu GETRADIO (2026-11-08).
// Rozpoznawanie OFM: HTTP 302 Location -> /OFMxxx/...; sprawdzany przy wyborze.
// Uwagi: GETRADIO zawiera tez stacje partnerskie, ktore nie zawsze sa OpenFM HLS.
#include "OpenFmCatalog.h"
#include <WiFi.h>  // WiFi.status() i WL_CONNECTED
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
namespace OpenFmCatalog {
static const Entry entries[] = {
  {"90s Dance", "90s-dance", 168},
  {"Impreza", "impreza", 57},
  {"Trap PL", "trap-pl", 0},
  {"Weekend Chill", "weekend-chill", 0},
  {"Po Polsku 60s & 70s", "po-polsku-60-70", 0},
  {"90s Chill", "90s-chill", 0},
  {"1001 Hits", "1001-hits", 0},
  {"Biesiada Slaska", "biesiada-slaska", 0},
  {"100% Hits", "100-hits", 0},
  {"Vixa", "vixa", 0},
  {"Top 2026 Hits", "top-hits", 0},
  {"Impreza PL", "impreza-pl", 0},
  {"Weekend Hits", "weekend-hits", 0},
  {"90s Hits", "90s-hits", 0},
  {"To Jest Hip-Hop", "to-jest-hip-hop", 0},
  {"500 Hip-Hop Hits", "500-hip-hop-hits", 0},
  {"Tego Sie Slucha", "popularne", 0},
  {"Na Pelnej!", "edm-anthems", 0},
  {"Disco Polo Classic", "disco-polo-classic", 0},
  {"Hity lat 2000.", "00s-hits", 0},
  {"Dance", "dance", 0},
  {"W Domu", "w-domu", 0},
  {"Po Polsku", "po-polsku", 0},
  {"10s Hits", "10s-hits", 0},
  {"Rock Na Caly Dzien", "rock-na-caly-dzien", 0},
  {"Tylko Polskie Przeboje", "tylko-polskie-przeboje", 0},
  {"Drum'n'Bass", "drumnbass", 0},
  {"Hip-Hop Freszzz", "hip-hop-freszzz", 0},
  {"80s Rock", "80s-rock", 0},
  {"Hip-Hop PL", "hip-hop-pl", 0},
  {"Trening", "trening", 0},
  {"Ibiza Party", "house", 0},
  {"Energia Na Trase", "do-auta-club", 0},
  {"Koncentracja", "koncentracja", 0},
  {"Biesiada", "biesiada", 0},
  {"Po Polsku 80s", "po-polsku-80", 0},
  {"Latino Hits", "latino-hits", 0},
  {"500 Party Hits", "500-party-hits", 0},
  {"Beats & Bass", "beats-bass", 0},
  {"Hip-Hop Klasyk", "hip-hop-klasyk", 0},
  {"Swieze Dzwieki", "alt-freszzz", 0},
  {"Jazz Cafe", "jazz", 0},
  {"Hity Disco Polo", "hity-disco-polo", 0},
  {"Radiowe Hity", "radiowe-hity", 0},
  {"Radio ESKA", "radio-eska", 0},
  {"Szanty", "szanty", 0},
  {"Praca Rock", "praca-rock", 0},
  {"VOX FM", "vox-fm", 0},
  {"Hip-Hop Motywacyjny", "muzyka-motywacyjna", 0},
  {"Hity Na Topie", "hity-na-topie", 0},
  {"80s Hits", "80s-hits", 0},
  {"Ballady Wszech Czasow", "ballady-wszech-czasow", 0},
  {"Koledy", "koledy", 0},
  {"Italo Disco", "italo-disco", 0},
  {"Disco Polo", "disco-polo", 0},
  {"Top Wszech Czasow - Swiat", "top-wszech-czasow-swiat", 0},
  {"Relaks", "relaks", 0},
  {"Kraina Lagodnosci", "kraina-lagodnosci", 0},
  {"Ladies Party", "ladies-party", 0},
  {"Polskie Reggae", "polskie-reggae", 0},
  {"Po Polsku 80s & 90s", "po-polsku-80s-90s", 0},
  {"500 Electronic Hits", "500-electronic-hits", 0},
  {"500 R'n'B Hits", "500-rnb-hits", 0},
  {"Smutno...", "smutne-piosenki", 0},
  {"Top Wszech Czasow - Rock PL", "top-wszech-czasow-rock-pl", 0},
  {"Smooth Jazz", "smooth-jazz", 0},
  {"Po Polsku 90s", "po-polsku-90", 0},
  {"Classic Hits", "classic-hits", 0},
  {"Piano Chill", "piano-chill", 0},
  {"Retro Cafe", "retro-cafe", 0},
  {"Pozytywki", "pozytywki", 0},
  {"500 Reggae Hits", "500-reggae-hits", 0},
  {"Kolysanki", "kolysanki", 0},
  {"Relaks Przy Klasyce", "muzyka-klasyczna", 0},
  {"Work Vibes", "work-vibes", 0},
  {"Top Wszech Czasow - Rock", "top-wszech-czasow-rock", 0},
  {"Muzyka Filmowa", "muzyka-filmowa", 0},
  {"Wesele", "wesele", 0},
  {"Love", "love", 0},
  {"Dobry Wieczor", "dobry-wieczor", 0},
  {"Chill Na Caly Dzien", "chill-na-caly-dzien", 0},
  {"Top Wszech Czasow - Hip-Hop PL", "top-wszech-czasow-hip-hop-pl", 0},
  {"Rock Na Trase", "do-auta-rock", 0},
  {"500 Alternative Hits", "500-alternative-hits", 0},
  {"Polskie Ballady", "polskie-ballady", 0},
  {"Alt PL", "alt-pl", 0},
  {"Radio Zlote Przeboje", "radio-zlote-przeboje", 0},
  {"Punk Rock", "punk-rock", 0},
  {"Happy", "happy", 0},
  {"Hity Na Caly Dzien", "hity-na-caly-dzien", 0},
  {"90s Rock", "90s-rock", 0},
  {"Polska Rocks!", "polska-rocks", 0},
  {"We Dwoje", "we-dwoje", 0},
  {"Classic Party", "classic-party", 0},
  {"Soul & R'n'B Cafe", "sexy-soul-rnb", 0},
  {"80s Chill", "80s-chill", 0},
  {"Disco Polo Nowosci 2026", "disco-polo-freszzz", 0},
  {"Chill & Lofi Beats", "chill-lofi-beats", 0},
  {"Bajki", "bajki", 0},
  {"Hip-Hop Ulica", "hip-hop-ulica", 0},
  {"Muzyka Do Snu", "muzyka-do-snu", 0},
  {"Hity: Jesien 2026", "summerfreszzz", 0},
  {"Alt Cafe", "alt-cafe", 0},
  {"Classic Metal", "classic-metal", 0},
  {"Piosenki dla dzieci", "piosenki-dla-dzieci", 0},
  {"Rocks!", "rocks", 0},
  {"Top Wszech Czasow - Polska", "top-wszech-czasow-polska", 0},
  {"Meloradio", "meloradio", 0},
  {"Hity lat 70 80 90", "hity-lat-70-80-90", 0},
  {"Rock Ballady", "rock-ballady", 0},
  {"Dobranoc", "dobranoc", 0},
  {"Alt Classic", "alt-classic", 0},
  {"Classic Rock", "classic-rock", 0},
  {"Ciezkie Brzmienia", "ciezkie-brzmienia", 0},
  {"Trance", "trance", 0},
  {"American Rock", "american-rock", 0},
  {"Polski Rock", "polski-rock", 0},
  {"Po Jednej Nutce", "po-jednej-nutce", 0},
  {"Na Spokojnie", "acoustic-vibes", 0},
  {"500 Heavy Hits", "500-heavy-hits", 0},
  {"Dzien Dobry!", "dzien-dobry", 0},
  {"Polo & Dance", "polo-dance", 0},
  {"Najwieksze Przeboje XX w.", "najwieksze-przeboje-xx-w", 0},
  {"Spokojne Po Polsku", "spokojne-po-polsku", 0},
  {"Polskie Na Caly Dzien", "polskie-na-caly-dzien", 0},
  {"1001 Rock Hits", "500-rock-hits", 0},
  {"80s & 90s", "80s-90s-hits", 0},
  {"Szum", "szum-do-snu", 0},
  {"Slonce!", "slonce", 0},
  {"Przeboje Na Caly Dzien", "przeboje-na-caly-dzien", 0},
  {"Hity Bez Konca", "hity-bez-konca", 0},
  {"Pop Na Caly Dzien", "pop-na-caly-dzien", 0},
  {"Legendy Bluesa", "legendy-bluesa", 0},
  {"Giganci Rocka", "giganci-rocka", 0},
  {"Ladies Cafe", "ladies-cafe", 0},
  {"Crema Cafe", "crema-cafe", 0},
  {"Praca Chill", "praca-chill", 0},
  {"The Best Of Prog Rock", "the-best-of-prog-rock", 0},
  {"Polski Rock Classic", "polski-rock-classic", 0},
  {"Praca", "praca", 0},
  {"Chillout", "chillout", 0},
  {"Spokojne Hity", "spokojne-hity", 0},
  {"Radio ZET", "radio-zet", 0},
  {"Muzyka Na Dobry Dzien", "muzyka-na-dobry-dzien", 0},
  {"RMF24", "rmf24", 0},
  {"Fajne Radio", "fajne-radio", 0},
  {"Radio Tok FM", "radio-tok-fm", 0},
  {"Radio Centrum (Lublin)", "radio-centrum-lublin", 0},
  {"Polskie Radio Kierowcow", "polskie-radio-kierowcow", 0},
  {"Radio Nowy Swiat", "radio-nowy-swiat", 0},
  {"Antyradio", "antyradio", 0},
  {"Radio Fest", "radio-fest", 0},
  {"Festiwalowy Vibe", "festiwalowe-lato-2026", 0},
  {"Radio Kampus", "radio-kampus", 0},
  {"Radio Kolor", "radio-kolor", 0},
  {"Czworka", "czworka-polskie-radio", 0},
  {"Radio 357", "radio-357", 0},
  {"Radio Afera", "radio-afera", 0},
  {"Polskie Radio 24", "polskie-radio-24", 0},
  {"Radio Pogoda", "radio-pogoda", 0},
  {"Polskie Radio Dzieciom", "polskie-radio-dzieciom", 0},
  {"Akademickie Radio Luz", "akademickie-radio-luz", 0},
  {"Chillizet", "chillizet", 0},
  {"Polskie Radio dla Zagranicy", "polskie-radio-dla-zagranicy", 0},
  {"ESKA2", "eska2", 0},
  {"House", "house-radio", 0},
  {"bounce", "bounce", 0},
  {"RMF Classic", "rmf-classic", 0},
  {"Polish Radio London", "polish-radio-london", 0},
  {"Deszcz", "deszcz", 0},
  {"Radio Rockserwis FM", "radio-rockserwis-fm", 0},
  {"Radio Piekary", "radio-piekary", 0},
  {"Polski Rap", "polski-rap", 0},
  {"Radio Chopin", "radio-chopin", 0},
  {"Polskie Radio dla Ukrainy", "polskie-radio-dla-ukrainy", 0},
  {"Radio Akadera", "radio-akadera", 0},
  {"Mistrzowie Gitary", "mistrzowie-gitary", 0},
  {"EskaRock", "eskarock", 0},
  {"Radio Wroclaw Kultura", "radiowroclawkultura", 0},
  {"Idzie burza", "idzie-burza", 0},
  {"Spiew ptakow", "spiew-ptakow", 0},
  {"Radio Krakow Kultura", "radio-krakow-kultura", 0},
  {"Odglosy natury", "odglosy-natury-do-snu", 0},
  {"Ognisko", "ognisko", 0},
  {"Radio Krakow", "radio-krakow", 0},
  {"Radio RAM", "radioram", 0},
  {"Szum morza", "szum-morza", 0},
  {"Radio Wroclaw", "radiowroclaw", 0},
  {"80s Na Caly Dzien", "80s-na-caly-dzien", 0},
  {"90s Na Caly Dzien", "90s-na-caly-dzien", 0},
  {"Off Radio Krakow", "off-radio-krakow", 0},
  {"Nowosci", "nowosci", 0},
  {"Na Luzie", "na-luzie", 0},
  {"Kocyk", "kocyk", 0},
  {"Radio Kiss FM", "radio-kiss-fm", 0},
  {"Pelny Odlot", "pelny-odlot", 0},
  {"Rap Classics", "rap-classics", 0},
  {"Hity Na Trase", "hity-na-trase", 0},
  {"Klasyki Na Trase", "klasyki-na-trase", 0},
  {"W Droge!", "w-droge", 0},
};
static int16_t remembered[200] = {};
static_assert(sizeof(entries)/sizeof(entries[0]) == 200, "Exactly 200 entries required");
int count(){ return 200; }
const Entry& entry(int slot){ return entries[(slot>=0 && slot<200)?slot:0]; }
String knownId(int slot){
  if(slot<0||slot>=count())return "";
  const int v=remembered[slot]?remembered[slot]:entries[slot].fixedId;
  return v>0?"OFM"+String(v):"";
}
String resolveId(int slot){
  if(slot<0||slot>=count())return "";
  String stored=knownId(slot); if(stored.length())return stored;
  if(WiFi.status()!=WL_CONNECTED) {Serial.println("[KATALOG] Brak WiFi");return "";}
  const String url=String("https://getradio.reconv.pl/openfm?s=")+entries[slot].slug;
  WiFiClientSecure client; client.setInsecure(); client.setTimeout(6500);
  HTTPClient http; http.setTimeout(6500); http.setConnectTimeout(4500);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* keys[]={"Location"}; http.collectHeaders(keys,1);
  if(!http.begin(client,url)){Serial.println("[KATALOG] Nie mozna otworzyc katalogu");return "";}
  http.addHeader("User-Agent","ESP32S3-OpenFM/200");
  const int code=http.GET();
  String target=http.header("Location");
  http.end();
  // NIE wypisujemy docelowego URL: moglby zawierac token.
  if(code<300||code>399||target.length()==0){
    Serial.printf("[KATALOG] %s: HTTP=%d, brak przekierowania OFM\n",entries[slot].slug,code);
    return "";
  }
  // Akceptuj tylko adresy kierujace na CDN OpenFM, nie dowolny URL.
  if(!target.startsWith("https://stream-cdn-") || target.indexOf(".open.fm/")<0){
    Serial.printf("[KATALOG] %s: to nie strumien OpenFM HLS, pomijam\n",entries[slot].slug);
    return "";
  }
  int p=target.indexOf("/OFM"); if(p<0)return ""; p+=4;
  int v=0,nd=0;
  while(p<(int)target.length() && target[p]>='0' && target[p]<='9' && nd<5) { v=v*10+(target[p++]-'0'); ++nd; }
  if(nd==0||v<1||v>32767||p>=(int)target.length()||target[p]!='/')return "";
  remembered[slot]=(int16_t)v;
  Serial.printf("[KATALOG] %s -> OFM%d\n",entries[slot].slug,v);
  return "OFM"+String(v);
}
}
