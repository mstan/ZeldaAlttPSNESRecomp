# Android port

Native Android build of ZeldaALttPSNESRecomp: one APK, **no game code or data
inside**. The player supplies their own legally dumped *A Link to the Past (USA,
v1.0)* ROM (`.sfc`, `.smc` or a `.zip` containing it; SHA-256
`66871d66…6009cfb`, 1 MiB, copier header stripped automatically).

## How it works without game code

The distributable build (`ZELDA_INTERP_ONLY=ON`, the default) does not link the
recompiled `src/gen` tree. `jni/zelda_interp_module.c` provides empty dispatch
tables and routes the few entry points the Zelda host calls (`I_RESET`,
`Interrupt_NMI`, `I_IRQ`, the main-loop and polyhedral slices) to the
snesrecomp **interpreter floor**, which executes the player's ROM directly. The
entry addresses come from the public `recomp/bank*.cfg` declarations. The main
loop uses the upstream LLE scheduler (`ZELDA_SCHEDULER_IMPL=LLE`).

PPU, APU/DSP, DMA and the rest of the hardware model are the shared snesrecomp
runtime, compiled from source.

`-Pzelda.aot=true` builds the AOT variant from `src/gen` (generated from your
own ROM with `bash tools/regen.sh`). It embeds ROM-derived code: **local
testing only, never distribute it.**

## Features

- **Physical controllers**: Bluetooth/USB gamepads through SDL3's Android
  controller stack (Xbox, PlayStation, Switch Pro, 8BitDo…), plus physical
  keyboards (`keybinds.ini`). Mapping follows `config.ini` `[GamepadMap]`.
- **Any resolution/aspect**: adaptive widescreen keeps the 224-line height and
  widens the view to the screen ratio (256 → 446 px), portrait or landscape,
  notches/cutouts respected. Can be turned off in the launcher.
- **Touch pad**: 8-way D-pad, A/B/X/Y, L/R, Select/Start, pause menu button.
  Multitouch with sliding between buttons, haptics, adjustable opacity and
  size. It **fades out as soon as a physical controller or keyboard is used**
  and **fades back in on the next screen touch** (or when the last controller
  is unplugged).
- **Pause menu** (on-screen ☰, Back, or the gamepad Guide button): resume,
  quick save/load state, show/hide touch controls, quit.
- Cartridge save (`saves/save.srm`) is flushed whenever the app goes to the
  background.

## Layout

```
android/
  CMakeLists.txt              native build: snesrecomp runtime + Zelda host + SDL3 (static)
  jni/zelda_interp_module.c   interpreter-only program module (no game code)
  jni/zelda_android_jni.c     JNI bridge (touch mask, pause, quick state, quit)
  app/src/main/java/io/github/alttprecomp/
    LauncherActivity.java     ROM picker + SHA-256 check, settings, Play
    GameActivity.java         SDLActivity host (":game" process), input routing, menu
    TouchControlsView.java    on-screen pad
    RomManager.java           ROM staging (zip/header handling, verification)
    Settings.java             preferences -> config.local.ini
  app/src/main/java/org/libsdl/app/   SDL 3.4.10 Java glue (zlib license)
```

`src/main.c` carries small `#ifdef __ANDROID__` blocks: app-private data dir,
SDL_Renderer output, touch/menu input injection, background save flush.

## Build

Requirements: Android SDK with NDK `27.2.12479018`, CMake `3.31.6`,
platform 35; a JDK 17+ (Android Studio's bundled JBR works).

```sh
cd android
echo "sdk.dir=/path/to/Android/Sdk" > local.properties
./gradlew assembleDebug                    # arm64-v8a + x86_64
./gradlew assembleDebug -Pabis=arm64-v8a   # phones only
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

SDL 3.4.10 is downloaded and built from source on first configure
(`-Psdl3.dir=<extracted SDL3-3.4.10>` reuses a local copy).

## Status

Tested on an AYN Odin 2 (Snapdragon 8 Gen 2, Android 13, 1920x1080) with the
interpreter-only APK: boot, intro, title, player select and name entry run;
audio plays; the built-in "Odin Controller" is detected; adaptive widescreen
selects 398x224; the touch pad appears on touch and drives the game; the pause
menu and quick save work; pressing the built-in physical buttons fades the
touch pad out and touching the screen brings it back. The game process uses
~19 % of one core, so the interpreter floor is fast enough on this class of
device.

Not yet verified: a long play session, portrait layout, and lower-end phones.

Engine messages (stdout/stderr) go to logcat under the tag `zelda`.
