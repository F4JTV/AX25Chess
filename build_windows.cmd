@echo off
rem Lanceur pour ceux qui preferent double-cliquer.
rem
rem   build_windows.cmd            build complet
rem   build_windows.cmd full       idem
rem   build_windows.cmd app        build portable seul
rem   build_windows.cmd installer  installateur seul, depuis dist\
rem   build_windows.cmd clean      nettoie puis reconstruit
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_windows.ps1" %*
pause
