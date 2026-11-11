@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ANDROID_DIR=%SCRIPT_DIR%android"

if "%1"=="" goto build_all
if /i "%1"=="debug" goto build_debug
if /i "%1"=="release" goto build_release
if /i "%1"=="install" goto install_apk
if /i "%1"=="logcat" goto run_logcat
if /i "%1"=="clean" goto clean_all

echo Uso: build_android.bat [debug ^| release ^| install ^| logcat ^| clean]
goto end

:build_all
echo [SkyRecomp] Compilando APK Debug e Release...
cd /d "%ANDROID_DIR%"
call gradlew.bat assembleDebug assembleRelease
if %ERRORLEVEL% NEQ 0 (
    echo [SkyRecomp] ERRO: Falha na compilacao!
    goto end
)
echo.
echo [SkyRecomp] Build concluido com sucesso!
echo Debug:   %ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk
echo Release: %ANDROID_DIR%\app\build\outputs\apk\release\app-release.apk
goto end

:build_debug
echo [SkyRecomp] Compilando APK Debug...
cd /d "%ANDROID_DIR%"
call gradlew.bat assembleDebug
if %ERRORLEVEL% NEQ 0 (
    echo [SkyRecomp] ERRO: Falha na compilacao Debug!
    goto end
)
echo [SkyRecomp] APK Debug gerado em: %ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk
goto end

:build_release
echo [SkyRecomp] Compilando e assinando APK Release...
cd /d "%ANDROID_DIR%"
call gradlew.bat assembleRelease
if %ERRORLEVEL% NEQ 0 (
    echo [SkyRecomp] ERRO: Falha na compilacao Release!
    goto end
)
echo [SkyRecomp] APK Release (assinado) gerado em: %ANDROID_DIR%\app\build\outputs\apk\release\app-release.apk
goto end

:install_apk
echo [SkyRecomp] Instalando APK no dispositivo conectado via ADB...
adb install -r "%ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk"
goto end

:run_logcat
echo [SkyRecomp] Monitorando logs do SkyRecomp via ADB Logcat...
adb logcat -c
adb logcat -s SkyRecomp:V SDL:V Vulkan:V AndroidRuntime:E
goto end

:clean_all
echo [SkyRecomp] Limpando caches e builds...
cd /d "%ANDROID_DIR%"
call gradlew.bat clean
goto end

:end
cd /d "%SCRIPT_DIR%"
endlocal
