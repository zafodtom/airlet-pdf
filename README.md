# Airlet PDF 0.3.0

Minimalistický lokální PDF prohlížeč pro Windows 10 (1809+) / 11 x64. C++20, Qt Widgets a vyměnitelné PDF jádro. Žádné účty, cloud, síťové požadavky, telemetrie ani proces po zavření.

**Stav 0.3.0:** export celých stránek do PNG/JPG je implementovaný a ověřený Linux Release testy. Nový Windows build čeká na ruční spuštění. Verze 0.2.0 prošla Windows CI v běhu #3, včetně testů a instalátoru.

[Instalační balíček 0.2.0](https://github.com/zafodtom/airlet-pdf/actions/runs/41479675732/artifacts/13159656695) je poslední hotové Windows sestavení. Export celých stránek je až ve 0.3.0. [Export a jeho ověření](docs/EXPORT.md).

## Implementováno

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
- CMake, syntetické regresní testy, Windows build/deploy skript, Inno Setup, ruční Actions.

**Otočení se nyní neukládá do PDF.** Extrakce samostatných vložených obrázků, tisk, export TXT/PDF, extrakce obrázků a skládání PDF zbývají. Výřez kopíruje vzhled vybrané oblasti, nikoliv původní vložený obrázek. [Plán](docs/BACKLOG.md).

## Sestavení a instalace

Poslední hotový instalační balíček **0.1.0** z úspěšného běhu #2: [AirletPDF-0.1.0-x64](https://github.com/zafodtom/airlet-pdf/actions/runs/41471603485/artifacts/13154494974), přístupný přihlášenému vlastníkovi do 5. 11. 2026. ZIP obsahuje instalátor a přenosný ZIP; další spuštění workflow vytvoří nový balíček.

Viz [docs/BUILD.md](docs/BUILD.md). Ruční Windows workflow vytvoří `AirletPDF-Setup-0.3.0-x64.exe`, přenosný ZIP a odpovídající zdroje Qt. Instalátor nabídne pouze volitelnou registraci aplikace mezi kandidáty pro PDF; výchozí aplikaci nemění.

```powershell
# V Developer PowerShell for VS 2022, po přípravě závislostí:
.\scripts\build-windows.ps1
```

Soukromý repozitář: [zafodtom/airlet-pdf](https://github.com/zafodtom/airlet-pdf). [docs/GITHUB.md](docs/GITHUB.md) popisuje ruční spuštění Windows sestavení.

## Ovládání

| Akce | Ovládání |
|---|---|
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
