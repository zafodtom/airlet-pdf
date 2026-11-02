# Airlet PDF 0.4.1

Minimalistický lokální PDF prohlížeč pro Windows 10 (1809+) / 11 x64. C++20, Qt Widgets a vyměnitelné PDF jádro. Žádné účty, cloud, síťové požadavky, telemetrie ani proces po zavření.

**Stav 0.4.1:** souvislé zvýraznění hledání i výběru myší, přehlednější hlavní menu, vlastní ikona a volba výchozí aplikace PDF na konci instalace. Windows sestavení této aktualizace čeká na nové ruční workflow.

Poslední ověřená Windows verze je [0.4.0 – instalátor a portable](https://github.com/zafodtom/airlet-pdf/actions/runs/41488401076/artifacts/13162852174). Prošla kompilací, testy i balením v běhu #7; uživatel potvrdil hledání, tvorbu PDF i tisk. Artefakty se uchovávají 3 dny.

## Implementováno

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

Viz [docs/BUILD.md](docs/BUILD.md). Ruční Windows workflow vytvoří `AirletPDF-Setup-0.4.1-x64.exe`, přenosný ZIP a odpovídající zdroje Qt. Instalátor zaregistruje Airlet PDF pro příponu .pdf a na závěrečné stránce nabídne otevření nastavení výchozích aplikací. Ve Windows 11 se otevře stránka Airlet PDF, ve Windows 10 obecná volba výchozích aplikací. Změnu dokončí uživatel v systémovém rozhraní.

```powershell
# V Developer PowerShell for VS 2022, po přípravě závislostí:
.\scripts\build-windows.ps1
```

Soukromý repozitář: [zafodtom/airlet-pdf](https://github.com/zafodtom/airlet-pdf). [docs/GITHUB.md](docs/GITHUB.md) popisuje ruční spuštění Windows sestavení.

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

Zdrojový kód Airlet PDF zůstává soukromý; nebyla mu bez rozhodnutí vlastníka přidělena veřejná licence. Práva ke knihovnám Qt a PDFium jejich licence nemění. Při distribuci jsou zachována práva příjemce k výměně LGPL knihoven a ladění jejich změn.
