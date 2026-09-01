@echo off
cd /d "%~dp0"

echo [1/2] Zapusk signal'nogo servera...
start "Eidolon Signaling" cmd /k "python signaling\main.py"

:: Zhdem 2 sekundy dlya starta websocket-servera
timeout /t 2 /nobreak >nul

echo [2/2] Zapusk Eidolon Host...
start "Eidolon Host" cmd /k "build\host\Release\eidolon_host.exe"