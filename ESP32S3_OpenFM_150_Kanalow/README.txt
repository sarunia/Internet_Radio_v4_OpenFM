ESP32-S3 OPENFM HLS: 150 NUMEROWANYCH POZYCJI (TEST)
======================================================

PLIKI: .ino, OpenFmResolver.cpp/.h, OpenFmPlayer.cpp/.h
GRAJACY SILNIK AUDIO I CANVAS SPI 40 MHz NIE SA ZMIENIONE.

POZYCJE:
1 OFM10, 2 OFM31, 3 OFM3, 4 OFM14 (wczesniej testowane).
5..150: kandydaci OFM1..OFM150 z pominieciem czterech powyzszych.
TO NIE JEST POTWIERDZONY KATALOG 150 STACJI OPENFM.
Nie istnieje tutaj zweryfikowane mapowanie wszystkich rzeczywistych kanalow.
Dla nieobslugiwanych identyfikatorow API moze zwrocic blad.

Pilot lewo/prawo (i gora/dol): nastepna/poprzednia pozycja.
Po starcie uruchamia sie pozycja 1 = OFM10.
Serial: g 150 (pozycja 150), g 23, 1..9, n, p, o OFM10, s, r, +, -, m, h.

METADANE UTWOROW
- Wykonawca i tytul sa oznaczone "--" gdy brak sprawdzonego zrodla.
- Test UI: meta Jan Kowalski|Przykladowy utwor
  (WARTOSCI WPROWADZONE RECZNIE, NIE POBRANE Z OPENFM)
- API /api/user/token zwraca URL playlisty, nie potwierdzono w nim
  dostepnych pol wykonawcy i tytulu. Nie przypisujemy ich na slepo.
- Segment MPEG-TS moze przenosic metadane ID3 w osobnym PID, ale nie
  zostalo potwierdzone w probce OFM10. Wymaga osobnego testu PAT/PMT/ID3
  albo wiarygodnego endpointu metadanych OpenFM.

UWAGA: Nie podajemy tokenu na Serial. Mozna w przyszlosci wymienic
kandydatow na faktyczna liste identyfikatorow, nie naruszajac dekodera.
