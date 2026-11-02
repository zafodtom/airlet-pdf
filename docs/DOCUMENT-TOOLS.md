# Hledání, tisk a PDF z obrázků – 0.4.0

## Hledání

Ctrl+F nebo Úpravy → Hledat. Po 300 ms bez další změny dotazu se jednorázově spustí pomocný proces, který prochází textové vrstvy po stránkách. Písmena se hledají bez rozlišení velikosti, diakritika se zachovává. Bílá místa včetně konců řádků se normalizují na mezery. Enter a tlačítka Další/Předchozí cyklicky přecházejí mezi nálezy; aktuální nález je oranžově zvýrazněný i při zoomu a otočení. Escape zavře dialog a zruší hledání, tlačítko Zastavit ponechá dosavadní výsledky. Změna dotazu či dokumentu zruší starý proces a odpojí jeho výsledky.

Limit je 256 znaků dotazu a 2000 nálezů, poté je potřeba upřesnit dotaz. Data jedné stránky se po prohledání uvolní. Souřadnice výsledků jsou v PDF bodech po crop/vestavěném otočení; worker předá také skutečnou velikost stránky, aby fungoval skok do dosud nenavštívených stran jiného formátu. U složitých vícesloupcových dokumentů pořadí textu odpovídá PDFium. Bez textové vrstvy se zobrazí informace o absenci OCR. Oprávnění ke získání textu se respektuje.

Od 0.4.1 se znaky aktuálního nálezu a výběru myší slučují do souvislých pruhů po řádcích; mezery mezi slovy nemají díry a překryté glyph boxy nezpůsobují tmavší skvrny. Mezi samostatnými řádky/sloupci se obdélníky nepropojují. Geometrie pruhů se vypočítává při změně výběru či nálezu, nikoli při každém překreslení; souřadnice stále respektují zoom a otočení. Výběr myší používá barvu zvýraznění systémové palety.

## Tisk

Ctrl+P nebo Soubor → Tisk otevře systémový dialog. Podporuje aktuální stránku, všechny nebo rozsah, papír/orientaci, okraje, barevnost, pořadí, kopie a duplex podle možností tiskárny. Dokument se znovu otevře až v pomocném procesu. Aktuální dočasná otočení se použijí i při tisku. Každá stránka se přizpůsobí do tisknutelné oblasti bez ořezu a se zachováním poměru stran.

V této verzi se tisk rasterizuje po dlaždicích 512 px při 300 DPI (150 DPI, pokud dokument dovoluje pouze nízkou kvalitu tisku). Nejde o vektorový přenos PDF na tiskárnu. Proces průběžně hlásí dokončené stránky. Zrušení jej ukončí; stránky již předané systému mohou zůstat v tiskové frontě, odkud je lze případně zrušit. Zpráva o úspěchu znamená předání úlohy tiskovému systému, nikoliv potvrzení fyzického vytištění. Pro tisk do PDF se používá pracovní adresář a nový cílový název, existující soubory se nepřepisují.

## PDF z obrázků

Soubor → PDF z obrázků funguje i bez otevřeného dokumentu. Výběr PNG/JPG/JPEG/BMP; jeden obrázek vytvoří jednu stránku. Výchozí řazení je podle názvů s číselným porovnáním, seznam lze přeuspořádat tažením i Nahoru/Dolů a položky odebrat. Vstupní soubory se nemění.

Volby: velikost podle obrázku s DPI 72–600 (výchozí 150), A4 na výšku nebo A4 na šířku s okraji 10 mm. Obrázek se vejde celý bez ořezu, průhlednost má bílé pozadí a zohlední se orientace EXIF. V této verzi se obrázky dekódují a vloží přes QPdfWriter; původní komprimovaný JPEG stream se nezaručuje. Nejvýše 10000 vstupů, jeden obrázek do 64 megapixelů a 32768 px na osu. Obrázky se načítají postupně mimo GUI.

Převod má průběh a zrušení. PDF se sestaví v dočasném souboru na cílovém disku a teprve po dokončení se přejmenuje přes vlastní QTemporaryFile bez přepisu. Při chybě či zrušení se pracovní adresář odstraní. Zdrojové obrázky ani existující PDF se nepřepíší. Nové PDF není automaticky otevřeno, aby se nepřerušilo aktuální prohlížení.

## Ověření

Linux Release (Qt 6.4.2, PDFium chromium/8125): všech 59 QtTest případů prošlo při 100% i simulovaném 200% DPI; záznamy jsou v docs/validation/0.4.0. Regresní sada ověřuje hledání v češtině a přes konec řádku, hesla a skeny bez textu, skutečný worker, barevné pořadí i rozměry obrázků v PDF, formát A4, kolize, neplatné vstupy, tiskový rozsah přes QPrinter PDF backend a zrušení tvorby PDF. UI test ověřuje další nález a změnu dokumentu. Windows 0.4.0 zatím není ověřená a skutečný tisk na fyzické tiskárně zde nebyl proveden.

## 0.4.1 – instalace a vzhled

Otevřít/Zavřít/Export jsou pouze v nabídce Soubor, O aplikaci v Nápovědě. Zkratky zůstávají. Ikona je vlastní bílý list s tyrkysovým motivem letu; SVG zdroj, PNG pro Qt a vícerozlišením ICO pro Windows jsou v assets. Vývojový scripts/make-icon.py obnoví PNG/ICO pomocí Pillow; v aplikaci ani sestavení není Python potřebný.

Instalátor zapisuje per-user Capabilities, RegisteredApplications a ProgID AirletPDF.Document a nabídne na konci instalace otevření systémové volby. Windows 11 používá ms-settings:defaultapps?registeredAppUser=Airlet%20PDF; Windows 10 ms-settings:defaultapps, kde lze vybrat příponu .pdf a Airlet PDF. Změnu musí potvrdit uživatel ve Windows. Nezapisuje se UserChoice ani jeho hash, nesahá se na cizí asociace; odinstalace odstraňuje jen vlastní registraci. Tichá instalace Nastavení neotvírá. Viz [Microsoft – výchozí aplikace](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings).

0.4.0 prošla Windows CI #7 (41488401076) a uživatelským testem hledání/tvorby/tisku. Aktualizace 0.4.1 má lokální regresní testy pruhů, oddělení řádků/sloupců a vestavěného otočení, ikony i obsahu lišty; samotný RC prostředek, instalátor a otevření nastavení vyžadují nové Windows ověření.
