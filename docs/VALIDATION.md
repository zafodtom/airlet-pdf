# Skutečně provedené ověření – 2. 11. 2026

## Prostředí a rozsah

Ubuntu 24.04.3 LTS v řízeném kontejneru, GCC 13.3.0, CMake 3.31.6, Qt 6.4.2, PDFium 158.0.8125.0 (chromium/8125), **Release**, backend Qt offscreen. Nejde o cílový Windows počítač ani skutečný monitor. Soukromá uživatelská PDF nebyla použita.

Windows závislosti Qt 6.8.3 MSVC 2022 x64 a PDFium win-x64 byly skutečně staženy; zjištěné SHA256 jsou uzamčeny. Zkontrolována struktura archivů, importní knihovna PDFium, dostupnost windeployqt, jeho přepínače v odpovídajícím Qt zdroji a licenční soubory. Windows kód **nebyl zkompilován ani spuštěn**, PowerShell/Inno skripty čekají na Windows ověření.

## Výsledky

| Kontrola | Výsledek |
|---|---|
| CMake Release konfigurace a kompilace | Úspěšné, bez compiler warnings po finálních úpravách |
| QtTest | 28 passed, 0 failed, 0 skipped |
| QtTest s QT_SCALE_FACTOR=2 | 28 passed, 0 failed, 0 skipped |
| Unicode cesta, heslo správné/nesprávné, neexistující soubor | Prošlo |
| Dlaždice vs odpovídající část celého renderu, 4 otočení | Přesná shoda pixelů |
| CropBox + vestavěné otočení, text a geometrie | Prošlo na syntetickém vzorku |
| Sken bez textu | Bez textové vrstvy, žádné OCR |
| 500 stran, rychlé přechody/zoom, změna dokumentu | Prošlo |
| Rušení již běžící úlohy a join při ukončení | Prošlo s řízeným pomalým testovacím jádrem |
| Zahození staré session/generace | Prošlo |
| Limit raster cache a vyprázdnění při zavření | Prošlo |
| Po dokončení fronty žádné nové render úlohy | Prošlo |
| A0 stránka při fit-width | Prošlo; opravena oscilace šířky kvůli scrollbarům |
| Otevření zobrazuje začátek první stránky | Prošlo |
| Vizuální kontrola rasteru a hlavního okna | Provedena na offscreen snímcích syntetického PDF |
| Windows GUI, instalace a odinstalace | **Neprovedeno** |
| Windows přechod mezi skutečnými HiDPI monitory | **Neprovedeno** |
| Studený start se řízenou cache | **Neprovedeno** |
| Dlouhodobá paměť cílové aplikace | **Nezměřeno** |
| Export/výběr/tisk/slučování | Dosud neimplementováno, nelze hodnotit |

## Orientační startovní měření

Měřeno skutečným spuštěním Release aplikace s `--metrics-file` a syntetickými fixtures. Stav diskové cache nebyl řízen. Tři spuštění basic.pdf jsou opakovaná nová spuštění procesu, nikoli potvrzené studené běhy. Qt offscreen neměří fyzický výstup na monitoru.

| Dokument | První paint od main | První paint od otevření |
|---|---:|---:|
| basic.pdf, běh 1 | 37 ms | 17 ms |
| basic.pdf, běh 2 | 39 ms | 21 ms |
| basic.pdf, běh 3 | 36 ms | 19 ms |
| technical-a0.pdf | 52 ms | 34 ms |
| long.pdf (500 stran) | 32 ms | 18 ms |
| scan.pdf | 41 ms | 26 ms |

Tyto hodnoty popisují jen toto vývojové prostředí a jednoduché syntetické soubory. Nejsou odhadem rychlosti na Xeonu ani srovnáním s jinými PDF aplikacemi. Pro Windows je připraven samostatný postup v PERFORMANCE.md.

V testovací klidové půlsekundě po dokončení práce byl čítač render úloh neměnný. Měření procesního CPU přes `std::clock` vykázalo při jednom běhu 0,250 ms za 500 ms (200% DPI běh 0,113 ms). Jsou to krátké vzorky v testovacím procesu s jednorázovým ukončovacím timerem, nikoliv důkaz nulového CPU za všech okolností. Produkční worker nemá timeoutové probouzení a v klidu blokuje na condition_variable.

Externí CPU/RSS čítače virtualizovaného `/proc` vracely nespolehlivé výsledky. Nebyly použity k tvrzení o paměti nebo klidové CPU. Dlouhodobé RSS/private bytes a studený start zůstávají pro skutečné Windows měření.

## Opravy nalezené během ověření

- Stabilní šířka viewportu odstranila fit-width smyčku s přidáváním a odebíráním scrollbarů u A0 na šířku.
- Po prvním fit se explicitně přejde na začátek první stránky.
- První obraz se měří až při paintu s přijatým PDF obrazem.
- Malá cache tří PDFium page handles brání opakovanému načítání téže stránky pro každou dlaždici.
- Změna rozměru dosud neznámé stránky ihned invaliduje rozpracovanou generaci.

## Zbývající překážka a stav dodávky

Vlastník následně založil `zafodtom/airlet-pdf`; připojení ověřilo soukromou viditelnost, prázdný repozitář a oprávnění admin/push. Implementace se nahrává přímo tam. Nic do jiného repozitáře nebylo zapisováno. **Windows instalační artefakt zatím nevznikl** a M1 proto nelze označit za kompletně dodaný pro Windows.

Zdrojový strom obsahuje použitelnou Linuxem ověřenou implementaci, manuální Windows workflow a instalátorový skript. Následující krok je úspěšný ruční Windows build; podrobnosti v GITHUB.md a BUILD.md. GitHub připojení nemá workflow-dispatch operaci, proto je pro spuštění potřeba webové rozhraní nebo autorizované CLI.
