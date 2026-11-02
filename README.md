# Airlet PDF 0.6.0

Minimalistický lokální PDF prohlížeč pro Windows 10 (1809+) / 11 x64. C++20, Qt Widgets a vyměnitelné PDF jádro. Prohlížení bez účtů, cloudu a telemetrie. Síť se používá výhradně po ruční volbě aktualizací; bez kontroly při startu a bez periodické služby.

**Stav 0.6.0:** procházení souborů oběma směry, nabídka Nastavení a ruční integrované aktualizace instalované Windows edice. Nová verze čeká na ruční Windows sestavení a ověření instalace aktualizace na cílovém PC.

Poslední ověřený Windows balíček: [0.5.0 – instalátor a portable](https://github.com/zafodtom/airlet-pdf/actions/runs/41507164928/artifacts/13170954187), úspěšný běh #9. Artefakty se uchovávají 3 dny; stabilní vydání pro aktualizace jsou v [GitHub Releases](https://github.com/zafodtom/airlet-pdf/releases).

**English:** Choose **Nastavení → Jazyk → English** and restart the application. Settings contains **Language**, **Continue between files** (off at startup), and **Updates**. With file continuation enabled, scrolling past the end opens the next PDF; scrolling before the start opens the previous PDF at its last page. Integrated updates support the installed Windows edition and run only when requested. Portable packages are updated manually.

## Implementováno

- **Nastavení → Pokračovat dalším souborem**: výchozí stav vypnuto, přechod na následující i předchozí PDF ze stejné složky v přirozeném pořadí názvů. Na hranicích složky bez smyčky. [Podrobnosti](docs/FOLDER-AND-LANGUAGE.md).
- **Nastavení → Jazyk**: čeština (výchozí) nebo angličtina, volba se projeví po restartu. Anglické menu používá Settings → Language. Systémové dialogy používají jazyk Windows.
- **Nastavení → Aktualizace…**: ruční kontrola veřejných GitHub Releases, stažení s SHA-256, potvrzení restartu, tichá instalace a nové otevření PDF. Pro instalovanou Windows edici; bez automatických kontrol. [Postup a ověření](docs/UPDATES.md).
- Soubor: Otevřít, Zavřít, Export; Nápověda: O aplikaci. Tyto položky nejsou v nástrojové liště; klávesové zkratky zůstávají.
- Vlastní ikona v okně, Windows EXE, zástupcích, instalátoru a asociaci PDF.
- Otevření dialogem, přetažením a cestou v argumentu; české Unicode cesty; heslo.
- Svislé prohlížení, přechod na stránku, první/poslední, předchozí/další.
- Ctrl+kolečko s kotvou pod kurzorem, +/−, 100 %, šířka/celá stránka; posun prostředním tlačítkem.
- Dodatečné otočení aktuální stránky po 90° pro zobrazení.
- Jeden worker, progresivní rušení PDFium, generace a session, 512px dlaždice, omezená cache, nízké rozlišení během zoomu, přednačtení jediné sousední stránky.
- Rozměry stran zjišťované při návštěvě, bez počátečního průchodu celým PDF.
- Paměť velikosti okna a poslední úspěšně otevřené složky.
- V režimu celá stránka jeden zářez kolečka přejde o jednu stránku, včetně různých formátů stran. V ostatních režimech větší krok myši, pixelový posun touchpadu zachován.
- Režim **Text**: skutečná textová vrstva, výběr tažením a Ctrl+C; diakritika a zalomení řádků, bez OCR. Data se načtou až při prvním výběru.
- Režim **Výřez**: obdélník do schránky přes Ctrl+C nebo do PNG/JPG přes kontextové menu. Průběh, zrušení, atomické uložení a ochrana zdrojového PDF.
- Textový i obrazový výběr je v této iteraci omezen na jednu stránku. Přetrvá při zoomu/otočení; Escape jej zruší. Výběr je dočasný a dokument nemění.
- Export… / Ctrl+E: aktuální, všechny nebo vlastní rozsah stran do PNG/JPG, DPI a kvalita JPG, průběh a zrušení. Samostatný proces chrání odezvu prohlížeče.
- Ctrl+F: vyhledání textu bez rozlišení velikosti písmen, zvýraznění aktuálního nálezu, další/předchozí, zrušení. Samostatný proces se spustí až po zadání dotazu, bez OCR.
- Ctrl+P: systémový tiskový dialog, aktuální/vše/vlastní rozsah, přizpůsobení do tisknutelné oblasti bez ořezu, průběh a zrušení.
- Soubor → PDF z obrázků: PNG/JPG/BMP, pořadí tažením nebo tlačítky, A4 na výšku/šířku či velikost podle obrázku a DPI. Nový soubor se dokončí atomicky bez přepisu.
- CMake, syntetické regresní testy, Windows build/deploy skript, Inno Setup, ruční Actions.

**Otočení mění pouze zobrazení a vzhled exportu/tisku.** Výřez a PNG/JPG export kopírují vykreslený vzhled, nikoli původní obrazový stream. Benchmark, panel navigace, úpravy pořadí/mazání/otáčení PDF stran, samostatná extrakce vložených obrázků a historie dokumentů nyní nejsou v rozsahu. [Dohodnutý rozsah](docs/BACKLOG.md).

## Sestavení a instalace

Viz [docs/BUILD.md](docs/BUILD.md). Ruční Windows workflow vytvoří `AirletPDF-Setup-0.6.0-x64.exe`, přenosný ZIP a odpovídající zdroje Qt. Instalátor zaregistruje Airlet PDF pro příponu .pdf a na závěrečné stránce nabídne otevření nastavení výchozích aplikací. Ve Windows 11 se otevře stránka Airlet PDF, ve Windows 10 obecná volba výchozích aplikací. Změnu dokončí uživatel v systémovém rozhraní.

```powershell
# V Developer PowerShell for VS 2022, po přípravě závislostí:
.\scripts\build-windows.ps1
```

Repozitář: [zafodtom/airlet-pdf](https://github.com/zafodtom/airlet-pdf). [docs/GITHUB.md](docs/GITHUB.md) popisuje ruční spuštění Windows sestavení.

## Ovládání

| Akce | Ovládání |
|---|---|
| Hledat text | Ctrl+F (Enter: další nález, Escape: zavřít hledání) |
| Tisk | Ctrl+P |
| PDF z obrázků | Soubor → PDF z obrázků… |
| Otevřít / zavřít dokument | Ctrl+O / Ctrl+W |
| Předchozí / další stránka | PageUp / PageDown |
| První / poslední | Ctrl+Home / Ctrl+End |
| Zoom | Ctrl+kolečko, Ctrl+plus / Ctrl+minus |
| 100 % / šířka / celá stránka | Ctrl+0 / Ctrl+1 / Ctrl+2 |
| Otočit doprava / doleva | Ctrl+R / Ctrl+Shift+R |
| Posun | Kolečko, posuvníky, prostřední tlačítko |
| Výběr textu / výřezu | Tlačítka Text / Výřez nebo Alt+T / Alt+X |
| Kopírovat vybraný text / obrázek | Ctrl+C |
| Uložit výřez | Pravé tlačítko → Uložit výřez… |
| Zrušit výběr | Escape |

## Architektura a licence

[Architektura](docs/ARCHITECTURE.md), [měření](docs/PERFORMANCE.md), [licenční podmínky](licenses/THIRD-PARTY.md).

Repozitář Airlet PDF je veřejný; samostatná licence ke zdrojovému kódu aplikace nebyla vlastníkem určena. Práva ke knihovnám Qt a PDFium jejich licence nemění. Při distribuci jsou zachována práva příjemce k výměně LGPL knihoven a ladění jejich změn.
