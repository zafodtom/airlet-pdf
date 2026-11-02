# Architektura

`MainWindow → PdfView → RenderService → PdfEngine ← PdfiumEngine`

UI vlastní metadata, rozvržení, QImage cache a dočasný výběr v PDF souřadnicích. `PdfEngine` používá Qt hodnotové typy, nikoli PDFium handle. Další jádro změní továrnu, nikoliv viewer. Žádný plugin framework.

## Vlastnictví a souběh

RenderService má jediný std::jthread. Na něm vznikne i zanikne jádro, dokument a QFile. Všechna PDFium volání včetně init/close chrání navíc globální rekurzivní mutex. Header připnuté verze 8125 říká, že žádné PDFium API není thread-safe a volání musí probíhat na jednom vlákně nebo být globálně serializována.

Otevření/uzavření a nová fronta zvýší atomickou generaci. Nejnovější fronta nahrazuje čekající požadavky; nevrství se události s celými render úlohami. PDFium IFSDK_PAUSE zachytí zneplatnění při progresivním renderu. Dokončený výsledek se kontroluje na workeru i GUI proti session a generaci. Změna dokumentu okamžitě vymaže cache. `FPDF_RenderPage_Close` se volá i při zrušení.

Některé operace PDFium (otevření, načtení stránky, jednotlivá dekódování) nelze přerušit uprostřed. Rozhraní zůstává responzivní, ale zavření aplikace může čekat na návrat této operace. Worker se při zavření vždy připojí; nemůže přežít proces.

Budoucí dlouhé exporty poběží v separátním procesu s vlastním dokumentem a PDFium knihovnou. Samotné druhé vlákno by buď porušilo pravidla PDFium, nebo by přes mutex blokovalo prohlížení. Exportní proces bude spouštěn až na vyžádání, s řízeným ukončením/zrušením.

## Render a klid

UI plánuje pouze při otevření, scrollu, resize, zoomu, otočení, změně obrazovky a upřesnění rozměru stránky. Neplánuje z paintEvent ani po každém výsledku. Jednorázová odložená událost sjednotí upřesnění rozměrů; žádný periodický timer.

Priorita: nízké rozlišení viditelných stran → ostré viditelné dlaždice 512×512 → nízké rozlišení jedné následující strany. Nízké rozlišení má max. 800×800 px. Cache ostrých dlaždic 96 MiB, cache malých obrazů 16 MiB; limity jsou automatické bez uživatelského nastavení. PDFium, fonty a metadata mají vlastní dodatečnou spotřebu, toto není limit celé aplikace.

Po vyprázdnění fronty `condition_variable::wait` čeká bez timeoutu. Žádné kontroly aktualizací, síť, polling či vlastní cyklus překreslování. Pozdější OS paint pouze znovu vykreslí cache.

## Souřadnice

PDFium vrací rozměry po ořezu a vestavěném otočení. Logické souřadnice rozhraní jádra mají počátek vlevo nahoře a jednotku bod (1/72 palce). Vlastní otočení tvoří 0–3 čtvrtotáčky. `Geometry` obsahuje inverzní transformace. PDFium `PageToDevice` převádí text z nativních PDF souřadnic včetně crop a vestavěného otočení. Render má fyzické pixely; widget logické pixely. Převod používá devicePixelRatioF. 100 % znamená 96 logických dpi / 72 bodů.

Rozměry neznámých stran se odhadnou podle první stránky, pak se opraví při návštěvě. Při opravě se zachová kotva na aktuální stránce. Posuvník proto u dokumentů s různými formáty může průběžně měnit délku; celý dokument se předem neskenuje. Qt scrollbary mají int rozsah; extrémní délky nad jeho rozsah jsou omezené. Souborový adaptér této PDFium API omezuje soubory na méně než 4 GiB.

## Omezení M1

Statické anotace se vykreslují; interaktivní formuláře/XFA/JavaScript nejsou podporované. Počáteční cíl je rychlé prohlížení běžných PDF, skenů a technických výkresů. Ve 0.2.0 je dočasný výběr textu a obdélníku na jedné stránce; není to anotace a nemění PDF.

## Výběr a výřez (0.2.0)

Text se načítá na workeru pouze po zahájení výběru, nikdy při běžném otevření/renderu. Cache drží nejvýše čtyři vyžádané stránky, aktivní výběr sdílí jejich data. Zpětný výsledek se kontroluje podle session i ID požadavku. Vektory glyph bounds jsou v neotočených crop-relative bodech; Geometry a Selection zajišťují převody do viewportu a hit-test. Při kopírování se zachová pořadí znaků PDFium a normalizují konce řádků. Respektuje se oprávnění pro kopírování textu. OCR není přítomné.

Výřez používá vlastní ID a document epoch, nezávislé na generaci běžného renderu. Standardní rozlišení je 144 dpi, automaticky omezené na 16 megapixelů a 8192 px na delší straně. Při skládání po 512px dlaždicích dostávají viditelné stránky přednost mezi jednotlivými dlaždicemi. Výsledný QImage obsahuje odpovídající DPI metadata. Schránka se mění jen v GUI vlákně po explicitním Ctrl+C.

PNG/JPG kódování a zápis má samostatný ImageSaveJob bez PDFium volání. Jthread se vytvoří až na vyžádání. QSaveFile bez direct-write fallback dokončí soubor atomicky; při zrušení nebo chybě se dočasný výstup nedokončí. Před přepsáním se vyžaduje souhlas pro konkrétní obrázek. PDF přípona i shoda cíle s původním PDF se odmítnou. Zrušení se ověří před dokončením; samotné Qt kódování nemusí jít přerušit uprostřed. Aplikace při ukončení připojí všechna vlastní vlákna.

Žádná část výběru nezavádí periodickou práci v klidu. Progress dialog existuje pouze během explicitní operace. Celá stránka používá krok kolečka 120 úhlových jednotek; běžný režim má viewportem odvozený singleStep bez animace. Pixelová data touchpadu se posouvají přímo.
