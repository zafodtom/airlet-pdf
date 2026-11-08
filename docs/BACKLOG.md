# Dohodnutý rozsah (2. 11. 2026)

## Hotové a ověřené uživatelem

- Rychlý prohlížeč, zoom, dočasné otočení a posun kolečkem po celých stránkách.
- Výběr textu, výřez a PNG/JPG export aktuální stránky, rozsahu i celého PDF.
- Windows 0.3.0: úspěšný běh #6, instalátor/portable a uživatelský test.

## 0.4.0 – ověřeno ve Windows i uživatelem

- Hledání textu na vyžádání, zvýraznění a další/předchozí nález, zrušení; bez OCR.
- Tisk přes systémový dialog a proces oddělený od prohlížení. Prakticky ověřit fyzickou tiskárnu, rozsahy, kopie a oboustranný tisk.
- Jedno nové PDF ze sady obrázků. Ruční pořadí se vztahuje jen na obrázky před vytvořením nového PDF.
- Windows CI #7 prošlo; uživatel potvrdil hledání, tvorbu PDF a tisk.

## 0.4.1 – ověřeno Windows CI #8

- Souvislé zvýraznění po slovech/řádcích pro hledání a výběr myší.
- Otevřít/Zavřít/Export a O aplikaci pouze v hlavním menu.
- Vlastní ikona aplikace i instalátoru.
- Per-user registrace pro PDF a volba otevřít Nastavení k potvrzení výchozí aplikace na konci instalace.

## 0.5.0 – ověřeno Windows CI #9

- Volitelný přechod na další PDF ze složky po explicitním posunu za koncem, přepínač při startu vždy vypnutý.
- Přirozené číselné řazení, žádná rekurze/watchery, kontrola zpožděných výsledků a potlačení doznívajícího kolečka.
- Čeština/angličtina v menu, uložená volba se projeví po restartu. Anglické vlastní dialogy i chyby procesových úloh; také anglická instalace.

## 0.6.0 – ověřeno Windows CI #11, publikováno

- Procházení PDF oběma směry; předchozí soubor se otevře na konci.
- Nastavení → Jazyk / Pokračovat dalším souborem / Aktualizace, lokalizované názvy.
- Ruční aktualizace instalované Windows edice z veřejných GitHub Releases, oddělené síťové UI a nativní instalátorový handoff. Bez startovních/periodických kontrol.
- Ruční workflow má volbu publish_release pro stabilní tag a balíčky; po prvním sestavení je potřeba ještě praktická zkouška 0.6.0 → vyšší verze na Windows.

## 0.6.1 – připraveno pro Windows sestavení

- Celá stránka při otevření každého PDF; označení aktivního režimu zobrazení.
- Pokračování soubory jednou společnou volbou v menu i na liště.
- Uživatelské README anglicky a česky, standardní O aplikaci, vlastní kód pod MIT.
- Praktický přechod 0.6.0 → 0.6.1 přes aktualizace čeká na Windows ověření.
- Podpis certifikátem odložen podle pokynu uživatele.

## 0.6.3 – oprava měkkého zobrazení

- Ostré dlaždice bez dodatečného škálování a vyhlazování, zarovnání na fyzické pixely displeje.
- Syntetické porovnání skutečně zobrazených pixelů s přímým rastrem PDFium, neceločíselný zoom, otočení a více dlaždic.
- Windows sestavení a kontrola výsledku na uživatelském dokumentu zbývají k ověření.

## Nyní nepřidávat

- Benchmark, panel náhledů/záložek/historie navigace.
- Změna pořadí, mazání a trvalé otočení existujících PDF stran.
- Extrakce původních vložených obrázků; současný renderovaný export postačuje.
- Historie otevřených dokumentů a zapamatování jejich pozice.
- OCR, kancelářské převody, cloud, automatické síťové kontroly a trvalá aktualizační služba.

Další funkce se určují až podle nových pokynů a zkušeností uživatele.
