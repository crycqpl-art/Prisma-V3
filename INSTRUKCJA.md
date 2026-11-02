# PRISMA G-1 (v0.3) – granular slicer

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
- **RATE**: gdy trzymasz klawisz, pad zaczyna od nowa co 1/4, 1/8, 1/16… zgodnie z siatką taktu. **OFF** = płynna chmura.
- **LENGTH**: jak długo gra każde powtórzenie (krócej = bardziej pocięte).
- **GRAIN SIZE**: wielkość drobinek (małe = ziarniste, duże = gładkie).
- **DENSITY**: ile drobinek na sekundę.
- **SCAN**: prędkość przesuwania się przez fragment (1x = normalnie, 0 = zamrożenie, 0.5x = wolniej, bez zmiany wysokości).
- **SPREAD**: jak szeroko drobinki rozlatują się wokół punktu grania.
- **CHAOS**: losowość pozycji, wysokości (czasem oktawa) i panoramy.
- **PITCH**: strojenie w półtonach.

## 4. PAD (ustawienia tylko wybranego pada)
- **RATE** i **LENGTH** pada. Dioda **LINK** świeci = pad korzysta z ustawień ogólnych (gałka jest wtedy szara). Kręcisz gałką pada = pad dostaje własną wartość i LINK gaśnie. Kliknięcie LINK = powrót do ustawień ogólnych.
- **REVERSE**: pad gra od tyłu.
- **RESET PAD** / **RESET ALL PADS**: usuwa własne ustawienia.
- Pady z własnymi ustawieniami mają **pomarańczową kropkę** na ekranie.

## 5. Po prawej
**AMP** (ADSR), **TONE** (Low Cut, High Cut, Reso), **COLOR / OUT** (Crunch, Wash, Pump, Volume).

## Zgłaszanie poprawek
**COPY** kopiuje wszystkie ustawienia razem z padami. Wklej je do czatu z opisem. **PASTE** wkleja ustawienia, które dostaniesz ode mnie. Numer wersji jest obok logo.

## Instalacja (Mac)
Pobrany folder nazywa się teraz z numerem wersji, np. `Prisma-v0.3.0-macOS`. Ta komenda sama bierze najnowszy:
```
SRC="$(ls -td ~/Downloads/Prisma-v*-macOS | head -1)"; echo "Instaluję: $SRC"
rm -rf ~/Library/Audio/Plug-Ins/VST3/Prisma*.vst3 ~/Library/Audio/Plug-Ins/Components/Prisma*.component
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
cp -R "$SRC/Prisma.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$SRC/Prisma.component" ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Prisma.vst3 ~/Library/Audio/Plug-Ins/Components/Prisma.component
```
Potem zamknij i otwórz Abletona, Settings → Plug-Ins → Option + Rescan.
