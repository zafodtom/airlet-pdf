# Syntetické testovací dokumenty

Všechny soubory vznikly pro tento projekt lokálně pomocí ReportLab/Pillow a pypdf. Neobsahují skutečná uživatelská data a nemají síťové odkazy.

| Soubor | Obsah |
|---|---|
| basic.pdf | 3 strany 300×400 bodů, bílé/modré obdélníky, text |
| long.pdf | 500 stran 300×400 bodů, číslovaný text a úsečka |
| technical-a0.pdf | Velký list 3370×2384 bodů, hustá vektorová mřížka a text |
| scan.pdf | Vložený syntetický rastr, bez textové vrstvy |
| cropped-rotated.pdf | MediaBox 400×500, CropBox (80,120)–(240,360), Rotate=90, text a obdélník |
| encrypted.pdf | První strana basic.pdf, heslo `airlet-test` |

Tyto nástroje nejsou závislostí aplikace ani sestavení. Fixtures jsou již přiloženy a testy pouze čtou jejich PDF bytes.
