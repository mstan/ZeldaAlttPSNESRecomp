package io.github.alttprecomp;

import android.app.AlertDialog;
import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

/**
 * Hosts the native game (libmain.so) and the touch overlay.
 *
 * Runs in its own process (":game", see the manifest) so the native engine's
 * static state is always fresh and the process simply ends when the game
 * returns to the launcher.
 *
 * Input routing:
 *  - Physical gamepads/keyboards go to SDL untouched (SDL's Android controller
 *    stack), and their first use fades the touch pad out.
 *  - Touching the screen fades the touch pad back in.
 *  - Touch buttons reach the engine through JNI as a 12-bit SNES pad mask.
 */
public class GameActivity extends SDLActivity
        implements TouchControlsView.Listener, InputManager.InputDeviceListener {

    static native void nativeSetTouchButtons(int mask);
    static native void nativeSetPaused(boolean paused);
    static native void nativeQuickState(boolean save);
    static native void nativeRequestQuit();

    private TouchControlsView touch;
    private Settings settings;
    private InputManager inputManager;
    private AlertDialog menu;
    private boolean forcedHidden;   // user chose "hide touch controls"
    private final Handler ui = new Handler(Looper.getMainLooper());

    // ------------------------------------------------------------ SDL hooks

    @Override
    protected String[] getLibraries() {
        // SDL3 is linked statically into libmain.so.
        return new String[]{"main"};
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected String[] getArguments() {
        return new String[]{RomManager.romFile(this).getAbsolutePath()};
    }

    // ------------------------------------------------------------- lifecycle

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) return;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        settings = Settings.load(this);
        touch = new TouchControlsView(this, this);
        touch.applySettings(settings);
        mLayout.addView(touch, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        inputManager.registerInputDeviceListener(this, ui);
        // Start hidden if a controller is already connected and auto-hide is on;
        // the first touch brings the pad back anyway.
        if (settings.autoHideTouch && hasPhysicalController()) touch.setControlsVisible(false);
    }

    @Override
    protected void onDestroy() {
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        boolean finishing = isFinishing();
        super.onDestroy();
        // Dedicated process: end it so the next launch starts the engine fresh.
        if (finishing) System.exit(0);
    }

    // ----------------------------------------------- physical input detection

    private static boolean isPhysicalController(InputEvent e) {
        InputDevice d = e.getDevice();
        if (d == null || d.isVirtual()) return false;
        int s = e.getSource();
        if ((s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || (s & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD) {
            return true;
        }
        return (s & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
                && d.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
    }

    private static boolean isControllerDevice(InputDevice d) {
        if (d == null || d.isVirtual()) return false;
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private boolean hasPhysicalController() {
        for (int id : InputDevice.getDeviceIds()) {
            if (isControllerDevice(InputDevice.getDevice(id))) return true;
        }
        return false;
    }

    private void onPhysicalInput() {
        if (touch != null && settings.autoHideTouch && touch.isShown2()) {
            touch.setControlsVisible(false);
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        boolean volume = code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN
                || code == KeyEvent.KEYCODE_VOLUME_MUTE;
        if (!volume && isPhysicalController(event)) {
            onPhysicalInput();
            // Guide/Home button on a gamepad opens the in-game menu.
            if (code == KeyEvent.KEYCODE_BUTTON_MODE || code == KeyEvent.KEYCODE_MENU) {
                if (event.getAction() == KeyEvent.ACTION_UP) toggleMenu();
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        if (isPhysicalController(ev) && ev.getAction() == MotionEvent.ACTION_MOVE) {
            final int[] axes = {MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_HAT_X,
                    MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                    MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER};
            for (int axis : axes) {
                if (Math.abs(ev.getAxisValue(axis)) > 0.4f) {
                    onPhysicalInput();
                    break;
                }
            }
        }
        return super.dispatchGenericMotionEvent(ev);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        // Any finger on the screen brings the touch pad back.
        if (ev.getActionMasked() == MotionEvent.ACTION_DOWN && touch != null
                && !touch.isShown2() && !forcedHidden) {
            touch.setControlsVisible(true);
        }
        return super.dispatchTouchEvent(ev);
    }

    @Override public void onInputDeviceAdded(int deviceId) { }
    @Override public void onInputDeviceChanged(int deviceId) { }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        // Last controller unplugged: show the touch pad immediately.
        if (touch != null && !forcedHidden && !hasPhysicalController()) {
            touch.setControlsVisible(true);
        }
    }

    // ------------------------------------------------------- touch callbacks

    @Override
    public void onTouchButtons(int mask) {
        nativeSetTouchButtons(mask);
    }

    @Override
    public void onMenuRequested() {
        toggleMenu();
    }

    @Override
    public void onBackPressed() {
        toggleMenu();
    }

    // ------------------------------------------------------------------ menu

    private void toggleMenu() {
        if (menu != null && menu.isShowing()) {
            menu.dismiss();
            return;
        }
        nativeSetTouchButtons(0);
        nativeSetPaused(true);
        String touchItem = (touch != null && touch.isShown2())
                ? "Masquer les contrôles tactiles" : "Afficher les contrôles tactiles";
        String[] items = {"Reprendre", "Sauvegarde rapide", "Chargement rapide", touchItem,
                "Quitter vers le menu"};
        menu = new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Pause")
                .setItems(items, (d, which) -> {
                    switch (which) {
                        case 1:
                            nativeQuickState(true);
                            Toast.makeText(this, "État sauvegardé", Toast.LENGTH_SHORT).show();
                            break;
                        case 2:
                            nativeQuickState(false);
                            Toast.makeText(this, "État chargé", Toast.LENGTH_SHORT).show();
                            break;
                        case 3:
                            boolean show = !touch.isShown2();
                            forcedHidden = !show;
                            touch.setControlsVisible(show);
                            break;
                        case 4:
                            nativeRequestQuit();
                            break;
                        default:
                            break;
                    }
                })
                .setOnDismissListener(d -> nativeSetPaused(false))
                .create();
        menu.show();
    }
}
