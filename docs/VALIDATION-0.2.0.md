# Ověření 0.2.0 – 2. 11. 2026

Základ: 0.1.0 úspěšně prošla Windows CI a uživatel potvrdil instalaci a velmi rychlou odezvu. Na základě jeho používání se mění krok kolečka a pokračuje se první částí M2.

## Implementace

- Celá stránka: kolečko přechází po celých stranách, dílčí úhlové kroky se sčítají. Při různých formátech se znovu přizpůsobí cílová stránka.
- Ostatní režimy: větší krok myši, přímý pixelový posun touchpadu, zachovaný Ctrl+zoom a prostřední tlačítko.
- Skutečný textový výběr tažením a Ctrl+C. Česká diakritika, zalomení řádků, výběr v obou směrech. Text se nenačítá při otevření ani při běžném renderu.
- Obdélníkový výřez do schránky nebo PNG/JPG, průběh a zrušení. Render po dlaždicích s předností prohlížení, kódování na samostatném líně spuštěném vlákně.
- Atomické uložení přes QSaveFile a ochrana před přepsáním zdrojového PDF či nepotvrzeného existujícího obrázku.
- Výběr je pouze na jedné stránce. Do PDF se nezapisují anotace ani změny otočení.

## Provedené testy

Ubuntu 24.04.3 LTS, GCC 13.3.0, Qt 6.4.2, PDFium chromium/8125, Release, Qt offscreen. 55 úspěšných QtTest případů při 100 % a 55 při QT_SCALE_FACTOR=2; 0 selhání a 0 přeskočení. Textové záznamy jsou v `docs/validation/0.2.0/`.

Nové kontroly zahrnují kroky kolečka a touchpadu, smíšené formáty stran, český víceřádkový text, všechny čtyři dodatečné rotace a dva zoomy pro běžnou i crop/vestavěně otočenou stránku, Ctrl+C přes rozhraní, shodu vícedlaždicového výřezu s přímým renderem, zrušení při změně dokumentu, PNG/JPG zápis, ochranu původního PDF, nepotvrzený přepis a zrušení bez hotového souboru. Zachovány jsou předchozí testy idle fronty, přepnutí dokumentu, cache a A0 viewportu.

Vizuálně zkontrolovány syntetické snímky hlavního okna, dočasného textového výběru a otočeného výřezu. Nejde o Windows GUI ověření.

## Zatím neověřeno

Windows běh #3, ID 41479675732, pro commit 59f7ec9 úspěšně dokončil kompilaci, automatické testy i tvorbu instalátoru. Artefakt AirletPDF-0.2.0-x64 má ID 13159656695. Interaktivní používání této verze uživatelem ještě nebylo potvrzeno.

Windows instalace, nové ovládání myší/touchpadem na cílovém PC, reálné HiDPI monitory a časové/CPU/paměťové měření ještě vyžadují praktickou zkoušku. Nejsou uváděna nová číselná Windows výkonová tvrzení.

## Následující práce

Uložení PDF s otočením a tisk; následně samostatný exportní proces pro celé strany, text, výběr PDF stran a vložené obrázky. Sestavování PDF patří do dalšího milníku. OCR, kancelářské převody, vyhledávání a updater zůstávají mimo tuto verzi.
