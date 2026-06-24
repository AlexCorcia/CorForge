@echo off
REM Drag a model file (.obj/.fbx/.3ds/.dae/.stl/.ply/.gltf/.blend) onto this
REM file in Explorer. It writes a .glb into CorForge\assets\models, ready to
REM load in the editor via  Add object -> Model.

set "EXE=%~dp0..\build\bin\modelconvert.exe"
set "OUTDIR=%~dp0..\assets\models"

if "%~1"=="" (
  echo Drag a model file onto this .bat, or run:
  echo    convert-model.bat "C:\path\to\model.obj"
  pause
  exit /b 1
)

if not exist "%EXE%" (
  echo modelconvert.exe not found - build the project first ^(scripts\build.ps1^).
  pause
  exit /b 1
)

echo Converting "%~1"  ->  "%OUTDIR%\%~n1.glb"
"%EXE%" "%~1" "%OUTDIR%\%~n1.glb"
echo.
echo Done. Launch CorForge and use  Add object -^> Model -^> %~n1
pause
