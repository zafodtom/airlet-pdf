# Airlet PDF

- C++20, Qt 6 Widgets, PDFium. Runtime bez Pythonu, WebView, telemetrie, sítě a updateru.
- Soukromý projekt `zafodtom/airlet-pdf`; nikdy jej nenahrazovat veřejným repozitářem.
- Priorita: první správný obraz, plynulost, klid bez vlastní periodické práce.
- Veškerá PDFium API v `src/pdfium`, žádné PDFium typy mimo tento modul.
- Volání PDFium serializovat globálně; dokument a QFile vlastní worker. Dlouhý export v budoucnu izolovat do samostatného procesu, ne do paralelního PDFium vlákna.
- Žádná PDF operace v GUI vlákně. Výsledky ověřovat podle session i generace.
- Načítat jen viditelné dlaždice a nejvýše sousední stránku v nízkém rozlišení.
- Cache musí být omezená. `paintEvent` nikdy neplánuje render. Po vyprázdnění fronty worker blokuje na condition_variable.
- Novou interakci nebo změnu layoutu doprovodit správnou invalidací. Pozor na zoom, crop, vlastní i vestavěné otočení a HiDPI.
- Neinstalovat LibreOffice; OCR a kancelářské převody mimo rozsah.
- Převody musí mít zrušení, průběh a atomické dokončení bez přepisu originálů.
- Prázdná nebo nefunkční tlačítka do UI nepřidávat. Výběr, ukládání, tisk a export nyní v backlogu.
- Release závislosti připnout a ověřit hashem; zachovat licence i odpovídající zdroje Qt při distribuci.
- Testy spustit `ctest --test-dir build --output-on-failure -C Release`.
- Testovat pouze syntetické dokumenty. Výkon Windows označovat jako změřený pouze při skutečném Windows běhu.
- Actions pouze `workflow_dispatch`, jedna Windows konfigurace, artefakty 3 dny. Jiné repozitáře neměnit.
