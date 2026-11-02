# Pokračování složkou a jazyk – 0.5.0

## Pokračovat dalším PDF

Přepínač na hlavní liště je při každém spuštění aplikace vypnutý a jeho stav se neukládá. Po zapnutí lze dalším posunem vpřed za koncem aktuálního PDF otevřít další čitelný PDF soubor ze stejné složky. Funguje i pro jednostránkové PDF.

Samotné otevření, skok na poslední stránku, Ctrl+End, posunutí posuvníku či první dosažení spodního okraje dokument nepřepnou. V režimu celá stránka je spouštěčem další zářez kolečka na konci; při průběžném zobrazení musí další posun přijít až ve chvíli, kdy je posuvník dole. Tlačítko další stránky a PageDown mohou také explicitně požádat o další dokument za poslední stránkou. Zoom přes Ctrl a vodorovné rolování přes Shift nepřepínají. Poslední stránka musí mít načtený obraz, aby nebyla přeskočena během načítání.

Po přepnutí se otevře první stránka a zůstává režim šířka/celá stránka/vlastní zoom. Otočení a výběry patří původnímu dokumentu, a do nového se nepřenášejí. Zpracování dalšího posunu je na 600 ms pozastaveno a před dalším přepnutím je nutná nejméně 250 ms pauza v dopředných událostech; nativní ScrollMomentum samo další soubor neotevře. To brání přeskákání krátkých dokumentů jedním prudkým pohybem.

Názvy se porovnávají bez rozlišení velikosti písmen, číselné úseky podle hodnoty (1, 2, 10), shody mají stabilní rozlišení podle přesného názvu. Přípona .pdf může mít libovolnou velikost písmen. Složka se při požadavku prochází jednou, bez rekurze; pracovní vlákno hledá nejbližší větší název a nedrží seznam všech souborů. Nové/smazané soubory se tak projeví při dalším požadavku. Neexistuje watcher, přednačítání jiných PDF ani periodická kontrola.

Na konci složky se zobrazí informace, neprobíhá návrat na začátek. Chybný či zaheslovaný další dokument použije obvyklé hlášení nebo dotaz na heslo; soubory se automaticky nepřeskakují. Vypnutí přepínače nebo ruční otevření jiného dokumentu zneplatní čekající výsledek procházení složky.

## Jazyk / Language

Výchozí je čeština; angličtinu lze zvolit v menu Jazyk / Language → English. Volba se uloží a použije při novém spuštění. Aplikace kvůli změně jazyka neukončuje rozpracované úlohy ani znovu nenačítá dokument. Při startu se nastavení jazyka inicializuje před vznikem workerů a za běhu zůstává neměnné.

Vlastní nabídky, stavové zprávy, hledání, tisk, export, PDF z obrázků a vlastní chyby jádra/procesů mají anglický překlad. JSON katalog v src/i18n/en.json je zabudovaný v binárkách, bez Qt Linguist nebo síťové služby. Jazyk se předává pomocnému procesu v jeho JSON zadání; heslo zůstává pouze v stdin. Názvy formátů, JSON klíče a klávesové zkratky se nemění. Dialogy Windows, ovladače tiskáren a nízkoúrovňové chyby systému mohou používat jazyk operačního systému. Instalátor nabízí češtinu a angličtinu; aplikace má vlastní nezávislou volbu jazyka.

## Ověření

Linux Release (Qt 6.4.2, PDFium chromium/8125): 63 QtTest případů prošlo při 100% i simulovaném 200% DPI. Záznamy jsou v docs/validation/0.5.0. Ověřeno 151 překladových položek včetně zachování parametrů %1/%2. Regresní testy zahrnují přirozené řazení 1/2/10, příponu PDF velkými písmeny, ignorování podsložek, skutečný přechod ze dvou jednostránkových PDF, stav vypnuto po novém spuštění, konec složky bez smyčky, vypnutí před příchodem výsledku, přechod až za spodním okrajem, blokování momentum/Ctrl/Shift a anglické menu i chybu skutečného pomocného procesu. Windows 0.5.0 zatím vyžaduje ruční CI a uživatelskou zkoušku kolečka/touchpadu a změny jazyka.
