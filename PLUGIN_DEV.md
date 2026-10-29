# Návod na vývoj pluginů pro Open Salamander (SFTP Plugin Reference & Best Practices)

Tento dokument slouží jako přehled architektury, vývojových pravidel a osvědčených postupů při vývoji a údržbě pluginu **SFTP/SCP pro Open Salamander** (x64).

---

## 1. Architektura a adresářová struktura pluginu

Plugin do Salamandera se skládá z:
- **`sftpx.spl`** – Dynamická knihovna pluginu (Windows DLL přejmenovaná na `.spl`).
- **`english.slg`, `czech.slg`** – Jazykové knihovny s přeloženými texty a dialogy.

### Adresářová struktura:

```
salamander-sftpx-plugin/
├── src/
│   ├── sftp.cpp            # Hlavní vstup, SalamanderPluginEntry2, registrace FS
│   ├── sftp.h              # Společné deklarace rozhraní a struktur
│   ├── sftp.def            # Exportní definice
│   ├── sftpconn.cpp/.h     # Komunikační vrstva nad libssh2 (SFTP + SCP)
│   ├── sftpglue.cpp/.h     # Propojení mezi Salamander FS a CSftpConnection, práce s cestami
│   ├── fs1.cpp             # Správa relací, přihlašovací dialog, ExecuteOnFS
│   ├── fs2.cpp             # Implementace CPluginFSInterface (ChangePath, List, Copy, Delete, chmod)
│   ├── menu.cpp            # Obsluha položek menu
│   ├── dialogs.cpp/.h      # Dialogy konzole pro spouštění příkazů
│   ├── precomp.h           # Precompiled headers se Salamander SDK
│   └── lang/
│       ├── lang.rh         # Společné resource ID pro jazyky
│       └── lang.rc         # Texty a dialogy
├── test/                   # Unit testy pro nezávislou engine vrstvu
├── libcrypto-3-x64.def     # Exporty pro generování import lib Salamanderova libcrypto
├── README.md               # Dokumentace (EN)
├── README_CZ.md            # Dokumentace (CZ)
├── jobs_done.md            # Přehled dokončených úkolů
└── implementation_plan.md  # Implementační plán
```

---

## 2. Navigace v adresářovém stromu a zachování fokusu

Při navigaci do nadřazeného adresáře (`..`, `isDir == 2`) v `ExecuteOnFS` je nutné:
1. Získat aktuální cestu `fs->Path` a normalizovat ji (`SftpNormalize`).
2. Odstranit případné koncové lomítko.
3. Najít poslední komponentu cesty (název opouštěné podsložky).
4. Vypočítat cestu k nadřazenému adresáři (`SftpParent`).
5. Předat název opouštěné podsložky jako parametr `suggestedFocusName` do `SalamanderGeneral->ChangePanelPathToPluginFS`.

Tím Salamander automaticky nastaví kurzor/focus na složku, ze které uživatel právě vystoupil.

---

## 3. Sestavení a testování

### Kompilace přes MSVC (MSBuild):
```powershell
MSBuild src\vcxproj\sftp.vcxproj /p:Configuration=Release /p:Platform=x64
```
SDK se bere z `..\..\samandarin\src` (přepíše se přes `/p:SalamanderSdkDir=`).

### Spuštění testů:
```powershell
test\build_test_path_join.bat   (z VS x64 Developer Prompt; ostatní testy v test\ se kompilují obdobně přes `cl`)
```

---

## 4. Udržování spojení a detekce odpojení (Keepalive & Auto-Reconnect)

Pro zajištění stability spojení na nestabilních sítích a proti timeoutům routerů/firewallů a serverů typu TrueNAS / OpenSSH:
1. **TCP Keepalive**: Socket má aktivovaný `SO_KEEPALIVE` s nastavením `SIO_KEEPALIVE_VALS` (15 s nečinnost, 5 s interval opakování).
2. **Proaktivní FS Timer Keepalive**: V `ChangePath` je registrován časovač `SalamanderGeneral->AddPluginFSTimer(8000, this, SFTP_TIMER_KEEPALIVE)`. Během nečinnosti uživatele (kdy Salamander čeká v message loop) se každých 8 s volá `SendKeepalive()`, což odbavuje `ClientAliveInterval` dotazy serveru (např. TrueNAS) a posílá keepalive sondu s `want_reply = 0`.
3. **Detekce stavu v `IsConnected()`**: Neblokující kontrola `select` s `recv(MSG_PEEK)` detekuje vzdálené uzavření spojení (FIN), reset (RST) nebo síťovou chybu ještě před zahájením další operace.
4. **Transparentní Reconnect & Retry**: `SftpEnsureConnected()` při zjištění odpojení automaticky obnoví spojení. Operace čtení adresářů `ListCurrentPath` při selhání provede transparentní znovunavázání a opakování operace.

---

## 5. Výpočet velikosti složek na serveru (Calc Size) a klávesové zkratky

Při výpočtu velikosti složek (`Calculate Size (server)`):
1. **Server-side optimalizace (`FastDirSize`)**: Nejprve se pokusí spustit rychlý výpočet na serveru přes SSH exec (`du -sb` / `du -sk` + `find`), což proběhne v milisekundách bez stahování výpisu souborů po síti.
2. **Bezpečný fallback na SFTP rekurzi**: Pokud server neumožňuje spuštění shellových příkazů, proběhne rekurzivní procházení podsložek s ochranou proti cyklení na symbolických odkazech.
3. **Nezamrzající dialog s Cancel**: Po celou dobu běhu je zobrazen dialog s průběžným stavem skenování a možností výpočet kdykoliv zrušit (klávesa Escape / tlačítko Storno).
4. **Aktualizace panelu**: Vypočtená velikost se zapíše do `CFileData` a panel se okamžitě překreslí.
5. **Klávesová zkratka `Ctrl+Shift+F10` & Kontextové menu**:
   - V `sftp.cpp` je pro položku `Calculate &Size (server)` registrována zkratka `SALHOTKEY(VK_F10, HOTKEYF_CONTROL | HOTKEYF_SHIFT)`.
   - V `fs2.cpp` (`ContextMenu()`) a v `GetSupportedServices()` (`FS_SERVICE_CALCULATEOCCUPIEDSPACE`) jsou standardní příkazy Salamandera `SALCMD_CALCDIRSIZES` a `SALCMD_OCCUPIEDSPACE` povoleny a přesměrovány na obsluhu `MENUCMD_CALCSIZE`.
6. **Řešení mezerníku (Spacebar) přes Windows Message Hook**:
   - V jádře Open Salamandera (`fileswn0.cpp:1082-1087`) je stisk mezerníku (`VK_SPACE`) pro pluginy (`ptPluginFS`) označen poznámkou `// to be implemented` a stisk mezerníku pouze invertuje výběr položky jako klávesa Insert bez volání pluginu.
   - K překonání tohoto omezení plugin instaluje vláknový Windows Message Hook (`SetWindowsHookEx(WH_GETMESSAGE, GetMsgHookProc, NULL, GetCurrentThreadId())`).
   - Hook zachytí `WM_KEYDOWN` s `VK_SPACE`, ověří, že uživatel nepíše do editboxu a že je aktivní panel s naším pluginem (`InterfaceForFS.IsOurFS(activeFS)`).
   - Zprávu zkonzumuje (`pMsg->message = WM_NULL`) a provede `SftpOnSpacePressedOnFolder`: invertuje výběr složky (`SelectPanelItem`), spočítá velikost přes `FastDirSize`, zapíše velikost do `CFileData`, překreslí panel (`RepaintChangedItems`) a posune kurzor na další položku.

---

## 6. Statické linkování a distribuce na jiné počítače

Pro maximální přenositelnost bez nutnosti instalovat VC++ runtime:
- **Statické runtimes**: projekt používá `/MT` (statické CRT), takže není potřeba VC++ redistributable.
- **Statický libssh2**: Slinkován přímo do `sftpx.spl` ze zdrojů `src/libssh2` (OpenSSL backend) (není potřeba žádná externí `libssh2.dll` ani `z.dll`).
- **Knihovna OpenSSL (`libcrypto-3-x64.dll`)**: `sftpx.spl` importuje `libcrypto-3-x64.dll` standardním PE importem. V Open Salamandru se tato knihovna nachází přímo v kořenovém adresáři aplikace (`C:\Apps\samandarin\libcrypto-3-x64.dll`). Starý kód `LoadBundledLibssh2()`, který ji načítal explicitně s `LOAD_WITH_ALTERED_SEARCH_PATH`, byl odstraněn, protože vedl k duplicitnímu načtení druhého OpenSSL runtime a poškození heapu (`0xc0000374`).
- **Ověření závislostí**:
  ```powershell
  dumpbin /dependents sftpx.spl
  ```
  Výstup smí obsahovat pouze standardní Windows systémové knihovny a `libcrypto-3-x64.dll`.
- **Statická analýza**:
  ```powershell
  cppcheck --enable=warning,performance,portability,style src/
  ```

---

## 7. Titulky tabů (Tabs), adresní řádek a mezipaměť prohlížeče (Cache Invalidation)

### Titulky tabů a adresní řádek:
1. **`GetPathForMainWindowTitle`**:
   - `mode == 1` (**Directory Name Only**): Salamander volá pro titulky záložek (tabů). Plugin vrací název profilu v hranatých závorkách následovaný mezerou a názvem podsložky (např. `[NAS] Season 29` nebo `[NAS] /`). Pokud profil nemá jméno, použije se hostname nebo čistý název složky.
   - `mode == 2` (**Shortened Path**): Salamander volá pro záhlaví okna. Plugin vrací zkrácenou cestu `[NAS] sftp://user@host[:port]/.../podsložka`.
2. **`GetNextDirectoryLineHotPath`**:
   - Musí správně přeskočit prefix `sftp://user@host[:port]/` jako jeden celek (kořen) a následně rozdělovat cestu podle lomítek `/` i `\\`. Nesmí přeskakovat fixní počet znaků, aby nedošlo k poškození uživatelského jména (např. oříznutí `root` na `oot`).

### Zneplatnění diskové mezipaměti prohlížeče (F3 View):
- `uniqueFileName` předávaný do `AllocFileNameInCache` **musí** obsahovat kompletní identifikaci serveru (`user@host:port`), vzdálenou cestu a zároveň velikost a čas modifikace souboru:
  ```cpp
  _snprintf_s(uniqueFileName + len, sizeof(uniqueFileName) - len, _TRUNCATE,
              ":%I64u:%08lx%08lx", file.Size.Value,
              file.LastWrite.dwHighDateTime, file.LastWrite.dwLowDateTime);
  ```
- Tím se garantuje okamžité stažení nové verze při změně souboru na serveru i při přepnutí mezi různými servery se stejnou strukturou cest.

---

## 8. Přenosové dialogy a podpora Dark Mode (Transfer Dialogs & Theming)

### Přenosový dialog (`CSftpTransferProgressDlg` / `IDD_TRANSFERDLG`):
1. **Dva progress bary**:
   - `IDP_TR_FILE_PROGRESS` – průběh přenosu aktuálního souboru (0 až 1000 promile).
   - `IDP_TR_TOTAL_PROGRESS` – celkový průběh celé operace (stahování/nahrávání více položek).
2. **Připojení k tématu Salamandera**:
   - V `WM_INITDIALOG` se volá `SalamanderGUI->AttachProgressBar(HWindow, IDP_TR_FILE_PROGRESS)` a `SalamanderGUI->AttachProgressBar(HWindow, IDP_TR_TOTAL_PROGRESS)`, což zajistí správné vykreslování v nativním stylu a barvách Salamandera.
3. **Plná podpora Dark Mode**:
   - Využívá sdílený modul `plugindarkmode.h` (`../salamander-plugins/salamand/plugins/shared/plugindarkmode.o`).
   - V `WM_INITDIALOG`: `PluginDarkMode_ApplyTitleBar(HWindow);` (obarví záhlaví okna do tmavého tématu přes DwmSetWindowAttribute).
   - V dialogové proceduře:
     - `PluginDarkMode_HandleThemeMessage(HWindow, uMsg, wParam, lParam);`
     - Při `WM_CTLCOLORDLG` a `WM_CTLCOLORSTATIC`:
       ```cpp
       LRESULT lr = 0;
       if (PluginDarkMode_HandleCtlColor(uMsg, wParam, lParam, &lr))
           return (INT_PTR)lr;
       ```
   - Stejný postup je aplikován i pro `CCalcSizeProgressDlg` a `CDeleteProgressDlg`.
4. **Prefix aktivní konexe `[NAS]` v cestách a titulku**:
   - Vzdálené cesty v polích `From:` a `To:` jsou formátovány pomocí `SftpFormatTransferPath`: detekuje se vzdálená cesta (`SftpIsPathRemote`) a automaticky se předřadí jméno aktivního profilu `[NAS] /cesta` s následným zkrácením přes `PathCompactPathExA` (prefix `[NAS]` zůstává vždy zachován).
   - Titulek okna v `WM_INITDIALOG` je rovněž obohacen o prefix `[Jméno_konexe]`, což uživateli umožňuje okamžitě vidět, ke kterému serveru operace náleží.
5. **Dedikované UI vlákno dialogu (`CSftpProgressDlgThread`)**:
   - Po vzoru FTP pluginu (`COperationDlgThread`) běží každý přenosový dialog v samostatném dedikovaném UI vlákně s vlastní zprávovou smyčkou `GetMessage`.
   - Zprávy o aktualizaci workeru (`WM_APP_SFTP_WORKER_UPDATE`) i časovače se zpracovávají výhradně v tomto vlákně, čímž je zcela eliminováno jakékoli rušení hlavního okna Salamandera (rozbalená menu nezhasínají, kurzor neproblikává, ani když je okno na pozadí či bez fokusu).
6. **Dynamický layout a resizing (`LayoutDialog` / `SetColumnWidths`)**:
   - V detailním zobrazení si seznam Spojení (`IDL_CONNECTIONS`) a seznam Operací (`IDL_OPERATIONS`) proporcionálně dělí dostupnou vertikální výšku okna (35 % pro Spojení, 65 % pro Operace).
   - Tlačítka pod seznamy jsou spolehlivě ukotvena vpravo dole a šířky sloupců v obou seznamech se dynamicky dopočítávají podle aktuální šířky klientské oblasti.

---

## 9. Správné ošetření storna operací a prevence vzniku 0-bajtových souborů

Při přenosu více souborů nebo rekurzivním stahování/nahrávání musí plugin korektně reagovat na stisk tlačítka **Storno / Cancel** v progress dialogu (`CSftpTransferProgressDlg`):

1. **Centralizovaná detekce storna (`SftpIsCancelled`)**:
   - Funkce kontroluje jak `g_OvrCancel` (storno z dialogu dotazu na přepis/resume), tak stav progress dialogu `g_ProgDlg->GetWantCancel()`.
   - Stav storna se ukládá do perzistentního příznaku `g_ProgCancel`, který přetrvá i po destrukci okna progress dialogu v `SftpProgressEnd()`.
   - Všechny hlavní smyčky (`CopyOrMoveFromFS`, `CopyOrMoveFromDiskToFS`, `SftpDownloadRecursive`, `SftpUploadRecursive`) testují `SftpIsCancelled()` na začátku každé iterace. Při stornu se smyčka ihned ukončí (`break`), aniž by se dotazovala uživatele na *"Continue with other items?"*.

2. **Ověření storna před otevřením / vytvořením souboru**:
   - V `Download`, `Upload`, `ScpDownload` i `ScpUpload` se `ReportProgress(path, 0, total)` volá **před** jakýmkoliv voláním `fopen_s` nebo `libssh2_sftp_open(..., CREAT | TRUNC)`.
   - Pokud uživatel zrušil operaci dříve, `ReportProgress` vrátí `false` a soubor se vůbec neotevře ani nezaloží prázdný na cílovém úložišti.

3. **Úklid nedokončených 0-bajtových souborů**:
   - Pokud došlo k přerušení transferu a nebylo přeneseno nic (`!ok && done == 0 && resumeOffset == 0`), soubor se okamžitě smaže:
     - Lokálně: `DeleteFileA(localPath)`
     - Vzdáleně: `libssh2_sftp_unlink(Sftp, remotePath)`
   - Pokud již byla přenesena část dat (`done > 0`), data zůstávají na disku/serveru pro možnost budoucího navázání (resume).

---

## 10. Per-instance konexe a asynchronní přenosy na pozadí (Phase B)

Od verze **v1.3.0** plugin přechází z globálního singletonu na plně izolované per-instance konexe a asynchronní přenosy na pozadí:

### 1. Izolace instancí (`CPluginFSInterface`):
- Každý otevřený FS panel vlastní nezávislé SSH/SFTP spojení `Conn` (`CSftpConnection`) a konfiguraci `Profile` (`CSftpProfile`).
- Všechny operace ve virtuálním FS panelu přistupují výhradně k `this->Conn` a `this->Profile`.
- Keepalive časovač (`SFTP_TIMER_KEEPALIVE`) a odpojování panelů fungují zcela nezávisle na ostatních panelech či tabech.
- Progress callbacky v `CSftpConnection` jsou instancovány (každé spojení má svůj callback a kontext).

### 2. Architektura worker threadu (`CSftpTransferWorker`):
- Přenosy souborů a celých složek (stahování i nahrávání) jsou vyčleněny do samostatného pracovního vlákna `CSftpTransferWorker`.
- **Dedikovaná SSH relace (`WorkerConn`)**: Worker thread otevírá vlastní SSH spojení se stejným profilem, takže hlavní panelové spojení `Conn` zůstává ihned volné pro plynulé procházení a práci v panelech Salamandera.
- **Fronta úloh (`TaskQueue`)**: Úlohy typu `CSftpTransferTask` jsou bezpečně řazeny pod zámkem `QueueLock` a probouzeny signálem `WakeEvent`.
- **Thread-safe stav (`CSftpTransferState`)**: Sdílený stav uchovává informace o přenesených bajtech, rychlosti v MB/s, ETA a celkovém počtu položek.
- **Životní cyklus a Reset (`Reset()` / `Stop()`)**: Po dokončení každé přenosové operace je vlákno ukončeno a `WM_APP_SFTP_WORKER_FINISHED` volá `Worker->Stop()`. Před zahájením nové operace se volá `Worker->Reset()`, což zabraňuje nežádoucí akumulaci čítačů a počtů položek z předchozích přenosů.

### 3. Nemodální dialog s tokom na pozadí:
- Dialog `CSftpTransferProgressDlg` běží nemodálně a neblokuje hlavní okno správce souborů.
- **Tlačítko „Na pozadí" (`IDB_BACKGROUND`)**: Uživatel může dialog kdykoli minimalizovat či skrýt (`SW_HIDE`), přičemž přenos pokračuje plnou rychlostí v pozadí.
- **Znovuzobrazení dialogu**: V menu pluginu **Moduly ➔ SFTP ➔ Zobrazit přenosy...** (`MENUCMD_SHOWTRANSFERS`) lze dialog kdykoliv přenést zpět do popředí. Příkaz je přístupný ze všech panelů (včetně lokálních disků) a prohledává všechny aktivní i odpojené FS relace. Výchozí klávesová zkratka byla ponechána volná (hotkey `0`), aby nekolidovala s klávesovou zkratkou Salamandera pro správu tabů (`Ctrl+Shift+T`). Uživatel si ji může volitelně nastavit v konfiguraci Salamandera.
- **Celkový postup podle objemu dat (MB/kB)**: Celkový progress bar se počítá na základě poměru přenesených bajtů vůči celkovému očekávanému objemu dat (`TotalDoneBytes / TotalExpectedBytes`), nikoli pouhým počtem souborů.
- **Notifikace změn**: Po dokončení všech úloh ve frontě worker dialog automaticky zavolá `SalamanderGeneral->PostChangeOnPathNotification` pro cíl i zdroj (u operací přesunutí / Move).


### 4. Null-safety při volání virtuálního FS (`ChangePath`, `IsCurrentPath`, `IsOurPath`):
- Salamander při volání `ChangePanelPathToPluginFS` s prázdnou cestou (např. po stisku Login v Connect dialogu) předává do `ChangePath` parametr `userPart = NULL`.
- Všechny metody FS musí striktně ověřovat `if (userPart == NULL) userPart = "";` před jakoukoli dereferencí řetězce nebo předáním do stringových a path-helper funkcí (`SftpStripHost`, `SftpIsSamePath`, `SftpIsRoot`, `SftpJoin`, `SftpParent`).
- `SftpStripHost` při `NULL` vždy vrací prázdný řetězec `""` a nikdy `NULL`.

### 5. Správa životního cyklu nemodálních dialogů a Observer pattern (`ISftpTransferDlgObserver`):
- Nemodální dialogy jako `CSftpTransferProgressDlg` využívají Windows časovač (`WM_TIMER` 101, 100 ms) pro periodický dotaz na snapshot stavu workeru (`GetStateSnapshot`).
- Pokud uživatel zavře nebo opustí SFTP panel, instance `CPluginFSInterface` je zničena včetně členské proměnné `CSftpTransferWorker TransferWorker`, která v destruktoru volá `DeleteCriticalSection`.
- **Riziko pádu (Use-After-Free / `0x24` Access Violation)**: Pokud by dialog přežil zničení FS/workeru, při dalším ticku timeru by zavolal `EnterCriticalSection` na smazanou kritickou sekci, což v `ntdll.dll!RtlEnterCriticalSection` způsobí `access violation write on 0x0000000000000024` (dereference vynulovaného `DebugInfo`).
- **Architektonické řešení**:
  1. Zavedeno rozhraní `ISftpTransferDlgObserver` s čistě virtuální metodou `virtual void DetachWorker() = 0;`.
  2. Dialog toto rozhraní implementuje a v `DetachWorker()` okamžitě zabíjí časovač (`KillTimer(HWindow, 101)`) a nuluje ukazatel `Worker = NULL`.
  3. `CSftpTransferWorker` nese příznak `Initialized` a v metodách `Stop()` a v destruktoru ihned notifikuje pozorovatele `AttachedObserver->DetachWorker()` a nuluje `DlgHwnd`. Metoda `GetStateSnapshot` bezpečně ověřuje `if (!Initialized) return;` ještě před dotykem kritické sekce.
  4. Destruktor `CPluginFSInterface::~CPluginFSInterface()` bezpečně odpojí a zničí okno dialogu `ActiveTransferDlg`, aby nezůstávaly viset žádné osiřelé časovače ani okna.

### 6. Opuštění panelu, dotaz na odpojení (`TryCloseOrDetach`) a Detached FS:
- Podle vzoru oficiálního FTP pluginu Salamandera implementuje virtuální FS metodu `TryCloseOrDetach`:
  ```cpp
  virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason);
  ```
- **Obsluha `FSTRYCLOSE_CHANGEPATH`**:
  - Pokud uživatel opouští panel (změna disku, navigace pryč), dialog `SalMessageBoxEx` nabídne:
    - **Odpojit** (`DIALOG_YES`): `detach = FALSE; return TRUE;` -> spojení se korektně uzavře a FS se zruší.
    - **Ponechat** (`DIALOG_NO`): `detach = TRUE; return TRUE;` -> spojení přejde do režimu Detached FS. Zůstává živé v paměti Salamandera a uživatel se k němu může kdykoliv vrátit z nabídky Změna disku (`Alt+F1`/`Alt+F2`) nebo nástrojové lišty disků.
    - **Storno** (`IDCANCEL`): `return FALSE;` -> změna cesty je zrušena a panel zůstává v SFTP.
- **Pojmenování odpojených relací v `GetChangeDriveOrDisconnectItem`**:
  - Do nabídky Změna disku se vkládá řetězec `\tSFTP:[Název_Profilu] /vzdálena/cesta\t` se jménem aktivního profilu, což uživateli umožňuje snadnou orientaci mezi více odpojenými servery.
- **Perzistence volby v registru (`LeavePanelAction`)**:
  - `0` = Ptát se (výchozí chování).
  - `1` = Vždy odpojit.
  - `2` = Vždy ponechat na pozadí (Detached FS).

---

## 11. Tmavý režim (Dark Mode) pro dialogy pluginu

Salamander od verze 5.0 integruje celoaplikační tmavý režim řízený přes konfigurační parametr `SALCFG_USEWINDOWSDARKMODE`. Pro správné zobrazení dialogů pluginu v tmavém i světlém režimu platí následující pravidla:

1. **Inicializace stavu hostitele (`SftpInitDarkMode`)**:
   - Helper `plugindarkmode.cpp` vyžaduje explicitní informaci o politice hostitele:
     ```cpp
     DWORD useDark = 0;
     if (general->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &useDark, sizeof(useDark), NULL))
     {
         PluginDarkMode_SetHostPolicyAvailable(TRUE, useDark ? TRUE : FALSE);
         if (useDark)
             PluginDarkMode_SetHostResolvedColors(RGB(220, 220, 220), RGB(32, 32, 32), RGB(220, 220, 220));
         else
             PluginDarkMode_SetHostResolvedColors(CLR_INVALID, CLR_INVALID, CLR_INVALID);
     }
     ```
   - **Důležité**: Pro dialogy nelze použít barvy `SALCOL_ITEM_FG_NORMAL` a `SALCOL_ITEM_BK_NORMAL`, protože ty reprezentují barvy položek panelu souborů (které mohou mít světlé pozadí např. `RGB(240, 240, 240)`), což by způsobilo bílé rámečky pod statickými texty (`WM_CTLCOLORSTATIC`). Volá se proto `PluginDarkMode_SetHostResolvedColors` s nativními dialogovými tmavými barvami `RGB(32, 32, 32)` a textem `RGB(220, 220, 220)`.
2. **Aplikace na dialogové okno (`SftpApplyDarkModeToWindow`)**:
   - Při `WM_INITDIALOG` a při změně motivu (`WM_THEMECHANGED`, `WM_SETTINGCHANGE`) se volá pomocná funkce:
     - `PluginDarkMode_ApplyTitleBar(hwnd)` – zajistí tmavý titulek okna Windows (DWM).
     - `PluginDarkMode_ApplyListTreeThemeRecursive(hwnd)` – nastaví tmavý režim pro podřízené ovládací prvky.
     - Pro tlačítka typu `BS_PUSHBUTTON` / `BS_DEFPUSHBUTTON` je v tmavém režimu nutné explicitně zavolat `SetWindowTheme(child, L"DarkMode_Explorer", NULL);`, aby tlačítka převzala moderní tmavý vizuální styl systému Windows namísto bílého rámečku.
3. **Obsluha barev v dialogové proceduře & oprava `SS_TYPEMASK`**:
   - Ve zprávách `WM_CTLCOLORDLG`, `WM_CTLCOLORSTATIC` a `WM_CTLCOLORBTN` se předává řízení do `PluginDarkMode_HandleCtlColor(uMsg, (HDC)wParam, (HWND)lParam)`.
   - **Kritický gotcha v `plugindarkmode.cpp`**: V původním kódu byla podmínka `if ((style & (SS_ICON | ...)) != 0) return FALSE;`. V rozhraní Win32 však typ statického prvku není bitmaska, nýbrž enum v rozsahu `style & SS_TYPEMASK`. Protože hodnota masky byla `0x0F`, prvky `SS_RIGHT` (2) i `SS_LEFTNOWORDWRAP` (12) vracely `FALSE` a nebyly dark módem obslouženy. Kód byl zafixován na `LONG_PTR type = style & SS_TYPEMASK; if (type == SS_ICON || ...) return FALSE;`.
   - Současně byla do `DialogProc` dialogů (`CSftpTransferProgressDlg`, `CDeleteProgressDlg`, `CCalcSizeProgressDlg`) doplněna pojistka: pokud by `PluginDarkMode_HandleCtlColor` vrátil `FALSE`, ale `PluginDarkMode_ShouldUseDark()` je aktivní, dialog přímo nastaví `SetBkMode(hdc, TRANSPARENT)`, `SetTextColor(hdc, RGB(220, 220, 220))`, `SetBkColor(hdc, RGB(32, 32, 32))` a vrátí tmavý štětec `s_darkBgBrush`.

---

## 12. Přenosy mezi servery (Server-to-Server Copy)

Při operacích přenosu v `CPluginFSInterface::CopyOrMoveFromFS` dochází k vyhodnocení cílové cesty:
1. **Lokální disk**: Cíl začíná písmenem jednotky (`C:\...`) nebo `\\UNC\`. Spouští se asynchronní download z SFTP na lokální disk.
2. **Stejný SFTP server**: Cíl je virtuální cesta začínající `sftp:` a cíl ukazuje na tentýž hostitel a port jako aktivní panel (`Profile.Host`). Provádí se server-side kopírování/přesun v rámci jednoho spojení.
3. **Odlišný SFTP server (Server-to-Server)**:
   - Cíl má tvar `sftp://user@remotehost:port/path` nebo `//user@remotehost:port/path` s odlišným serverem než `this->Profile.Host`.
   - **Vyhledání cílového připojení**:
     - Plugin prohledá aktivní FS panely přes `InterfaceForFS.GetActiveFSList()`. Pokud nalezne odpovídající instanci se stejným hostitelem a uživatelem, převezme její aktivní profil i otevřené spojení `pTargetConn`.
     - Pokud aktivní panel neexistuje, prohledá uložené profily `SftpProfiles`.
     - V případě potřeby naváže spojení k cíli přes `SftpEnsureConnected(parent, localTargetConn, targetProfile)`.
   - **Přenosový dialog a identifikace profilů**:
     - Do dialogu `CSftpTransferProgressDlg` se předávají názvy zdrojového i cílového profilu:
       - `From: [Zdrojový_Profil] /cesta/zdroj`
       - `To: [Cílový_Profil] /cesta/cil`
       - Titulek okna přenosu: `[[Zdroj] -> [Cíl]] SFTP Transfer`.
   - **Streamovaný přenos**:
     - Pro každou označenou položku se soubor stáhne ze zdroje (`this->Conn`) do bezpečného dočasného umístění v lokálním `%TEMP%` a ihned se nahraje na cílový server (`*pTargetConn`).
     - Dočasný soubor je smazán.
     - V případě přesunu (`!copy`) se zdrojový soubor po úspěšném nahrání na cíl smaže ze zdroje.
   - **Dvoustranná notifikace změn**:
     - Po dokončení se vyvolá `SalamanderGeneral->PostChangeOnPathNotification` pro zdrojovou i cílovou cestu, což zajistí okamžitou aktualizaci obou otevřených panelů.

---

## 13. Nezávislé instance workerů pro souběžné přenosy na pozadí & životní cyklus dialogů

Při souběžném spuštění více přenosů (stahování i nahrávání) v rámci téhož panelu nebo profilu nesmí docházet ke sdílení jediné instance `CSftpTransferWorker`:
1. **Per-transfer Worker alokace**:
   - V metodách `CopyOrMoveFromFS` a `CopyOrMoveFromDiskToFS` se pro každou operaci dynamicky vytvoří nový worker:
     ```cpp
     CSftpTransferWorker* worker = new CSftpTransferWorker();
     ```
   - Úlohy se zařadí do tohoto workeru a ten se předá dialogu s příznakem vlastnictví:
     ```cpp
     dlg->AttachWorker(worker, true);
     RegisterTransferDlg(dlg);
     worker->Start(Profile, dlg->HWindow);
     ```
2. **Vlastnictví workeru dialogem (`OwnsWorker`)**:
   - `CSftpTransferProgressDlg` nese příznak `OwnsWorker`.
   - V metodě `DetachWorker()`, při destrukci okna (`WM_DESTROY`) i při dokončení přenosu (`WM_APP_SFTP_WORKER_FINISHED`):
     - Zruší se časovač (`KillTimer(HWindow, 101)`).
     - Workeru se vynuluje HWND i observer.
     - Pokud `OwnsWorker == true`, zavolá se `w->Stop()` a `delete w;`.
     - Ukazatele se vynulují před uvolněním pro zamezení rekurzivního volání / double free.
3. **Evidence dialogů (`ActiveTransferDlgs`) v FS rozhraní**:
   - `CPluginFSInterface` udržuje `std::vector<CSftpTransferProgressDlg*> ActiveTransferDlgs`.
   - Dialog se při spuštění registruje přes `FS->RegisterTransferDlg(dlg)` a při zániku odregistruje přes `FS->UnregisterTransferDlg(dlg)`.
   - Metoda `ShowTransferDialog(HWND parent)` a příkaz `MENUCMD_SHOWTRANSFERS` iterují přes všechny aktivní dialogy a obnovují všechna minimalizovaná/skrytá okna.
4. **Korektní teardown při zavření Salamandera (`~CPluginFSInterface`)**:
   - Při ukončení Salamandera (`WM_USER_CLOSE_MAINWND`) probíhá destrukce všech instancí `CPluginFSInterface`.
   - Destruktor prochází `ActiveTransferDlgs`, odpojuje worker dialogy (`dlg->SetFS(NULL)`, `dlg->DetachWorker()`) a bezpečně ničí okna (`DestroyWindow`), čímž je zamezeno pádům na neplatné ukazatele po uvolnění pluginu.
5. **Nezávislá nemodální okna a Z-pořadí (`Parent = NULL`, `WS_EX_APPWINDOW`)**:
   - Ve Win32 API správce oken vždy drží vlastněné okno (`owned window`) nad jeho vlastníkem (`owner window`) v Z-pořadí, i když vlastník získá fokus.
   - Předáním `Parent = NULL` do `CCommonDialog` se dialog stává unowned oknem.
   - Původní `parent` se uloží do `CenterToWnd` a použije se pro jednorázové vycentrování v `WM_INITDIALOG` přes `SalamanderGeneral->MultiMonCenterWindow(HWindow, CenterToWnd, TRUE)`.
   - Přidáním rozšířeného stylu `WS_EX_APPWINDOW` a stylu `WS_MINIMIZEBOX` získává přenosový dialog vlastní tlačítko na hlavním panelu Windows a tlačítko minimalizace v záhlaví.
   - Při kliknutí do hlavního okna Salamandera se Salamander bez problémů přenese do popředí a dialogy jej netrvale nepřekrývají.

---

## 14. Vláknová bezpečnost přenosů na pozadí, OpenSSL DRBG a Windows CSPRNG (`BCryptGenRandom`)

Při provozu asynchronních přenosů ve vedlejších vláknech je nutné dodržet zásadní pravidla pro souběh s kryptografickými knihovnami a C Runtime:
1. **Inicializace vlákna přes C Runtime (`_beginthreadex` namísto `CreateThread`)**:
   - Vlákno workeru nesmí být vytvářeno surovým Win32 `CreateThread`, které nealokuje a neinicializuje per-thread datové struktury C Runtime (`_tiddata` v UCRT/MSVCRT).
   - Vždy použijte `_beginthreadex` z `<process.h>` se vstupním bodem `unsigned __stdcall ThreadEntryPoint(void* param)` a na výstupu volejte `_endthreadex(0)`.
2. **Nativní Windows CSPRNG pro SSH náhodná čísla (`BCryptGenRandom`)**:
   - OpenSSL 3 využívá složitou hierarchii deterministických generátorů náhodných čísel (DRBG) s per-thread TLS kontexty a synchronizačními zámky (`rand_global->lock` / `CRYPTO_THREAD_read_lock` -> `RtlAcquireSRWLockShared`).
   - Při volání `RAND_bytes` z nově vytvořeného vedlejšího vlákna docházelo v OpenSSL 3 k přístupové výjimce (Access Violation na uvolněné adrese zámku v `libcrypto-3-x64.dll`).
   - Funkce `ssh2_random` v `src/libssh2/openssl.c` je proto na Windows přímo napojena na jádrové systémové rozhraní Windows CNG:
     ```c
     #if defined(_WIN32)
     if (BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0)
         return 0;
     #endif
     ```
   - Toto volání je 100% thread-safe, rychlé, FIPS certifikované, spravované jádrem Windows a zcela nezávislé na stavu a zámcích OpenSSL DRBG.
3. **Explicitní inicializace a úklid vlákna OpenSSL**:
   - V `CSftpConnection::GlobalInit` se na hlavním vlákně volá:
     ```cpp
     OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
                         OPENSSL_INIT_ADD_ALL_CIPHERS |
                         OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);
     ```
   - Před ukončením každého worker vlákna se volá `OPENSSL_thread_stop()`, které uvolní případné alokace v TLS kontextu daného vlákna.

---

## 15. Dialog konfliktu existujícího souboru, 2-módový Progress dialog (Simple/Detailed) a klávesnicová navigace

Pro zajištění konzistence se standardními pluginy Salamanderu (zejména vestavěným FTP pluginem) a vysokého uživatelského komfortu:

### 1. Dialog řešení konfliktu existujícího souboru (`IDD_CONFLICTDLG` / `CSftpConflictDlg`)
- **Detekce konfliktu**: Před zahájením zápisu souboru ve workeru (`DoDownloadRecursive`, `DoUploadRecursive`) se ověří existence cílového souboru (`GetFileAttributesEx` pro lokální disk, `libssh2_sftp_stat` pro SFTP).
- **Zobrazení dialogu**: Pokud soubor existuje a uživatel dříve nezvolil trvalou akci pro celou operaci ("Pamatovat si volbu" / Apply to all), vyvolá se `CSftpConflictDlg`.
- **Dropdown split tlačítko "Opakovat"**:
  - Pomocí `SalamanderGUI->AttachButton(HWindow, IDOK, BTF_DROPDOWN)` se standardní tlačítko Windows změní na split tlačítko s rozbalovací šipkou.
  - Při kliknutí na šipku nebo stisku klávesy se vyvolá popup menu `IDM_FILEEXISTSERRRETRY` s možnostmi:
    - *Opakovat* (`IDOK`) – opakuje operaci od začátku.
    - *Pokračovat* (`CM_SIED_RESUME`) – naváže na existující soubor od jeho velikosti (`resumeOffset = existingSize`).
    - *Pokračovat nebo přepsat* (`CM_SIED_RESUMEOROVR`) – pokud je cílový soubor menší, naváže; pokud je stejný nebo větší, přepíše.
    - *Použít alternativní název* (`CM_SCRD_USEALTNAME`) – automaticky vygeneruje např. `soubor (1).ext` do editboxu nového cílového názvu přes `SftpGenerateAltName`.
- **Tlačítka dialogu**: Přepsat (`CM_SIED_OVERWRITE`), Přepsat vše (`CM_SIED_OVERWRITEALL`), Přeskočit (`IDB_SCRD_SKIP`), Storno (`IDCANCEL`), Nápověda (`IDHELP`).

### 2. Informativní přenosový dialog se dvěma módy (`CSftpTransferProgressDlg`)
- **Kompaktní režim (Simple Look)**:
  - Zobrazuje zdrojovou a cílovou cestu, zbývající čas (ETA, výpočet..., pozastaveno, hotovo), uplynulý čas, stav přenesených dat (MB/kB a počet chyb) a celkový progress bar.
  - Checkbox `[x] Po skončení operace zavřít toto okno` s perzistencí v registru (`CloseTransferDlgOnFinish`).
  - Pokud checkbox není zaškrtnut nebo došlo k chybě: dialog zůstane po skončení otevřený, tlačítko Storno se změní na „Zavřít" s výchozím fokusem (`BS_DEFPUSHBUTTON`), tlačítka Pauza a Na pozadí se zakáží a stav ukáže `(hotovo)` nebo `(chyba)`.
- **Detailní režim (Detailed Look – "Detaily >>" / "Chyby >>")**:
  - Dialog se dynamicky zvětší směrem dolů (`ShowControlsAndChangeSize` na základě uložené původní výšky okna z `WM_INITDIALOG`).
  - **ListView Spojení (`IDL_CONNECTIONS`)**: zobrazuje aktivní připojení (ID, Akce, Stav) a tlačítka.
  - **ListView Operace per soubor (`IDL_OPERATIONS`)**: zobrazuje všechny položky fronty se systémovými ikonami souborů (`SHGetFileInfo`), popisem a stavem (`Čeká`, `Zpracovávám`, `Dokončeno`, `Chyba`, `Přeskočeno`). Data se poskytují virtuálně přes `LVN_GETDISPINFOA` a `LVN_GETDISPINFOW`.
  - **Filtr chyb**: Checkbox `[ ] Zobrazit jen chyby` filtruje pouze položky se stavem `StatusError`.
  - **Tlačítko "Chyby >>"**: je aktivní pouze při chybě; po stisku automaticky rozbalí detaily, aktivuje filtr chyb a zaměří chybující položku.
  - **Tlačítko "Pauza" / "Pokračovat"**: volá `Worker->SetPaused()`, což pozastaví smyčku worker threadu pomocí Win32 `ResetEvent(RunEvent)` a při pokračování obnoví přenos přes `SetEvent(RunEvent)`.

### 3. Klávesnicová navigace v modeless dialozích (`IsDialogMessage`)
- Modeless (nemodální) dialogy v aplikaci s hlavní smyčkou zpráv hlavního okna nedostávají automaticky klávesy `Tab`, `Shift+Tab`, `Esc` ani šipky pro přepínání prvků, pokud zpráva neprojde funkcí `IsDialogMessage`.
- Plugin v `src/sftp.cpp` udržuje globální seznam aktivních HWND dialogů `g_TransferDlgHwnds` chráněný kritickou sekcí.
- **Kritické pravidlo**: `IsDialogMessage` se ve vláknovém hooku `WH_GETMESSAGE` **smí volat pouze pro klávesové zprávy** (`pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST`) a pouze pro **viditelná okna** (`IsWindowVisible(hDlg)`).
  - *Důvod*: Pokud by se `IsDialogMessage` zavolala pro myšové nebo non-client zprávy (`WM_NCLBUTTONDOWN`, `WM_LBUTTONDOWN` atd.), vrátí `TRUE`, hook zprávu vynuluje (`WM_NULL`) a Windows ji nikdy nedoručí do `DispatchMessage`. Následkem toho systém Windows vůbec nespustí vnitřní smyčku pro posun okna (`SC_MOVE`), okno nelze chytit za horní lištu, zamrzne v pohybu a ignoruje kliknutí myši.
- Správná implementace v hooku `GetMsgHookProc`:
  ```cpp
  if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
  {
      for (HWND hDlg : dlgList)
      {
          if (IsWindow(hDlg) && IsWindowVisible(hDlg) && IsDialogMessage(hDlg, pMsg))
          {
              pMsg->message = WM_NULL; // Klávesa byla zkonzumována dialogem
              return 0;
          }
      }
  }
  ```

### 4. Pravidla pro Dark Mode u dynamicky zobrazovaných prvků a ListView
- **Tlačítka a `WM_CTLCOLORBTN`**: Při použití vizuálních stylů uxtheme (`DarkMode_Explorer`) se pro tlačítka v `WM_CTLCOLORBTN` **nesmí vracet tmavý štětec**. Vrácení tmavého štětce způsobí, že Windows podklad tlačítka vykreslí jako černý nečitelný obdélník.
- **ListView a dynamické prvky**: Při dynamickém zvětšení okna (např. po kliknutí na „Detaily >>") je nutné zavolat `SftpApplyDarkModeToWindow(HWindow)` na nově zobrazené ovládací prvky a pro všechny `SysListView32` explicitně nastavit:
  ```cpp
  ListView_SetTextColor(hList, RGB(220, 220, 220));
  ListView_SetTextBkColor(hList, RGB(25, 25, 25));
  ListView_SetBkColor(hList, RGB(25, 25, 25));
  SetWindowTheme(hList, L"DarkMode_Explorer", NULL);
  SetWindowTheme(ListView_GetHeader(hList), L"DarkMode_ItemsView", NULL);
  ```
- **Zamezení rušení hlavní smyčky Salamandera na pozadí**: Pokud je dialog skrytý po stisku „Na pozadí", funkce `UpdateFromWorker` nesmí volat `SetWindowText`, `SetDlgItemText` ani `InvalidateRect`. Tyto zprávy ruší rozbalená menu Salamandera a způsobují poblikávání kurzoru. Stačí test: `if (IsBackground || !IsWindowVisible(HWindow)) return;`.

---

## 16. Ochrana proti pádu přenosu na `Timed out waiting on socket` a ladění socketů

### 1. Příčina chyby `Writing remote file: Timed out waiting on socket`
Při nahrávání větších souborů na server (např. NAS, pomalé disky, síťové výkyvy) docházelo k neočekávanému pádu přenosu:
- V `libssh2` ve funkci `_libssh2_wait_socket` (v `src/libssh2/session.c`) se při čekání na socket v blokujícím režimu počítal timeout pro volání `select()` z periody keepalive (`libssh2_keepalive_send` vrací čas do příštího keepalive paketu, např. 10 s).
- Pokud server během těchto 10 sekund nepotvrdil zápis do socketu (např. z důvodu flushování diskové vyrovnávací paměti na serveru nebo plného TCP receive okna), `select()` skončil s `rc == 0`.
- Knihovna `libssh2` však `rc == 0` nekriticky vyhodnotila jako fatální chybu spojení:
  ```c
  if (rc == 0)
      return ssh2_err(session, LIBSSH2_ERROR_TIMEOUT, "Timed out waiting on socket");
  ```
  i když šlo pouze o probuzení časovačem keepalive, nikoli o vypršení skutečného aplikačního timeoutu.

### 2. Řešení v `src/libssh2/session.c`
V `_libssh2_wait_socket` se návratová hodnota `rc == 0` rozlišuje:
- Pokud je nastaven `api_timeout > 0`, ověří se skutečně uplynulý čas od začátku operace (`elapsed_time >= api_timeout`). Teprve při překročení celkového timeoutu se vrátí chyba `LIBSSH2_ERROR_TIMEOUT`. Pokud vypršel pouze dílčí keepalive interval, funkce vrátí `0` ("ready to try again"), což umožní odeslat keepalive sondu a pokračovat v čekání.
- Pokud `api_timeout` není nastaven (`0`), timeout `select()` vznikl pouze periodou keepalive; funkce bezpečně vrací `0` a přenos pokračuje bez falešného přerušení.

### 3. Nastavení socketů v `CSftpConnection::Connect`
- **Session timeout**: Nastaven explicitní timeout operací `libssh2_session_set_timeout(Session, 60000)` (60 sekund), který dává serveru dostatek času pro diskové operace, ale chrání před nekonečným uváznutím.
- **`TCP_NODELAY`**: Na socketu byl vypnut Nagleův algoritmus (`setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, ...)`), což zamezuje 40-200ms zpožděním při potvrzování SFTP bloků a zlepšuje plynulost toku dat.



