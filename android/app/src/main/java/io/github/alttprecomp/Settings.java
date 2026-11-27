package io.github.alttprecomp;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/** Player preferences, persisted in SharedPreferences and mirrored to the engine. */
final class Settings {
    private static final String PREFS = "settings";

    boolean widescreen = true;      // adaptive widescreen: follow the screen ratio
    boolean linearFilter = false;   // smooth vs. sharp pixels
    boolean autoHideTouch = true;   // hide the touch pad while a gamepad is used
    boolean haptics = true;
    int touchOpacity = 60;          // percent
    int touchScale = 100;           // percent

    static Settings load(Context ctx) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        Settings s = new Settings();
        s.widescreen = p.getBoolean("widescreen", s.widescreen);
        s.linearFilter = p.getBoolean("linearFilter", s.linearFilter);
        s.autoHideTouch = p.getBoolean("autoHideTouch", s.autoHideTouch);
        s.haptics = p.getBoolean("haptics", s.haptics);
        s.touchOpacity = p.getInt("touchOpacity", s.touchOpacity);
        s.touchScale = p.getInt("touchScale", s.touchScale);
        return s;
    }

    void save(Context ctx) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putBoolean("widescreen", widescreen)
                .putBoolean("linearFilter", linearFilter)
                .putBoolean("autoHideTouch", autoHideTouch)
                .putBoolean("haptics", haptics)
                .putInt("touchOpacity", touchOpacity)
                .putInt("touchScale", touchScale)
                .apply();
    }

    /**
     * Writes config.local.ini next to config.ini in app storage. The engine
     * parses it after config.ini, so these values win (see src/main.c).
     */
    void writeEngineOverrides(Context ctx) {
        String ini = "[Graphics]\n"
                + "Widescreen = " + (widescreen ? 1 : 0) + "\n"
                + "LinearFiltering = " + (linearFilter ? 1 : 0) + "\n"
                + "NoSpriteLimits = 1\n"
                + "[Sound]\n"
                + "EnableAudio = 1\n"
                + "AudioFreq = 48000\n"
                + "AudioSamples = 1024\n"
                + "[GamepadMap]\n"
                + "EnableGamepad1 = true\n"
                + "EnableGamepad2 = true\n";
        File f = new File(ctx.getFilesDir(), "config.local.ini");
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(ini.getBytes("UTF-8"));
        } catch (IOException ignored) {
        }
    }
}
