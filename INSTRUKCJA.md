# PRISMA G-1 (v0.4) – granular slicer z rytmicznymi ziarnami

Prisma to **instrument** (ścieżka MIDI). Wrzucasz jeden sampel (pętlę, wokal, kawałek utworu), wtyczka tnie go na **pady pod klawiszami od C1 w górę**, a każdy pad gra swój fragment granularnie: wysokość nie zmienia się z klawiszem.

## 1. Sampel i podział (pasek pod ekranem)
- Przeciągnij plik na ekran albo kliknij **LOAD**. **CLEAR** czyści.
- **GRID**: podział według wartości nut. Wybierz **1/1 … 1/32** oraz **STR** (zwykłe), **TRI** (triole) albo **DOT** (kropkowane). **BPM** to tempo sampla: ustawione na „Project” bierze tempo projektu (idealne dla klipów z Abletona). Jeśli sampel ma inne tempo, przeciągnij i ustaw np. 92.
- **EQUAL**: równe części (4 … 64).
- **TRANSIENTS**: cięcie na uderzeniach, **SENS** = czułość.
- Maksymalnie 64 pady.

## 2. Ręczne ustawianie padów (na ekranie)
- Kliknij pad na ekranie albo kafelek na dole ekranu. Pad, który grasz z klawiatury, zaznacza się sam.
- **Przeciągnij środek ramki**, żeby przesunąć fragment. **Przeciągnij krawędź**, żeby go wydłużyć lub skrócić.
- **SNAP** przyciąga do siatki. Trzymaj **Shift**, żeby ustawić swobodnie.
- **RE-SLICE** tnie od nowa według ustawień i cofa ręczne zmiany.

## 3. GRAIN / ALL PADS (ustawienia wszystkich padów, automatyzowalne w Abletonie)
Nowy silnik w 0.4: zamiast gęstej chmury drobinek pad gra **długie ziarna w rytm siatki**. Każde ziarno to kawałek dźwięku grany w oryginalnej wysokości.
- **GRAINS**: jak często strzela ziarno (1/1 … 1/64, z triolami i kropkowanymi), zawsze równo z taktem. Pierwsze ziarno gra od razu po naciśnięciu klawisza.
- **SYNC**: dioda świeci = rytm według tempa projektu. Zgaszona = wolne tempo w Hz (duża gałka pokazuje wtedy Hz).
- **LENGTH**: długość jednego ziarna w ms (10 ms … 1 s). Krócej niż odstęp między ziarnami = pocięte z przerwami. Dłużej = ziarna nachodzą na siebie i robi się gęsto, bez dziur.
- **POSITION**: od którego miejsca w padzie startują ziarna.
- **SCAN**: jak szybko ziarna przesuwają się przez pad (1x = normalnie, 0 = zamrożenie, czyli powtarzanie tego samego kawałka).
- **PITCH**: strojenie w półtonach.
- **CHAOS**: losowe przesunięcia, lekkie rozstrojenie, czasem pominięte uderzenie, a przy dużych wartościach oktawy i ziarna od tyłu.
- **SPACE**: szerokość stereo, ziarna rozkładają się na lewo i prawo.
- **SHAPE**: kształt ziarna. **SQUARE** (pełne, zachowuje atak), **SOFT** (miękkie wejście i wyjście), **PLUCK** (szybko gaśnie), **SWELL** (narasta).

## 4. PAD (ustawienia tylko wybranego pada)
- **RATE** (rytm ziaren) i **LENGTH** (długość ziarna w ms) pada. Dioda **LINK** świeci = pad korzysta z ustawień ogólnych (gałka jest wtedy szara). Pad z własnym RATE zawsze gra w rytm projektu, nawet gdy SYNC jest wyłączony. Kręcisz gałką pada = pad dostaje własną wartość i LINK gaśnie. Kliknięcie LINK = powrót do ustawień ogólnych.
- **REVERSE**: pad gra od tyłu.
- **RESET PAD** / **RESET ALL PADS**: usuwa własne ustawienia.
- Pady z własnymi ustawieniami mają **pomarańczową kropkę** na ekranie.

## 5. Ekran
- Kropkowany wyświetlacz w stylu starych urządzeń (VFD).
- Kiedy strzela ziarno, kawałek fali, który właśnie gra, **unosi się z ekranu** i rozszczepia na kolory.
- Promień leci do **pryzmatu** po prawej, a pod nim **wodospad** pokazuje rytm: każda kreska to ziarno (w poziomie: lewo/prawo w stereo, długość: jak długo gra, kolor: wysokość).
- Stare projekty z v0.3 otworzą się z tymi samymi padami, ale własne RATE/LENGTH padów wrócą do LINK (zmieniło się ich znaczenie).

## 6. Po prawej
**AMP** (ADSR), **TONE** (Low Cut, High Cut, Reso), **COLOR / OUT** (Crunch, Wash, Pump, Volume).

## Zgłaszanie poprawek
**COPY** kopiuje wszystkie ustawienia razem z padami. Wklej je do czatu z opisem. **PASTE** wkleja ustawienia, które dostaniesz ode mnie. Numer wersji jest obok logo.

## Instalacja (Mac)
Pobrany folder nazywa się teraz z numerem wersji, np. `Prisma-v0.4.0-macOS`. Ta komenda sama bierze najnowszy:
```
SRC="$(ls -td ~/Downloads/Prisma-v*-macOS | head -1)"; echo "Instaluję: $SRC"
rm -rf ~/Library/Audio/Plug-Ins/VST3/Prisma*.vst3 ~/Library/Audio/Plug-Ins/Components/Prisma*.component
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R "$SRC/Prisma.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$SRC/Prisma.component" ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Prisma.vst3 ~/Library/Audio/Plug-Ins/Components/Prisma.component
```
Potem zamknij i otwórz Abletona, Settings → Plug-Ins → Option + Rescan.
