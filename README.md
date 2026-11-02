# Airlet PDF 0.1.0

Minimalistický lokální PDF prohlížeč pro Windows 10 (1809+) / 11 x64. C++20, Qt Widgets a vyměnitelné PDF jádro. Žádné účty, cloud, síťové požadavky, telemetrie ani proces po zavření.

**Stav:** implementace prvního milníku; viz [ověření](docs/VALIDATION.md). Windows instalátor musí projít Windows sestavením a praktickou zkouškou. Připravený workflow není dokladem úspěšného běhu.

## Implementováno

- Otevření dialogem, přetažením a cestou v argumentu; české Unicode cesty; heslo.
- Svislé prohlížení, přechod na stránku, první/poslední, předchozí/další.
- Ctrl+kolečko s kotvou pod kurzorem, +/−, 100 %, šířka/celá stránka; posun prostředním tlačítkem.
- Dodatečné otočení aktuální stránky po 90° pro zobrazení.
- Jeden worker, progresivní rušení PDFium, generace a session, 512px dlaždice, omezená cache, nízké rozlišení během zoomu, přednačtení jediné sousední stránky.
- Rozměry stran zjišťované při návštěvě, bez počátečního průchodu celým PDF.
- Paměť velikosti okna a poslední úspěšně otevřené složky.
- Rozhraní jádra a interní získání textu/geometrie; UI výběru přijde v dalším milníku.
- CMake, syntetické regresní testy, Windows build/deploy skript, Inno Setup, ruční Actions.

**Otočení se nyní neukládá.** Výběr textu/obrázků, výřez, tisk, export a skládání PDF ještě nejsou součástí uživatelského rozhraní. [Plán](docs/BACKLOG.md).

## Sestavení a instalace

Viz [docs/BUILD.md](docs/BUILD.md). Ruční Windows workflow vytvoří `AirletPDF-Setup-0.1.0-x64.exe`, přenosný ZIP a odpovídající zdroje Qt. Instalátor nabídne pouze volitelnou registraci aplikace mezi kandidáty pro PDF; výchozí aplikaci nemění.

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

## Architektura a licence

[Architektura](docs/ARCHITECTURE.md), [měření](docs/PERFORMANCE.md), [licenční podmínky](licenses/THIRD-PARTY.md).

Zdrojový kód Airlet PDF zůstává soukromý; nebyla mu bez rozhodnutí vlastníka přidělena veřejná licence. Práva ke knihovnám Qt a PDFium jejich licence nemění. Při distribuci jsou zachována práva příjemce k výměně LGPL knihoven a ladění jejich změn.
