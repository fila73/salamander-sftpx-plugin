@echo off
rem Builds and runs test_cfgmigrate.cpp. Run from a VS x64 developer prompt (vcvars64.bat).
setlocal
pushd "%~dp0"
cl /nologo /EHsc /W3 /J test_cfgmigrate.cpp advapi32.lib || (popd & exit /b 1)
.\test_cfgmigrate.exe
set RC=%ERRORLEVEL%
popd
exit /b %RC%
