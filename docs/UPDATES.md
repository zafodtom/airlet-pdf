# Ruční aktualizace – 0.6.0

## Použití

Nastavení → Aktualizace… (Settings → Updates…) otevře vlastní aktualizační dialog a ručně zkontroluje poslední stabilní veřejný GitHub Release. K dispozici je aktuální stav a nové číslo verze. Tlačítko Stáhnout aktualizaci stáhne instalátor do dočasné složky s průběhem, poté se ověří velikost a SHA-256. Do té doby se aplikace ani instalace nemění. Zavření dialogu zruší stahování a odstraní soubory.

Tlačítko Restartovat a aktualizovat vyžaduje potvrzení. Následně se viewer zavře, nativní helper počká na jeho ukončení, podruhé ověří SHA-256 a provede Inno Setup velmi tiše do stejné složky. Po dobu instalace zůstává viditelné malé vlastní okno Airletu s informací o průběhu; uživatel neprochází průvodcem instalátoru. Helper neběží z instalační složky a nemá závislost na Qt DLL, takže jejich výměnu neblokuje. Po úspěchu se Airlet znovu otevře se stejnou cestou PDF; přechodné výběry, otočení a pozice se neobnovují. Nastavení jazyka zůstává. U zaheslovaného PDF bude heslo vyžádáno znovu, nepředává se upgraderu.

Aktualizace jsou v této verzi určené pro instalovanou Windows edici. Portable má srozumitelnou informaci o ruční výměně ZIP; nepřevádí se bez souhlasu na instalovanou aplikaci. Během exportu, tisku či tvorby PDF nelze aktualizační dialog otevřít. Po dobu dialogu je hlavní okno zakázáno, takže nevznikne nová úloha těsně před instalací. Windows se automaticky nerestartuje; případný požadavek na restart se zobrazí.

## Síť a důvěryhodnost

Výchozí proces vieweru neodkazuje na QtNetwork (ověřeno lokálním seznamem závislostí). QtNetwork a TLS se používají pouze v AirletUpdate.exe spuštěném přes menu; po zavření dialogu zaniknou. Bez kontroly při startu, časovače pro periodickou kontrolu, účtu, tokenu nebo ukládání hesla. PDF ani jeho obsah se na síť neposílají.

Zdroj: https://api.github.com/repos/zafodtom/airlet-pdf/releases/latest. Jsou povolené pouze stabilní vyšší verze X.Y.Z, žádné drafty/prerelease/downgrade. Verze z tagu musí odpovídat manifestu. Odkazy na instalátor a manifest musejí být HTTPS GitHub assety přesně v zafodtom/airlet-pdf a daném tagu; přesměrování povoluje pouze explicitní GitHub hosty. TLS chyby se neignorují. Metadata mají limit 2 MiB, manifest 64 KiB, instalátor 512 MiB, síťový čtecí buffer 256 KiB, timeout bez přenosu 30 s a nejvýše 8 přesměrování. Stahování se zapisuje průběžně, nedrží celý instalátor v paměti.

SHA-256 a přesná velikost z manifestu se ověřují po stažení. Je-li k dispozici také GitHub asset digest, musí se shodovat. Nativní helper po ukončení aplikace ověřuje skutečný soubor znovu přes Windows BCrypt a drží otevřený handle bez sdílení zápisu/mazání až do konce instalace. Jde o integritu proti manifestu získanému přes HTTPS od vydavatele, nikoli nezávislý Authenticode podpis instalátoru. Podpisový certifikát není v projektu zaveden.

Stažený instalátor se po pokusu o instalaci odstraní. Malá kopie helperu nemůže za běhu smazat vlastní EXE; označí dokončenou složku a při další ruční kontrole se dokončené složky starší pěti minut odstraní. Aktivní stažení se neuklízejí. Při chybě před předáním helperu uklízí QTemporaryDir parent procesu.

## Publikace nové verze

Jednorázově nainstalovat 0.6.0 běžným instalátorem; starší verze nemají klienta aktualizací. Při dalším vydání:

1. Zvýšit verzi v CMake, installeru, skriptu, workflow a poznámkách k vydání.
2. Spustit ruční Windows workflow na main a zaškrtnout publish_release. Výchozí hodnota je false, prosté testovací sestavení nic nezveřejňuje.
3. Po testech a balení vznikne manifest z opravdového instalačního EXE: schema 1, version, installer.name/size/sha256. Workflow vytvoří nový tag vX.Y.Z a stabilní release s EXE, portable ZIP, SHA256SUMS.txt a airlet-update.json. Existující release se nepřepisuje.
4. V nainstalovaném Airletu zvolit Nastavení → Aktualizace. Artefakty Actions slouží pouze testům, aplikace z nich nikdy neaktualizuje.

Dokud neexistuje první zveřejněný stabilní release, dialog zobrazí tuto informaci bez pádu. Před prvním vydáním 0.6.0 API nepotvrdilo žádné existující releases; první publikace je součástí nového ručního workflow.

## Ověření a zbývající praktická zkouška

Linux Release: všech 65 QtTest případů prošlo při běžném i simulovaném 200% DPI; protokoly jsou v docs/validation/0.6.0. Syntetické testy kontrolují porovnání verzí, zákaz downgradu/prerelease, hosty/cesty/HTTPS, duplicity assetů, shodu velikosti, názvu, verze a hashů. Windows sada navíc spouští skutečný nativní BCrypt verifier nad známým souborem a odmítá změněný hash. Běžné testy nestahují nic z internetu a neprovádějí instalaci.

Je nutné provést Windows CI a pak na cílovém PC ověřit celý přechod z 0.6.0 na vyšší stabilní verzi (např. 0.6.1), zavření dialogu během stahování, zachování jazyka/asociace PDF a znovuotevření dokumentu. Bez tohoto běhu není kompletní instalace aktualizace označená jako prakticky ověřená.

Primární dokumentace: [GitHub Releases REST](https://docs.github.com/en/rest/releases/releases), [Qt přesměrování](https://doc.qt.io/qt-6.8/qnetworkrequest.html), [Inno Setup parametry](https://jrsoftware.org/ishelp/topic_setupcmdline.htm).
