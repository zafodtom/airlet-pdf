# Architektura

`MainWindow → PdfView → RenderService → PdfEngine ← PdfiumEngine`

UI vlastní pouze metadata, rozvržení a QImage cache. `PdfEngine` používá Qt hodnotové typy, nikoli PDFium handle. Další jádro změní továrnu, nikoliv viewer. Žádný plugin framework.

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

Statické anotace se vykreslují; interaktivní formuláře/XFA/JavaScript nejsou podporované. Výběr textu není v UI a správnost jeho souřadnic ve všech kombinacích bude nutné znovu ověřit při jeho zapojení. Počáteční cíl je rychlé prohlížení běžných PDF, skenů a technických výkresů.
