# Third-party notices / Oznámení a práva ke knihovnám

Airlet PDF is licensed under MIT for its own code. Third-party components retain their separate licenses. The package includes Qt Core, Gui, Widgets, PrintSupport and Network 6.8.3 (dynamic LGPLv3), PDFium chromium/8125 (BSD and bundled third-party licenses), and app-local Microsoft Visual C++ runtime DLLs. Qt license texts are in licenses/qt, the corresponding unmodified QtBase source archive is in sources, and PDFium notices are in licenses/pdfium. You may replace compatible Qt libraries and reverse-engineer the application to debug modifications to LGPL libraries; the MIT license does not restrict these rights. See the detailed notices below.

Airlet PDF používá dynamicky připojené Qt Core, Gui, Widgets, PrintSupport a Network 6.8.3 pod GNU LGPL verze 3 a PDFium chromium/8125 pod BSD a souvisejícími licencemi. Vlastní kód Airlet PDF je pod licencí MIT (soubor LICENSE vedle aplikace); licence MIT nemění podmínky přiložených knihoven. Není použito placené PDF SDK.

Qt: plné znění LGPLv3 a GPLv3 je v `licenses/qt`. Kompletní odpovídající zdroj QtBase 6.8.3 včetně závislostí a jejich oznámení je při Windows balení přiložen v `sources/qtbase-everywhere-src-6.8.3.tar.xz`. Archiv nebyl upravován. Jeho kontrolní součet je v `dependencies.lock.json`. Použité moduly jsou součástí QtBase; žádné GPL-only addony se nepřipojují.

Příjemce může Qt knihovny nahradit vlastními kompatibilními sestaveními. Aplikace nevynucuje podpis ani hash Qt DLL. Pro výměnu: zavřete Airlet PDF, zazálohujte adresář, nahraďte odpovídající `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6PrintSupport.dll`, `Qt6Network.dll` a potřebné pluginy kompatibilními knihovnami stejné architektury a ABI. Pro sestavení použijte přiložený QtBase zdroj s MSVC 2022 x64 a dynamickými knihovnami (`-shared -opensource -confirm-license -release -nomake examples -nomake tests`). Pokud jsou potřeba další závislosti pro vlastní Qt konfiguraci, použijte jejich odpovídající zdroje a licence v archivu. Reverzní inženýrství pro ladění úprav LGPL knihoven je výslovně dovoleno. Podmínky Airlet PDF tato práva neomezují.

PDFium: `licenses/pdfium` obsahuje licence PDFium, binární distribuce a všech oznámení dodaných s připnutým archivem: mimo jiné FreeType, ICU, HarfBuzz, libjpeg-turbo, libpng, zlib, OpenJPEG, LCMS a další. Při nové verzi převzít kompletní aktuální obsah `LICENSE` a `licenses/`, nikoliv jen BSD licenci PDFium.

MSVC runtime: Windows balení kopíruje redistribuovatelné DLL ze složky `VC/Redist/MSVC/.../x64/Microsoft.VC143.CRT` licencovaného VS 2022 prostředí vedle EXE. Nevyžaduje globální instalaci runtime ani správce. Nejde o samostatně prodávaný runtime. Před externí distribucí prověřit aktuální licenční podmínky použitého VS prostředí a obsah vytvořeného balíčku.

Instalátor je vytvářen Inno Setup (BSD-style licence jeho nástroje); neinstaluje Inno Setup na cílovém PC. Při distribuci kompilátoru samotného by bylo nutné přiložit i jeho licenci.

Zdroje a ověřené informace:

- https://www.qt.io/development/open-source-lgpl-obligations
- https://doc.qt.io/qt-6.8/licensing.html
- https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/8125
- https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8125/public/fpdfview.h

Licence MIT se vztahuje na vlastní kód Airlet PDF. Qt, PDFium a další přiložené součásti mají samostatné licence uvedené výše. Při předání binárního balíčku musí zůstat zachována všechna zde uvedená práva příjemce ke knihovnám a jejich licence.
