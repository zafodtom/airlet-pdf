# Skutečně provedené ověření – 2. 11. 2026

## Prostředí a rozsah

Ubuntu 24.04.3 LTS v řízeném kontejneru, GCC 13.3.0, CMake 3.31.6, Qt 6.4.2, PDFium 158.0.8125.0 (chromium/8125), **Release**, backend Qt offscreen. Nejde o cílový Windows počítač ani skutečný monitor. Soukromá uživatelská PDF nebyla použita.

Windows závislosti Qt 6.8.3 MSVC 2022 x64 a PDFium win-x64 byly skutečně staženy; zjištěné SHA256 jsou uzamčeny. Zkontrolována struktura archivů, importní knihovna PDFium, dostupnost windeployqt, jeho přepínače v odpovídajícím Qt zdroji a licenční soubory. První Windows build č. 1 (https://github.com/zafodtom/airlet-pdf/actions/runs/41470150175) ověřil stažení/rozbalení závislostí a CMake konfiguraci na Windows Server 2022, MSVC 19.44.35229.0, SDK 10.0.26100.0. Kompilace adaptéru PDFium selhala na kolizi makra Windows `max` se `std::max`. Doplněno NOMINMAX a WIN32_LEAN_AND_MEAN; testy MSVC nově explicitně používají UTF-8. Oprava prošla v novém [Windows běhu #2](https://github.com/zafodtom/airlet-pdf/actions/runs/41471603485): Release kompilace, automatické offscreen testy a Inno Setup skončily úspěšně. Interaktivní GUI a skutečná instalace na čistém cílovém PC ještě neproběhly.

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
| Windows CMake konfigurace | Prošla v prvním CI běhu |
| Windows Release kompilace | **Prošla v CI běhu #2**, MSVC 19.44, Qt 6.8.3 |
| Windows automatické testy | **Prošly**: CTest 1/1 cíl, 0 selhání, 2,24 s, Qt offscreen |
| Inno Setup a přenosný ZIP | **Úspěšně vytvořeny**, Inno Setup 6.7.1 |
| Kontrola distribučních souborů | SHA256, integrita ZIP, EXE PE x64, Qt/PDFium/MSVC DLL, licence a zdroj Qt ověřeny |
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
- Windows sestavení definuje NOMINMAX před windows.h z PDFium, aby nezničilo volání std::max; MSVC testy mají explicitní UTF-8.
- Změna rozměru dosud neznámé stránky ihned invaliduje rozpracovanou generaci.

## Dostupný balíček a zbývající praktické ověření

Soukromý repozitář: https://github.com/zafodtom/airlet-pdf

Úspěšný běh #2 pro commit `3a61da0` vytvořil [artefakt AirletPDF-0.1.0-x64](https://github.com/zafodtom/airlet-pdf/actions/runs/41471603485/artifacts/13154494974). Je dostupný oprávněnému přihlášenému uživateli do 5. 11. 2026. Kompletní strukturovaný záznam je v `docs/validation/windows-build-2.json`.

- Instalátor `AirletPDF-Setup-0.1.0-x64.exe`: 61 834 219 B, SHA256 `c8169beda7caffa4b6a7d2c0d86272b4fe91ac5942808dfaa1fda41594f41412`.
- Přenosný ZIP `AirletPDF-0.1.0-x64-portable.zip`: 65 012 037 B, SHA256 `de8aa842ff3a50499b3f651f81ccaf1ad248be88db8892ea452161a2bb7f0b4d`.

Oba kontrolní součty byly ověřeny proti přiloženému SHA256SUMS.txt, celý artefakt proti digestu GitHubu. V přenosném ZIPu byly ověřeny požadované DLL, qwindows plugin, EXE pro AMD64, licence a správný odpovídající archiv zdrojů Qt.

Zbývá praktická instalace a odinstalace na Windows 10/11, interaktivní GUI, studený/teplý start, dlouhodobá paměť, klidová CPU a skutečné HiDPI monitory na cílovém PC. Nejsou tvrzeny žádné Windows výkonové výsledky.

CI balení má neblokující upozornění: zpětná lomítka PDFIUM_ROOT v generovaném install skriptu, chybějící volitelné DX shader DLL a metadata překladů. Všechny povinné soubory byly přesto přiloženy. windeployqt navíc přibalil QtNetwork kvůli generic touch pluginu; aplikace sama síťové API nepoužívá. Úklid balení je zaznamenán v backlogu pro příští build cyklus, bez dalšího CI běhu pouze kvůli upozorněním.
