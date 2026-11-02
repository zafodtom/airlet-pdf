# Soukromý repozitář a Windows sestavení

Repozitář: https://github.com/zafodtom/airlet-pdf

Dne 2. 11. 2026 bylo přes GitHub připojení ověřeno, že repozitář je **soukromý**, má výchozí větev `main` a účet `zafodtom` má oprávnění admin/push. Před prvním nahráním neměl žádné větve ani obsah. Repozitář založil vlastník; připojení samo operaci pro vytváření repozitářů nemá.

Implementace se nahrává do tohoto repozitáře. Žádný jiný projekt ani existující uživatelský obsah se nepřepisuje. Do Git nepřidávat `.deps`, `build`, soukromá PDF ani výkonové záznamy s názvy soukromých souborů.

## Spuštění Windows sestavení

https://github.com/zafodtom/airlet-pdf/actions/workflows/windows-release.yml

Workflow **Windows Release (manual)** se spouští pouze přes **Run workflow**, větev `main`. Neexistuje spouštění při push/PR ani plánované běhy. Úspěšný běh vytvoří artefakt `AirletPDF-0.1.0-x64` s instalátorem, přenosným ZIPem a kontrolními součty; uchování je 3 dny. Zdroj Qt je uvnitř distribučních balíčků.

Dostupné GitHub připojení umožňuje zápis kódu a čtení výsledků Actions, nemá však operaci workflow dispatch. Spuštění proto vyžaduje webové rozhraní GitHubu nebo autorizované lokální CLI:

```powershell
gh workflow run windows-release.yml --repo zafodtom/airlet-pdf --ref main
```

Ruční spuštění vyžaduje přihlášení účtu s přístupem k repozitáři. Samotná přítomnost workflow není potvrzení úspěšného Windows sestavení. Skutečné ověření je v [VALIDATION.md](VALIDATION.md).
