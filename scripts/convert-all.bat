@echo off
setlocal enabledelayedexpansion

REM ===========================================================================
REM Batch model converter.
REM
REM Drag a FOLDER onto this .bat (e.g. your MODELS folder), or run:
REM     convert-all.bat "C:\path\to\MODELS"
REM
REM For every immediate subfolder it picks the best model file it finds and
REM converts it to  <ROOT>\RESULT\<subfoldername>.glb.  Loose model files in the
REM root are converted too. Each .glb embeds that model's textures, so RESULT
REM ends up holding self-contained models ready for CorForge (Add -> Model).
REM ===========================================================================

set "EXE=%~dp0..\build\bin\modelconvert.exe"

set "ROOT=%~1"
if "%ROOT%"=="" set "ROOT=%CD%"

if not exist "%EXE%" (
  echo modelconvert.exe not found - build the project first ^(scripts\build.ps1^).
  pause & exit /b 1
)

set "RESULT=%ROOT%\RESULT"
mkdir "%RESULT%" 2>nul

echo Root:   %ROOT%
echo Output: %RESULT%
echo.

REM --- each immediate subfolder -> one .glb named after the folder -----------
for /d %%D in ("%ROOT%\*") do (
  if /i not "%%~nxD"=="RESULT" (
    set "PICK="
    for %%E in (gltf glb fbx obj dae 3ds stl ply blend) do (
      if not defined PICK (
        for %%F in ("%%~D\*.%%E") do if not defined PICK set "PICK=%%~fF"
      )
    )
    if defined PICK (
      echo [%%~nxD] ^<- "!PICK!"
      "%EXE%" "!PICK!" "%RESULT%\%%~nxD.glb"
      echo.
    ) else (
      echo [%%~nxD] no model files found
    )
  )
)

REM --- loose model files directly in the root --------------------------------
for %%E in (gltf glb fbx obj dae 3ds stl ply blend) do (
  for %%F in ("%ROOT%\*.%%E") do (
    echo [root] "%%~nxF"
    "%EXE%" "%%F" "%RESULT%\%%~nF.glb"
  )
)

echo.
echo Done. Converted models are in:  %RESULT%
echo Copy the .glb you want into CorForge\assets\models\, then use Add -^> Model.
pause
