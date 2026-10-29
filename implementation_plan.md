# Oprava meziprotokolových přenosů (SFTP <-> FTP a cizí FS)

## Popis problému a příčiny

Při testování meziprotokolových přenosů v Open Salamanderu došlo ke dvěma problémům:
1. **Pád při kopírování ze SFTP na FTP**:
   - V metodě `CPluginFSInterface::CopyOrMoveFromFS`: Pokud cílová cesta není disková cesta Windows (`X:\...` nebo `\\server\...`), stávající kód předpokládal, že se jedná o SFTP URL (`if (!diskPath)`). Při cíli `ftp://...` se kód pokusil navázat SFTP spojení přes SSH s FTP serverem a došlo k pádu aplikace.
   - **Správné chování dle specifikace Salamanderu (`spl_fs.h`)**: Pokud cíl nepatří SFTP pluginu a není to disková cesta, v `mode == 2` musí plugin vrátit `operationMask = TRUE; cancelOrHandlePath = TRUE; return FALSE;`. Salamander tím rozpozná cizí FS plugin (např. FTP) a sám zařídí dvoukrokový mezipřenos přes dočasnou diskovou složku (`%TEMP%`).

2. **Chyba "nepodařilo se vytvořit soubor" při kopírování z FTP na SFTP**:
   - Salamander stáhl soubor z FTP do dočasné diskové složky `%TEMP%\samXXXX.tmp` a zavolal `CopyOrMoveFromDiskToFS` na SFTP pluginu.
   - V `CopyOrMoveFromDiskToFS` nastaly závažné problémy:
     a) **Asynchronní návrat vs. smazání dočasných souborů**: `CopyOrMoveFromDiskToFS` spustil asynchronního workera a ihned vrátil `TRUE`. Salamander po obdržení `TRUE` okamžitě smazal dočasnou diskovou složku `%TEMP%\samXXXX.tmp` dříve, než worker stihl soubor otevřít a nahrát! Pokud byla instance FS navíc vytvořena nově (`OpenFS`), Salamander ihned zavolal `CloseFS`, což zničilo instanci FS i worker.
     b) **Inicializace profilu a hostitele**: V `CopyOrMoveFromDiskToFS` nebyla volána inicializace profilu podle `targetPath` (`SftpParseHostInto`). Pokud byl FS vytvořen nově nebo cílová cesta směřovala na jiného hostitele/profil, profil nebyl platný.
     c) **Vytvoření chybějícího vzdáleného adresáře**: Pokud v cílové cestě neexistoval vzdálený adresář, `libssh2_sftp_open` selhal s `LIBSSH2_FX_NO_SUCH_FILE` ("Failed opening remote file").
     d) **Chybové hlášení SFTP**: `CSftpConnection::SetError` hlásila jen obecnou chybu ze session místo konkrétního SFTP chybového kódu z `libssh2_sftp_last_error(Sftp)` (např. Permission denied, No such file, atd.).

---

## Navrhované změny

### 1. `src/fs2.cpp`
- **V `CopyOrMoveFromFS`**:
  - Přidat detekci cílové cesty: ověřit, zda cíl skutečně patří SFTP protokolu (`sftp:` nebo náš `fsName` nebo relativní cesta v rámci aktivního SFTP připojení).
  - Pokud cíl **nepatří SFTP pluginu** a **není to disková cesta** (např. začíná `ftp:` nebo jiným schématem):
    - V `mode == 2` nastavit `operationMask = TRUE; cancelOrHandlePath = TRUE; return FALSE;`, aby Salamander sám převzal řízení mezipřenosu přes dočasnou diskovou složku.
  - V `mode == 3` (když Salamander stáhne soubory do `%TEMP%` pro jiný plugin / archiv):
    - Stahování do diskového `%TEMP%` musí doběhnout před návratem z `CopyOrMoveFromFS`, aby soubory na disku existovaly dříve, než Salamander předá řízení cílovému pluginu.
- **V `CopyOrMoveFromDiskToFS`**:
  - Vyparsovat hostitele a profil z `targetPath` pomocí `SftpParseHostInto` (pokud `Profile` není nastaven nebo se liší od cíle).
  - Vyčistit a normalizovat `remoteDir` ze zadané `targetPath`.
  - Pokud je operace volána ze Salamanderova dočasného adresáře (nebo na dočasné instanci FS):
    - Před návratem z metody počkat na dokončení přenosu workeru (`dlgThread->WaitForExit()` / zprávy dialogu), aby Salamander nesmazal `%TEMP%` složku a nezavolal `CloseFS` během probíhajícího nahrávání!
    - Po dokončení vrátit `TRUE` pouze při úspěchu (pokud došlo k chybě nebo stornu, vrátit `FALSE` a nastavit `*invalidPathOrCancel = TRUE`).

### 2. `src/sftpconn.cpp`
- **V `CSftpConnection::SetError` a `CSftpConnection::Upload`**:
  - Zahrnout `libssh2_sftp_last_error(Sftp)` do chybového hlášení pro přesnou diagnostiku (např. `LIBSSH2_FX_PERMISSION_DENIED`, `LIBSSH2_FX_NO_SUCH_FILE`, `LIBSSH2_FX_FAILURE`).
  - V `Upload`: Pokud otevření souboru selže na chybějící cestě (`LIBSSH2_FX_NO_SUCH_FILE`), pokusit se automaticky vytvořit rodičovský adresář na serveru a zkusit otevřít znovu.
- **Oprava pádu přenosu na `Writing remote file: Timed out waiting on socket` [DOKONČENO ve v1.4.0]**:
  - V `src/libssh2/session.c` v `_libssh2_wait_socket` opraveno chybné vyhodnocení timeoutu `select()`, kdy vypršení periody keepalive bez nastaveného `api_timeout` způsobovalo shození spojení.
  - V `CSftpConnection::Connect` nastaven `libssh2_session_set_timeout(Session, 60000)` a `TCP_NODELAY`.
  - Vytvořen a úspěšně otestován unit test `test/test_wait_socket_timeout.cpp`.

---

## Verifikační plán

### Automatizované testy
- Kompilace pluginu:
  `MSBuild src\vcxproj\sftp.vcxproj /p:Configuration=Release /p:Platform=x64`
- Spuštění existujících testů:
  `./test_worker.exe`
  `./test_path_hottrack.exe`

### Manuální verifikace
- Hot-deploy do `C:\Apps\samandarin\plugins\sftp\sftp.spl`.
- Otestování v Salamanderu:
  1. Kopírování z FTP do SFTP: soubory se stáhnou z FTP do TEMP a následně nahrají na SFTP, žádné hlášení o nemožnosti vytvořit soubor.
  2. Kopírování ze SFTP do FTP: žádný pád, Salamander korektně stáhne soubory ze SFTP do TEMP a nahraje je na FTP.
  3. Běžné kopírování ze SFTP na lokální disk a obráceně zůstává funkční bez regrese.
