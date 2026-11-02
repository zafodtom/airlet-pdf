# Měření výkonu

Používat výhradně Release. Všechny hodnoty spojit s OS, CPU/RAM/GPU, DPI monitoru, verzí Qt/PDFium, verzí binárky a druhem dokumentu. Testovací dokumenty jsou syntetické; žádné privátní soubory v repozitáři.

## Co znamenají metriky

`--metrics-file=C:\...\run.json` zaznamená jednorázově časy od vstupu do main k volání show okna a prvnímu paintu obsahujícímu správný render PDF. Čas od požadavku otevření zahrnuje worker, PDFium, metadata, render a čekání na UI paint. Nejde o čas fyzického zobrazení pixelu na monitoru a nenahrazuje měření latence kompozitoru.

Samotný čas od main nezahrnuje loader OS. PowerShell harness měří navíc čas od spuštění procesu k pozorování výsledného souboru. Ten obsahuje režii spuštění a maximálně 25ms vzorkovací nepřesnost. Měření soubor vytváří pouze při explicitním parametru; běžný start nic neloguje.

```powershell
.\scripts\measure-windows.ps1 -Pdf C:\Testy\vykres.pdf -RunKind cold-after-reboot
.\scripts\measure-windows.ps1 -Pdf C:\Testy\vykres.pdf -RunKind warm
```

**Studený běh:** první spuštění po restartu Windows, předchozí neotevírání stejného PDF a aplikace; zapsat stav antiviru a disku. Harness cache sám nečistí. Parametr `cold-after-reboot` je popisek podmínek, ne automatické dosažení studené cache.

**Teplý běh:** zavřít aplikaci, opakovaně spustit stejnou binárku a PDF alespoň pětkrát, uvést medián a rozsah. Nikdy nesrovnávat Debug a Release ani Linux offscreen s cílovým Windows GUI.

## Praktická zkouška na cílovém PC

1. Jednostránkové běžné PDF, dlouhý dokument (500+ stran), skeny a velký A0 technický výkres. Ověřit první správný obraz, Ctrl+kolečko u detailu, fit, čtyři otočení.
2. Deset minut rychle listovat, měnit zoom a vracet se; zapisovat private bytes/working set. Raster cache má vlastní limit, PDFium a metadata jsou navíc.
3. Přepnout dokument a zavřít během vykreslování A0 a skenů. Starý obraz se nesmí objevit v jiném dokumentu. Po zavření nesmí zůstat proces.
4. Nechat obraz beze změn 30–60 s. Ve Správci úloh/Process Exploreru pozorovat CPU, ideálně WPR CPU sampling. Worker po konečné frontě čeká bez timeoutu. Krátký nulový vzorek CPU není důkaz absolutně nulového odběru.
5. Přesunout mezi 100/150/200% DPI monitory, prověřit ostrost, mezery dlaždic, kotvu zoomu a posuvníky. Souřadnice výběru budou zvlášť testovány při M2.

Testy ověřují, že po dokončení práce čítač render úloh zůstává stejný, a že změna session odfiltruje starý výsledek. To je důkaz absence nových render úloh v testovaném scénáři, nikoliv plná analýza všech OS/Qt událostí.

## Limity

Otevření/načtení stránky PDFium může být nepřerušitelné; zavření počká na návrat takového volání. Načítání rozměrů je lazy, délka scrollbarů se může upravovat. Interaktivní formuláře/XFA/JS a soubory >=4 GiB zatím nejsou cílem. Vyhledávání, výběr a export se během M1 výkonového měření nespouští.
