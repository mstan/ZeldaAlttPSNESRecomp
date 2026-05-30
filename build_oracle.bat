@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" >nul 2>&1
msbuild "%~dp0zelda.sln" /p:Configuration=Oracle /p:Platform=x64 /m /t:Rebuild /v:minimal
exit /b %ERRORLEVEL%
