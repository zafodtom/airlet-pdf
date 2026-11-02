# Soukromý repozitář a Windows sestavení

Repozitář: https://github.com/zafodtom/airlet-pdf

Dne 2. 11. 2026 bylo přes GitHub připojení ověřeno, že repozitář je **soukromý**, má výchozí větev `main` a účet `zafodtom` má oprávnění admin/push. Před prvním nahráním neměl žádné větve ani obsah. Repozitář založil vlastník; připojení samo operaci pro vytváření repozitářů nemá.

Implementace je nahraná v tomto repozitáři. Žádný jiný projekt ani existující uživatelský obsah se nepřepisuje. Do Git nepřidávat `.deps`, `build`, soukromá PDF ani výkonové záznamy s názvy soukromých souborů.

## Spuštění Windows sestavení

https://github.com/zafodtom/airlet-pdf/actions/workflows/windows-release.yml

Workflow **Windows Release (manual)** se spouští pouze přes **Run workflow**, větev `main`. Neexistuje spouštění při push/PR ani plánované běhy. Úspěšný běh vytvoří artefakt `AirletPDF-0.3.0-x64` s instalátorem, přenosným ZIPem a kontrolními součty; uchování je 3 dny. Zdroj Qt je uvnitř distribučních balíčků.

Dostupné GitHub připojení umožňuje zápis kódu a čtení výsledků Actions, nemá však operaci workflow dispatch. Spuštění proto vyžaduje webové rozhraní GitHubu nebo autorizované lokální CLI:

```powershell
gh workflow run windows-release.yml --repo zafodtom/airlet-pdf --ref main
```

Ruční spuštění vyžaduje přihlášení účtu s přístupem k repozitáři. Samotná přítomnost workflow není potvrzení úspěšného Windows sestavení. Skutečné ověření je v [VALIDATION.md](VALIDATION.md).

## Ověřený Windows balíček 0.1.0

[Běh #2](https://github.com/zafodtom/airlet-pdf/actions/runs/41471603485) pro commit `3a61da0` úspěšně dokončil Release kompilaci, automatické testy a Inno Setup. [Artefakt ke stažení](https://github.com/zafodtom/airlet-pdf/actions/runs/41471603485/artifacts/13154494974) je dostupný do 5. 11. 2026. Uvnitř je `AirletPDF-Setup-0.1.0-x64.exe`, přenosný ZIP a SHA256SUMS.txt. Zatím nebyla provedena skutečná interaktivní instalace na cílovém PC.

## Připravená 0.2.0

Aktuální větev main obsahuje 0.2.0 s upraveným kolečkem, textovým výběrem a výřezem. Nový Windows build je třeba spustit přes Run workflow pro main. Nepoužívat opakování staršího běhu: to zachovává původní commit. Dosavadní hotový instalátor 0.1.0 není balíček nové verze.

## 0.3.0

0.2.0 úspěšně prošla během #3 (41479675732). Aktuální main obsahuje 0.3.0 s exportem celých stránek do PNG/JPG. Pro její instalátor je nutný nový Run workflow pro main.
