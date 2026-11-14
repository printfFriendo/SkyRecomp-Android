@echo off
setlocal enabledelayedexpansion

title SkyRecomp Android - 1-Click Game Recompiler and APK Builder
echo ===============================================================================
echo     SkyRecomp Android - Recompilador Nativo de NSP e Gerador de APK (ARM64)
echo ===============================================================================
echo.

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:: 1. Localizar arquivo NSP
set "NSP_PATH=%~1"

if "%NSP_PATH%"=="" (
    if exist "C:\Projetos\The Legend of Zelda Skyward Sword HD.nsp" (
        set "NSP_PATH=C:\Projetos\The Legend of Zelda Skyward Sword HD.nsp"
    ) else if exist "%SCRIPT_DIR%extracted\exefs\main" (
        echo [INFO] ExeFS ja descompactado encontrado em extracted\exefs.
        set "NSP_PATH=EXTRACTED"
    ) else (
        echo [?] Nenhum arquivo NSP informado via linha de comando ou arrastar-e-soltar.
        set /p "NSP_PATH=Digite ou arraste o caminho completo do arquivo NSP: "
        set "NSP_PATH=!NSP_PATH:"=!"
    )
)

:: 2. Localizar prod.keys
set "KEYS_PATH="
if exist "%USERPROFILE%\Desktop\Castlevania\prod.keys" (
    set "KEYS_PATH=%USERPROFILE%\Desktop\Castlevania\prod.keys"
) else if exist "%USERPROFILE%\.switch\prod.keys" (
    set "KEYS_PATH=%USERPROFILE%\.switch\prod.keys"
) else if exist "%SCRIPT_DIR%prod.keys" (
    set "KEYS_PATH=%SCRIPT_DIR%prod.keys"
) else if exist "C:\Projetos\prod.keys" (
    set "KEYS_PATH=C:\Projetos\prod.keys"
)

if not "%NSP_PATH%"=="EXTRACTED" (
    if not exist "%NSP_PATH%" (
        echo [ERRO] O arquivo NSP especificado nao foi encontrado:
        echo        "%NSP_PATH%"
        pause
        exit /b 1
    )
    if "%KEYS_PATH%"=="" (
        set /p "KEYS_PATH=Digite ou arraste o caminho do seu arquivo prod.keys: "
        set "KEYS_PATH=!KEYS_PATH:"=!"
    )
    if not exist "!KEYS_PATH!" (
        echo [ERRO] Arquivo prod.keys nao encontrado!
        pause
        exit /b 1
    )
)

echo.
echo [CONFIGURACAO]
if not "%NSP_PATH%"=="EXTRACTED" (
    echo  - ROM NSP:   "%NSP_PATH%"
    echo  - Keys:      "%KEYS_PATH%"
) else (
    echo  - ROM:       Binarios ExeFS ja extraidos em extracted\exefs
)
echo.

:: 3. Executar pipeline de descompactacao, geracao C e compilacao ARM64
if not "%NSP_PATH%"=="EXTRACTED" (
    python tools\build_game_android.py --nsp "%NSP_PATH%" --keys "%KEYS_PATH%" --apk
) else (
    python tools\build_game_android.py --apk
)

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ===============================================================================
    echo [FALHA] Houve um erro durante a recompilacao ou geracao do APK.
    echo ===============================================================================
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ===============================================================================
echo [SUCESSO] SkyRecomp APK Release compilado e assinado com sucesso!
echo ===============================================================================
echo.
echo 1. O APK final pronto para o Poco F5 esta localizado em:
echo    %SCRIPT_DIR%android\app\build\outputs\apk\release\app-release.apk
echo.
echo 2. Para jogar, instale o APK no Poco F5 e copie o arquivo de dados:
echo    Origem:  %SCRIPT_DIR%extracted\romfs.bin
echo    Destino: /sdcard/Android/data/com.apolinario.skyrecomp/files/romfs.bin
echo.
echo 3. Voce tambem pode instalar diretamente agora via USB/ADB se desejar.
echo.
choice /c SN /m "Deseja instalar o APK e copiar os dados no dispositivo conectado agora (ADB)?"
if errorlevel 2 goto fim
if errorlevel 1 goto instalar_adb

:instalar_adb
echo.
echo [ADB] Instalando APK Release...
adb install -r "%SCRIPT_DIR%android\app\build\outputs\apk\release\app-release.apk"
echo.
echo [ADB] Criando pasta de dados no celular...
adb shell mkdir -p /sdcard/Android/data/com.apolinario.skyrecomp/files/
echo [ADB] Transferindo libskygame.so...
adb push "%SCRIPT_DIR%android\app\src\main\jniLibs\arm64-v8a\libskygame.so" /sdcard/Android/data/com.apolinario.skyrecomp/files/
if exist "%SCRIPT_DIR%extracted\romfs.bin" (
    echo [ADB] Transferindo romfs.bin (7 GB, pode levar alguns minutos via USB)...
    adb push "%SCRIPT_DIR%extracted\romfs.bin" /sdcard/Android/data/com.apolinario.skyrecomp/files/romfs.bin
)
echo.
echo [ADB] Concluido com sucesso! Inicie o aplicativo SkyRecomp no seu Poco F5.

:fim
echo.
echo Pressione qualquer tecla para sair.
pause >nul
