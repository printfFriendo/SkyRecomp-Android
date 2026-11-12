# Android Port State (Checkpoint de Progresso)

## Status Atual
- **Fase Ativa:** Todas as Fases (1 a 5) + Blindagem de Inicialização & Sensores de Giroscópio Concluídos!
- **Últimos Arquivos Modificados/Criados:** `MainActivity.java`, `runtime/hle/input.c`, `runtime/nvn/vulkan/device.c`, `runtime/paths.c`, `runtime/platform/linux.c`
- **Artefatos Finais:**
  - `android/app/build/outputs/apk/release/app-release.apk` (8.95 MB, **Assinado** com APK Signature Scheme v2, alinhado com `zipalign` 4-byte boundary).
  - Suporte a Sensores: Giroscópio e Acelerômetro nativos do smartphone integrados ao SixAxis/MotionPlus.
  - Blindagem de Inicialização: Preload de `libc++_shared.so`, criação automática da pasta de dados pelo Java, fallbacks para Vulkan 1.1/1.2 em GPUs móveis e remoção de unmap destrutivo no `mmap`.
- **Verificação Estática & Build:**
  - Compilação Android NDK r28b + Gradle Release: 100% SUCCESS.
  - Alinhamento de páginas: 16KB (`0x4000`), 100% compatível com Android 15+.

---

## Checklist de Fases

### Fase 1: Inicialização do Tracker e Auditoria (Read-Only)
- [x] 1.1 Verificar existência e criar `ANDROID_PORT_STATE.md` com checklist completo.
- [x] 1.2 Inspecionar `Makefile`, `tools/`, `docs/BUILD-linux.md` e fontes em `runtime/`.
- [x] 1.3 Mapear chamadas dependentes de SO (`mmap`, `dlopen`, XDG paths, `pthread`, intrínsecos de CPU, inicialização SDL3/Vulkan).
- [x] 1.4 Registrar mapeamento detalhado em `ANDROID_PORT_STATE.md` e concluir Fase 1.

### Fase 2: Sistema de Build CMake + Android NDK (`android/`)
- [x] 2.1 Criar `CMakeLists.txt` raiz/adaptado compatível com cross-compile CLI (Ninja/CMake) e NDK r27+ (NDK r28.2 integrado).
- [x] 2.2 Configurar target `skygame` (SHARED) com flags otimizadas (`-O3 -march=armv8-a+simd+crc -fomit-frame-pointer -fno-strict-aliasing`).
- [x] 2.3 Configurar target `main` / `skyrecomp` (SHARED) com dependências `SDL3`, `shaderc_shared`, `vulkan`, `log`, `android`, `dl`, `m`.
- [x] 2.4 Criar projeto Gradle em `android/` com `build.gradle.kts`, `settings.gradle.kts`, `gradle.properties` e `app/build.gradle.kts`.
- [x] 2.5 Configurar `AndroidManifest.xml` (`extractNativeLibs="true"`, `abiFilters += "arm64-v8a"`, orientação landscape, `SDLActivity`).
- [x] 2.6 Validar geração e configuração CMake salvando em `ANDROID_BUILD_ERRORS.log`.

### Fase 3: Adaptação de OS & Memória (Linux Glibc -> Android Bionic)
- [x] 3.1 Virtual Memory (`mmap`): tratar Top-Byte Ignore (TBI), tagged pointers, ASLR e `MAP_FIXED_NOREPLACE` em `runtime/platform/linux.c` e `runtime/main.c`.
- [x] 3.2 Dynamic Loading (`dlopen`): adaptar `runtime/gamelib.c` e `runtime/gamelib.h` para `libskygame.so` e resolução no `nativeLibraryDir` do APK.
- [x] 3.3 File System Paths: implementar `SDL_GetPrefPath` e rotas de armazenamento Android em `runtime/paths.c`.
- [x] 3.4 Runtime Logger: espelhar logs para logcat (`__android_log_print`) e `skyrecomp_crash.log` em `runtime/platform/linux.c` e `runtime/rt.h`.
- [x] 3.5 Backtrace & Diagnostics: adaptar `plat_print_backtrace` sem `<execinfo.h>` usando libunwind/Android API.

### Fase 4: Backend Vulkan 1.3 para GPUs Mobile (Adreno / Turnip / Mali)
- [x] 4.1 Garantir integração da superfície `SDL_Vulkan_CreateSurface` com `ANativeWindow` em `runtime/nvn/vulkan/device.c`.
- [x] 4.2 Adicionar detecção e fallbacks de features opcionais em `device.c`, `exec.c`, `textures.c` (`depthClamp`, `dualSrcBlend`, `samplerAnisotropy`, `fillModeNonSolid`, `shaderClipDistance`).
- [x] 4.3 Tratar rotação, redimensionamento dinâmico e recriação de Swapchain (`VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`).
- [x] 4.4 Validar compilação do backend gráfico Vulkan e registrar em `ANDROID_BUILD_ERRORS.log`.

### Fase 5: Input, Controles Touch e ImGui Mobile
- [x] 5.1 Ajustar scaling de DPI do Dear ImGui para telas mobile de alta densidade em `runtime/ui/menu.mm`, `launcher.mm` e `imgui_glue.cpp`.
- [x] 5.2 Mapear botão Back do Android (`SDL_SCANCODE_AC_BACK`) para alternar menu in-game em `runtime/ui/menu.mm`.
- [x] 5.3 Implementar overlay de controles Touch (analógicos, D-Pad, botões Switch A/B/X/Y/L/R/ZL/ZR) alimentando o runtime em `runtime/hle/input.c` / `runtime/ui/touch_overlay.*`.
- [x] 5.4 Testes finais, verificação e fechamento do port.

---

## Mapeamento Arquitetural de Arquivos Afetados

| Arquivo | Dependências de SO / Hardware Mapeadas | Alterações Necessárias |
|---|---|---|
| `runtime/platform/linux.c` | `execinfo.h`, `mmap`, `MAP_FIXED_NOREPLACE`, `isatty`, `gettid`, `pthread_setschedparam`, `posix_spawn`, `nftw` | Remover `<execinfo.h>` no Android; implementar backtrace compatível com Bionic; adicionar espelhamento para `__android_log_print`; desativar `posix_spawn` de processos externos em sandbox do Android. |
| `runtime/main.c` | Constructor `reserve_guest_space`, re-exec via `plat_exec`, loop de reboot, SDL main entry point | Tratar inicialização no Android sem tentar re-exec por `/proc/self/exe`; integrar entry point `SDL_main`. Ajustar buffer `realpath` para `PATH_MAX`. |
| `runtime/paths.c` | XDG paths (`XDG_DATA_HOME`, `~/.local/share`), caminhos locais | Usar `SDL_GetPrefPath("Apolinario", "SkyRecomp")` e caminhos `/sdcard/Android/data/...` para carregar `romfs`, `exefs` e `saves`. |
| `runtime/gamelib.c`, `runtime/gamelib.h` | `GAMELIB_NAME ("skygame.so")`, `dlopen` | No Android, definir `GAMELIB_NAME` como `"libskygame.so"`; suportar resolução direta de biblioteca no pacote do APK. |
| `runtime/rt.h` | Macros `LOG` e `FATAL` | Espelhar mensagens para o subsistema Android Logcat (`ANDROID_LOG_INFO`, `ANDROID_LOG_ERROR`). |
| `runtime/nvn/vulkan/device.c` | Criação de dispositivo Vulkan, features obrigatórias vs opcionais, swapchain | Suportar GPUs mobile (Mali, Adreno); condicionar `depthClamp`, `fillModeNonSolid`, `dualSrcBlend`, `samplerAnisotropy`; tratar `VK_SUBOPTIMAL_KHR` e rotação de tela. Substituir `SDL_SavePNG` por `SDL_SaveBMP`. Encaminhar eventos de touch para o overlay. |
| `runtime/nvn/vulkan/exec.c` | Pipelines gráficos que assumiam features desktop | Desativar `depthClampEnable` e `polygonMode` de linha se não suportados pelo hardware mobile. Fallback seguro para SRC1 blend factors. |
| `runtime/nvn/vulkan/textures.c` | Amostradores com anisotropia | Condicionar `anisotropyEnable = VK_TRUE` à feature `samplerAnisotropy` do dispositivo físico. |
| `runtime/ui/menu.mm`, `runtime/ui/launcher.mm` | Fontes de sistema (`/usr/share/fonts`, Windows, Mac), DPI estático, atalho `ESC` / `F1` | Incluir `/system/fonts/Roboto-Regular.ttf` e fallbacks; aplicar escala de DPI do display SDL3; interceptar `SDL_SCANCODE_AC_BACK`; renderizar overlay de toque. |
| `runtime/hle/input.c` | Entrada de gamepad/teclado/mouse | Integrar alimentação atômica de estado pelo overlay de controles touch mobile (`touch_set_buttons`, `touch_set_sticks`). |
| `runtime/ui/touch_overlay.*` (novo) | Overlay gráfico de controles táteis | Controles virtuais (sticks esquerdo/direito, botões A/B/X/Y, D-Pad, L/ZL/R/ZR, +/-, Menu) com multi-touch e feedback visual. |
| `android/` (novo) | Projeto Gradle + CMakeLists | Sistema de compilação completo para Android NDK r28, arm64-v8a, empacotando APK. |

---

## Arquivos Modificados/Criados
- `ANDROID_PORT_STATE.md`: Checkpoint de progresso, checklist e mapeamento arquitetural.
- `CMakeLists.txt`: Root CMake configurado para targets `skygame` e `main` (libmain.so) com NDK e libs importadas (SDL3, shaderc_combined).
- `android/`: Projeto Gradle completo (`build.gradle.kts`, `settings.gradle.kts`, `app/build.gradle.kts`, `AndroidManifest.xml`, `SDLActivity.java`).
- `runtime/platform/linux_compat.h`: Compatibilidade Bionic Android (remover `__GLIBC_PREREQ` e usar nativas).
- `runtime/platform/linux.c`: Logcat mirroring, TBI mask, unwinding com `<unwind.h>`, MAP_FIXED fallback, dual logging.
- `runtime/rt.h`: Macros LOG/FATAL em formato de expressão sequencial com Logcat.
- `runtime/paths.c`: `SDL_GetPrefPath` e rota `/sdcard/Android/data/com.apolinario.skyrecomp/files`.
- `runtime/main.c`: `PATH_MAX` buffer para `realpath`, inclusão de `<limits.h>` e `<SDL3/SDL_main.h>`.
- `runtime/gamelib.c`, `runtime/gamelib.h`: `"libskygame.so"` e dlopen direto de libdir.
- `runtime/hle/movie.c`: Stubs HLE de reprodução de vídeo sem dependência externa de FFmpeg.
- `runtime/ui/launcher.mm`: Protegida dependência de FFmpeg com `#if !defined(__ANDROID__)`, fontes Android e DPI scaling.
- `runtime/nvn/vulkan/device.c`: `SDL_SaveBMP` no Android em `vk_save_png`; fallbacks de features mobile; suporte a rotação e `VK_SUBOPTIMAL_KHR`; integração de touch events em `ui_run`.
- `runtime/nvn/vulkan/vk.h`: Flags de capacidades gráficas opcionais.
- `runtime/nvn/vulkan/exec.c`: Fallbacks de `depthClampEnable`, `polygonMode` e `blend_factor`.
- `runtime/nvn/vulkan/textures.c`: Amostradores com anisotropia condicionados a `samplerAnisotropy`.
- `runtime/nvn/custom_tex.c`: `SDL_LoadBMP` no Android em `load`.
- `runtime/hle/input.c`: Entrada de toque atômica (`touch_set_buttons`, `touch_set_sticks`) integrada ao polling de HID.
- `runtime/ui/touch_overlay.h`, `runtime/ui/touch_overlay.cpp`: Implementação completa do overlay tátil mobile com escala e opacidade customizáveis.
- `runtime/ui/controls_ui.mm`: Seção de configuração de Touch Screen no menu de controles do jogo com sliders de opacidade e escala.
- `runtime/settings.h`, `runtime/settings.c`: Persistência de preferências de toque (`touch_enabled`, `touch_opacity`, `touch_scale`) no arquivo `skyrecomp.ini`.
- `runtime/ui/menu.mm`: Suporte a `SDL_SCANCODE_AC_BACK`, `menu_toggle`, fontes Android e rendering do overlay de toque.
- `runtime/nvn/vulkan/device.c`: Salvamento automático de pipeline cache Vulkan ao entrar em background (`SDL_EVENT_WILL_ENTER_BACKGROUND` / `SDL_EVENT_DID_ENTER_BACKGROUND`).
- `tools/build_game_android.py`: Construtor multi-thread automatizado para NDK Clang ARM64-v8a gerando `libskygame.so` a partir do ExeFS do Switch.
- `tools/gen_smaa.py`, `third_party/smaa/smaa_hlsl.inc`: Gerador do include de shader SMAA.
- `tools/skygame_stubs.c`: Stubs de tabelas de símbolos para compilação antes da extração da ROM.
- `android/app/src/main/jniLibs/arm64-v8a/libskygame.so`: Biblioteca nativa do jogo compilada e embarcada.

---

## Histórico de Erros Resolvidos
1. **NDK ausente inicialmente**: Instalado NDK 28.2.13676358 via Gradle Wrapper.
2. **Dependência de FFmpeg em movie.c**: Criados stubs HLE limpos para retorno de EOS no Android.
3. **Macro LOG em expressões vírgula**: Convertida de `do { ... } while(0)` para `(expr, (void)0)`.
4. **SDL_LoadPNG / SDL_SavePNG não expostos no SDL3 padrão**: Substituídos por `SDL_LoadBMP` / `SDL_SaveBMP` sob `#if defined(__ANDROID__)`.
5. **realpath buffer < PATH_MAX**: Buffer `arg_root` expandido para `PATH_MAX` (4096 bytes) com `<limits.h>`.
6. **smaa_hlsl.inc inexistente**: Gerado via `tools/gen_smaa.py` a partir de `SMAA.hlsl`.
7. **shaderc e SDL3 no Android**: SDL3 oficial AAR 3.4.20 extraído para `third_party/SDL3/lib/arm64-v8a/libSDL3.so` e `app/libs/SDL3.aar`. Shaderc compilado via NDK `ndk-build` e empacotado em `libshaderc_combined.a` linkado estaticamente em `libmain.so`.
8. **Símbolos Vulkan 1.3 ausentes no stub API 29**: Configurado `-DANDROID_PLATFORM=android-33` para prover os stubs do Vulkan 1.3 (`vkCmdBeginRendering`, `vkQueueSubmit2`, etc.).
9. **Forward declaration de touch_overlay_render no menu.mm**: Corrigida linkage C em escopo de arquivo dentro de `extern "C"`.
10. **Crash no boot do APK**: Corrigido com inclusão de `libc++_shared` no `MainActivity.java`, criação física de diretório via `getExternalFilesDir(null)`, fallback de Vulkan 1.3 -> 1.2 -> 1.1 em `device.c` e eliminação de unmap destrutivo no `mmap`.
11. **Giroscópio / Motion Aiming**: Sensores `SDL_SENSOR_GYRO` e `SDL_SENSOR_ACCEL` do smartphone abertos e mapeados em tempo real para a emulação do MotionPlus do Switch.


