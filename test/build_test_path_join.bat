@echo off
rem Builds and runs test_path_join.cpp. Run from a VS x64 developer prompt (vcvars64.bat).
setlocal
pushd "%~dp0"
rem extract SftpNormalize .. SftpParent verbatim from sftpglue.cpp
powershell -NoProfile -Command "$l=Get-Content '..\src\sftpglue.cpp'; $s=($l|Select-String -SimpleMatch 'void SftpNormalize(char* path)').LineNumber; $e=($l|Select-String -SimpleMatch 'bool SftpIsSamePath(').LineNumber; $l[($s-1)..($e-2)]|Set-Content 'sftpglue_paths.inc'" || (popd & exit /b 1)
cl /nologo /EHsc /W3 /J test_path_join.cpp user32.lib || (popd & exit /b 1)
.\test_path_join.exe
set RC=%ERRORLEVEL%
popd
exit /b %RC%
