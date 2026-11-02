# Sestavení a distribuce

## Windows x64 (cílová konfigurace)

- Windows 10 1809+ nebo Windows 11 x64.
- Visual Studio 2022 / Build Tools 2022, workload Desktop development with C++, SDK Windows 10/11. Použít prostředí s oprávněním redistribuovat jeho runtime.
- CMake >= 3.24, PowerShell 5.1+ nebo 7, 7-Zip, Inno Setup 6.3+.
- Qt **6.8.3 MSVC 2022 x64**, dynamické; PDFium **chromium/8125** bez V8/XFA. Přesné archivy a SHA256 jsou v `dependencies.lock.json`.

Z kořene zdrojů:

```powershell
.\scripts\build-windows.ps1
```

Verze 0.6.0 prošla Windows CI #11 a je publikovaná. Verze 0.6.1 vyžaduje nové ruční sestavení; volba publish_release navíc zveřejní stabilní balíček pro ruční aktualizace v aplikaci.

Skript stáhne QtBase binární archiv, odpovídající zdroje QtBase a PDFium, každý ověří SHA256 a připraví `.deps`. Následuje Visual Studio x64 Release sestavení, testy, staging, windeployqt, app-local MSVC DLL a Inno Setup. Není potřeba Qt účet ani placený SDK. Aplikace pro běh nepotřebuje Python, 7-Zip, CMake ani Visual Studio.

Výsledky:

- `dist/AirletPDF-Setup-0.6.1-x64.exe`
- `dist/AirletPDF-0.6.1-x64-portable.zip`
- `dist/SHA256SUMS.txt`

Zdroje Qt jsou uvnitř obou variant v adresáři `sources`. Proto mají balíčky přibližně o 47 MiB více než samotný program. Soubory licencí jsou v `licenses`.

Pouze kompilace bez instalátoru: `scripts/build-windows.ps1 -SkipInstaller`. Pro kontrolu GUI spouštět `out/portable/AirletPDF.exe`, nikoliv izolované EXE bez DLL.

Nejde o bitově identické sestavení napříč měnícími se Windows SDK a MSVC toolsety. Jsou uzamčené knihovny i build konfigurace; runner `windows-2022` a lokální VS/CMake/Inno se mohou aktualizovat. Pro bitovou reprodukovatelnost bude potřeba uzamknout celý build image. Do reportu testování vždy zapsat skutečné verze nástrojů.

## Ruční GitHub Actions

Actions → **Windows Release (manual)** → Run workflow. Jediný job, maximálně 25 minut, build paralelně 2, uchování výsledků 3 dny. Žádné push/PR/schedule triggery. Artefakt `AirletPDF-0.6.1-x64` obsahuje oba balíčky a SHA256. Volba `publish_release` navíc publikuje stabilní veřejné vydání v0.6.1 pro aktualizace v aplikaci. Bez této volby se vytvoří jen artefakty.

## Linux – vývojové ověření

Není cílovou distribuční platformou. Qt >= 6.4, GCC 13 nebo jiný C++20 kompilátor, CMake >=3.24, Ninja. Běžné Ubuntu balíčky: `cmake ninja-build qt6-base-dev qt6-base-dev-tools`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Bez PDFIUM_ROOT se správný Linux x64 archiv automaticky stáhne a ověří přes CMake. `-DPDFIUM_ROOT=/ověřená/cesta` je vývojový override; uživatel zodpovídá za ověření archivu i verze. PDFium handle/types se nepřenášejí do UI.

## Čistá instalace

Ověřit na Windows bez Qt/VS v PATH: instalace pouze pro uživatele, spuštění, dialog, drag/drop, cestu v argumentu, heslo, restart a odinstalaci. Registrace PDF je volitelný kandidát, nepíše UserChoice a nemění výchozí asociaci. Přiložené runtime knihovny jsou app-local a zaniknou s odinstalací.

QtNetwork je nasazen pouze pro oddělený AirletUpdate.exe. AirletApplyUpdate.exe je nativní Win32 se statickým MSVC CRT a BCrypt/Shell32 ze systému. Aktualizační manifest se generuje až z hotového instalátoru včetně skutečné velikosti a SHA-256.
