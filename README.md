# LoggX Studio

Videoschnitt und Musikbearbeitung in einem Programm – für macOS und Windows 11 (Linux läuft ebenfalls).

Dieses Repository enthält den **Prototyp (Phase 1 der Roadmap)**: eine funktionierende Schnitt-Engine mit Oberfläche. Es ist noch kein fertiges Produkt.

![Arbeitsbereich Schnitt](docs/schnitt.png)

## Was der Prototyp kann

- **Import** von Video, Audio und Bildern (alles, was FFmpeg liest: MP4, MOV, MKV, MP3, WAV, PNG, JPG …)
- **Timeline** mit beliebig vielen Video- und Audiospuren: Clips verschieben (auch zwischen Spuren), an beiden Enden trimmen, teilen, löschen, Lücke schließen, Einrasten an Nachbarn und Abspielkopf
- **Masken** pro Videoclip: Rechteck oder Ellipse mit Position, Größe, weicher Kante und Umkehrung
- **Deckkraft** und Überlagerung mehrerer Videospuren
- **Ton**: Lautstärke, **Tonhöhe (Pitch) ±12 Halbtöne ohne Tempoänderung**, **Tempo 0,25×–4× ohne Tonhöhenänderung**
- **Vorschau** mit Ton, bildgenaues Springen
- **Export** als H.264 + AAC (MP4/MOV/MKV) in Projektauflösung, 1080p, 720p oder 480p; nutzt den Hardware-Encoder, wenn vorhanden
- **Projektdateien** `.loggx` (JSON, Medienpfade auch relativ gespeichert), Undo/Redo ohne Grenze
- Kommandozeile: `loggx-render projekt.loggx ausgabe.mp4 [--size 1280x720]`

## Was noch fehlt (laut Konzept)

| Phase | Inhalt |
| --- | --- |
| MVP | GPU-Rendering (Metal / DirectX 12), Effekte und Übergänge, Farbkorrektur + LUTs, Keyframes, Titel, Musikmodul mit MIDI, Instrumenten und Mixer, Wellenformen und Vorschaubilder in der Timeline, Autosave, Admin-Zugang und Lizenzsystem, Export bis 4K |
| 1.0 | 8K, ProRes, H.265, AV1, HDR, Masken-Tracking, KI-Freistellung, VST3/AU/CLAP, Aufnahme, Beat-Sync, Web-Admin-Dashboard |
| Ausbau | Stem-Separation, Auto-Reframe, Mastering-Assistent, Marktplatz, Multicam, RAW |

Bekannte Grenzen des Prototyps:

- Bilder werden auf der CPU berechnet. Die Vorschau wird deshalb auf höchstens 960×540 verkleinert; 4K-Material kann ruckeln.
- Die Wiedergabe-Uhr läuft unabhängig vom Audiogerät. Bei sehr langen Durchläufen ohne Stopp kann der Ton minimal vom Bild wegdriften.
- Drehungs-Metadaten (Hochkant-Videos vom Handy) werden noch nicht ausgewertet.
- Es gibt noch kein Installationspaket; das Programm wird aus dem Quelltext gebaut.

## Aufbau

```
core/     Projektmodell, Timeline-Operationen, .loggx-Format (C++20, keine Abhängigkeit außer nlohmann/json)
media/    FFmpeg: Analyse, Video-/Audio-Decoder, Exporter
engine/   Compositor (Bild), AudioMixer (Ton, Rubber Band), Projekt-Render
app/      Oberfläche mit Qt 6 / QML
tools/    loggx-render (Export ohne Oberfläche)
tests/    Tests für Kern, Engine (End-to-End) und App-Logik
```

Abweichung vom Konzept: Die Tonausgabe läuft im Prototyp über Qt Multimedia, die Audio-Engine ist eigener Code plus Rubber Band. JUCE kommt erst mit dem Plugin-Hosting (VST3/AU) dazu.

## Bauen

Benötigt: CMake ≥ 3.21, C++20-Compiler, Qt ≥ 6.4 (Quick, Quick Controls, Multimedia), FFmpeg ≥ 6, optional Rubber Band ≥ 3 (für Pitch/Tempo).

### macOS

```sh
brew install cmake ninja pkg-config qt ffmpeg rubberband nlohmann-json
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open "build/app/LoggX Studio.app"
```

### Windows 11

Visual Studio 2022 (C++), Qt 6 (Online-Installer, mit „Qt Multimedia“) und vcpkg:

```powershell
vcpkg install "ffmpeg[core,avcodec,avformat,swscale,swresample]:x64-windows" nlohmann-json:x64-windows
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64
cmake --build build --config Release
C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe --qmldir app\qml "build\app\Release\LoggX Studio.exe"
```

Hinweis: Unter Windows wird derzeit ohne Rubber Band gebaut; Tonhöhe und Tempo wirken dort noch nicht auf den Ton.

### Linux (Ubuntu 24.04)

Paketliste siehe `.github/workflows/build.yml`, danach:

```sh
cmake -S . -B build -G Ninja && cmake --build build
"./build/app/LoggX Studio"
```

### Tests

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Bedienung

| Aktion | Taste |
| --- | --- |
| Abspielen / Pause | Leertaste |
| Am Abspielkopf teilen | S oder Strg+B |
| Clip löschen / mit Lücke schließen | Entf / Umschalt+Entf |
| Ein Bild zurück / vor | ← / → |
| Rückgängig / Wiederholen | Strg+Z / Strg+Umschalt+Z |
| Medien importieren | Strg+I |
| Timeline zoomen | Strg + Mausrad |

Doppelklick auf eine Datei in der Medienliste legt sie am Abspielkopf in die Timeline. Ein Klick auf den Spurkopf schaltet die Spur stumm bzw. unsichtbar. Doppelklick auf die Beschriftung eines Reglers setzt ihn zurück.

## Lizenzen der Abhängigkeiten

Vor einer Veröffentlichung zu klären: Qt (LGPL oder kommerziell), FFmpeg (LGPL; Builds mit libx264 sind GPL), Rubber Band (GPL oder kommerziell), H.264/AAC-Patentlizenzen. Für dieses Repository ist noch keine eigene Lizenz festgelegt.
