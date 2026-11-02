# Navazující milníky

## M1 – před označením Windows verze za hotovou

- Windows Release workflow #2, EXE, testy i instalátor jsou úspěšně vytvořené. Zbývá prověřit čisté Windows 10/11 x64, instalaci a odinstalaci.
- Ve 0.2.0 je připraven úklid CMake cest a generic pluginů windeployqt; ověřit v dalším Windows buildu spolu s novými funkcemi.
- Změřit skutečný studený start, opakovaný start, HiDPI přesun monitorů, rychlé listování a A0 výkres na cílovém Xeonu/K2200.
- Prověřit paměť po dlouhém listování, klidovou CPU a ukončení během komplikovaného dekódování.

## M2 – výběr a ukládání

- Hotovo v 0.2.0: textová vrstva a Ctrl+C na jedné stránce, respektování oprávnění, bez OCR. Později zvážit vícestránkový výběr a složitější rozvržení/bidi.
- Hotovo v 0.2.0: hit-test a převody zoom/crop/obě otočení; regresní testy při 100% a simulovaném 200% DPI. Skutečné Windows monitory ověřit prakticky.
- Hotovo v 0.2.0: obdélníkový výřez do schránky/PNG/JPG, průběh, zrušení, ochrana originálu a atomické uložení.
- Uložit jako s aktuálními otočeními; nepřepisovat originál.
- Tisk: dialog tiskárny, rozsah, výpočet velikosti a zrušení mimo GUI vlákno.

## M3 – exporty

- Líně spuštěný samostatný exportní proces. Zrušení i při zavírání prohlížeče, žádný osiřelý proces.
- Hotovo v 0.3.0: JPG/PNG celé stránky, rozsah, DPI/kvalita, proces, průběh a zrušení. Windows sestavení nové verze čeká na ověření.
- UTF-8 TXT z existující vrstvy; vybrané stránky do PDF bez rasterizace.
- Obrázky: oddělit objektovou extrakci od renderu strany a vektorového výřezu. Původní formát zachovat, kde PDFium umožní získat vhodný dekódovatelný stream; ostatní transparentně označit jako PNG extrakci. Prověřit masky, alpha, inline images, form XObjects a opakovaně použitý objekt.
- Rozsah aktuální/vše/vlastní, validovat `1–5, 8, 12`, duplicity, obrácené či neplatné rozsahy.
- `Navod/Navod_001.jpg` s původním číslem strany, nejméně 3 číslice, index obrázku.
- Předběžná kontrola kolizí, žádný tichý přepis. Každý výstup přes dočasný soubor a atomický commit; jasný seznam hotových/zrušených/chybných.

## M4 – sestavení PDF

- Samostatné jednoduché okno, PDF/JPG/PNG, drag/drop, přirozené řazení, ruční změna pořadí a odstranění.
- A4 nebo velikost obrázku, orientace, okraje, fit bez ořezu.
- PDF import bez rasterizace se zachováním textu a vektorů. U JPEG přímé vložení streamu, pokud to možnosti jádra dovolí.
- Hesla zdrojů pouze v paměti, atomické uložení a ochrana před kolizí s některým vstupem.

## Později

- Vyhledávání.
- DOCX/DOC/ODT/XLSX/PPTX; nyní žádné LibreOffice ani jiná kancelářská runtime závislost.
- Aktualizace jen tlačítkem „Zkontrolovat aktualizace“. Žádný startovní ani periodický check. Nabídnout stažení/instalaci až po nalezení. Autorizace privátních releases bez sdíleného tokenu v binárce; návrh bezpečného individuálního přístupu před implementací.
- Ověřit potřebu podpory souborů >= 4 GiB a alternativního file-access API.
- Výměna renderovacího jádra až podle měření, ne předčasný plugin systém.
