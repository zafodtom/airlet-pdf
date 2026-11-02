# Export stránek – 0.3.0

Tlačítko Export… nebo Ctrl+E nabízí aktuální, všechny nebo vlastní stránky (1–5, 8, 12), PNG/JPG, 72–600 DPI a kvalitu JPG 1–100. Výchozí je 150 DPI, JPG kvalita 92. Výstup vytvoří podsložku podle zdroje: Navod/Navod_001.png. Čísla odpovídají původním stranám, nejméně tři číslice. Použije se aktuální otočení stran v prohlížeči.

Rozsah se kontroluje v UI i workeru; duplicity se odstraní a stránky se seřadí. Před prvním výstupem se kontrolují všechny kolize. Existující obrázky se nepřepisují. Každá stránka se dokončí do soukromého dočasného adresáře a přesune bez přepisu na finální jméno. Při zrušení zůstávají dokončené soubory, pracovní adresář se odstraní. Chyba uvádí počet dokončených stran.

Samostatný konzolový pomocný proces AirletExport.exe vznikne pouze na vyžádání a přijme JSON přes stdin (včetně případného hesla, které není v argumentech ani logu). Má vlastní dokument, PDFium a paměť. Prohlížeč zůstává nezávislý. Při zrušení nebo zavření aplikace je proces ukončen a dočasné soubory uklizeny. Žádný pomocný proces není trvalý.

Limit jedné exportované strany je 64 megapixelů a 32768 px na osu; pro větší technické výkresy je potřeba snížit DPI. Exportuje se vzhled celé stránky, nikoliv originální stream vloženého obrázku. Textová vrstva a vektory se při PNG/JPG přirozeně rasterizují.

## Ověření

Linux Release, Qt 6.4.2, PDFium chromium/8125: 57 QtTest případů prošlo při 100% i simulovaném 200% DPI. Integrační test spouští skutečný samostatný EXE worker, ověřuje PNG/JPG, rozměry/otočení, původní číslování, šifrované PDF, neplatné rozsahy, ochranu již existujícího výstupu a ukončení po prvním dokončeném obrázku. Záznamy v docs/validation/0.3.0.

Windows 0.2.0 prošla CI. Windows 0.3.0 zatím není sestavená ani ověřená. Požadovaný další krok je nový ruční workflow na main.

Windows běh 0.3.0 #4 (41481428498) prošel kompilací, selhal ale v testech bez podrobného výstupu. Následná oprava odděluje konzolový worker AirletExport.exe od GUI a vynucuje testovací log do souboru, který CI vypíše. Příčina testového selhání není dosud potvrzená a opravu je třeba ověřit novým Windows během.
