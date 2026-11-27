package io.github.alttprecomp;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

/**
 * On-screen SNES pad drawn over the game surface.
 *
 * Multitouch: every pointer is re-evaluated on each move, so a thumb can slide
 * from B to Y or roll around the D-pad. A pointer that lands on the D-pad keeps
 * steering it even if it drifts outside. The resulting 12-bit pad mask is sent
 * to the engine only when it changes.
 *
 * Visibility is driven by GameActivity: the pad fades out as soon as a physical
 * controller or keyboard is used, and fades back in on the next screen touch.
 */
public class TouchControlsView extends View {

    public interface Listener {
        void onTouchButtons(int mask);
        void onMenuRequested();
    }

    // Zelda/SNES pad bits.
    static final int BTN_B = 0x001, BTN_Y = 0x002, BTN_SELECT = 0x004, BTN_START = 0x008,
            BTN_UP = 0x010, BTN_DOWN = 0x020, BTN_LEFT = 0x040, BTN_RIGHT = 0x080,
            BTN_A = 0x100, BTN_X = 0x200, BTN_L = 0x400, BTN_R = 0x800;

    private static final int ZONE_NONE = 0, ZONE_DPAD = 1, ZONE_MENU = 2;

    private final Listener listener;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final SparseIntArray pointerZone = new SparseIntArray();

    // Geometry (px), recomputed in layoutControls().
    private float unit;
    private float dpadX, dpadY, dpadR;
    private float faceX, faceY, faceR, faceSpacing;
    private final RectF rectL = new RectF(), rectR = new RectF();
    private final RectF rectSelect = new RectF(), rectStart = new RectF(), rectMenu = new RectF();

    private int mask;
    private boolean shown = true;
    private float visibility = 1f;     // 0..1, animated
    private ValueAnimator fade;
    private float opacity = 0.6f;
    private float scale = 1f;
    private boolean haptics = true;

    public TouchControlsView(Context ctx, Listener listener) {
        super(ctx);
        this.listener = listener;
        stroke.setStyle(Paint.Style.STROKE);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        setHapticFeedbackEnabled(true);
    }

    void applySettings(Settings s) {
        opacity = Math.max(0.15f, Math.min(1f, s.touchOpacity / 100f));
        scale = Math.max(0.6f, Math.min(1.5f, s.touchScale / 100f));
        haptics = s.haptics;
        layoutControls();
        invalidate();
    }

    boolean isShown2() { return shown; }

    /** Fades the pad in/out. Hiding also releases every touch button. */
    void setControlsVisible(boolean visible) {
        if (visible == shown) return;
        shown = visible;
        if (!visible) {
            pointerZone.clear();
            updateMask(0);
        }
        if (fade != null) fade.cancel();
        fade = ValueAnimator.ofFloat(visibility, visible ? 1f : 0f);
        fade.setDuration(visible ? 150 : 400);
        fade.addUpdateListener(a -> {
            visibility = (float) a.getAnimatedValue();
            invalidate();
        });
        fade.start();
    }

    // ---------------------------------------------------------------- layout

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        layoutControls();
    }

    private void layoutControls() {
        int w = getWidth(), h = getHeight();
        if (w == 0 || h == 0) return;
        boolean portrait = h > w;
        unit = Math.min(w, h) / (portrait ? 9.5f : 10.5f) * scale;

        float insetL = 0, insetR = 0, insetB = 0, insetT = 0;
        WindowInsets wi = getRootWindowInsets();
        if (wi != null && Build.VERSION.SDK_INT >= 30) {
            Insets in = wi.getInsets(WindowInsets.Type.displayCutout()
                    | WindowInsets.Type.systemGestures());
            insetL = in.left; insetR = in.right; insetB = in.bottom * 0.5f; insetT = in.top;
        }
        float m = unit * 0.35f;

        dpadR = unit * 1.55f;
        dpadX = insetL + m + dpadR + unit * 0.3f;
        dpadY = h - insetB - m - dpadR - unit * (portrait ? 1.6f : 0.6f);

        faceR = unit * 0.6f;
        faceSpacing = unit * 1.05f;
        faceX = w - insetR - m - faceSpacing - faceR - unit * 0.3f;
        faceY = h - insetB - m - faceSpacing - faceR - unit * (portrait ? 1.6f : 0.5f);

        float sw = unit * 2.4f, sh = unit * 0.95f;
        float sy = portrait ? Math.min(dpadY, faceY) - dpadR - unit * 1.1f - sh : insetT + m + unit * 0.2f;
        rectL.set(insetL + m, sy, insetL + m + sw, sy + sh);
        rectR.set(w - insetR - m - sw, sy, w - insetR - m, sy + sh);

        float pw = unit * 1.5f, ph = unit * 0.6f, gap = unit * 0.35f;
        float py = h - insetB - m - ph;
        rectSelect.set(w / 2f - gap / 2 - pw, py, w / 2f - gap / 2, py + ph);
        rectStart.set(w / 2f + gap / 2, py, w / 2f + gap / 2 + pw, py + ph);

        float ms = unit * 0.8f;
        rectMenu.set(w / 2f - ms / 2, insetT + m, w / 2f + ms / 2, insetT + m + ms * 0.8f);
    }

    // ----------------------------------------------------------------- input

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        if (!shown) {
            // Hidden pad: GameActivity decides whether this touch revives it.
            return true;
        }
        int idx = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = e.getX(idx), y = e.getY(idx);
                int zone = ZONE_NONE;
                if (rectMenu.contains(x, y)) zone = ZONE_MENU;
                else if (dist(x, y, dpadX, dpadY) < dpadR * 1.6f) zone = ZONE_DPAD;
                pointerZone.put(e.getPointerId(idx), zone);
                break;
            }
            case MotionEvent.ACTION_POINTER_UP: {
                int id = e.getPointerId(idx);
                if (pointerZone.get(id) == ZONE_MENU && rectMenu.contains(e.getX(idx), e.getY(idx))) {
                    listener.onMenuRequested();
                }
                pointerZone.delete(id);
                break;
            }
            case MotionEvent.ACTION_UP: {
                int id = e.getPointerId(idx);
                if (pointerZone.get(id) == ZONE_MENU && rectMenu.contains(e.getX(idx), e.getY(idx))) {
                    listener.onMenuRequested();
                }
                pointerZone.clear();
                updateMask(0);
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                pointerZone.clear();
                updateMask(0);
                return true;
            default:
                break;
        }

        int newMask = 0;
        for (int i = 0; i < e.getPointerCount(); i++) {
            if ((action == MotionEvent.ACTION_POINTER_UP) && i == idx) continue;
            int id = e.getPointerId(i);
            float x = e.getX(i), y = e.getY(i);
            int zone = pointerZone.get(id, ZONE_NONE);
            if (zone == ZONE_MENU) continue;
            if (zone == ZONE_DPAD) newMask |= dpadMask(x, y);
            else newMask |= buttonMask(x, y);
        }
        updateMask(newMask);
        return true;
    }

    private int dpadMask(float x, float y) {
        float dx = x - dpadX, dy = y - dpadY;
        if (dx * dx + dy * dy < (dpadR * 0.22f) * (dpadR * 0.22f)) return 0;
        // 8 sectors of 45 degrees, centred on the cardinal directions.
        double a = Math.toDegrees(Math.atan2(-dy, dx));
        if (a < 0) a += 360;
        int sector = (int) ((a + 22.5) / 45.0) % 8;
        switch (sector) {
            case 0: return BTN_RIGHT;
            case 1: return BTN_RIGHT | BTN_UP;
            case 2: return BTN_UP;
            case 3: return BTN_UP | BTN_LEFT;
            case 4: return BTN_LEFT;
            case 5: return BTN_LEFT | BTN_DOWN;
            case 6: return BTN_DOWN;
            default: return BTN_DOWN | BTN_RIGHT;
        }
    }

    private int buttonMask(float x, float y) {
        float hit = faceR * 1.3f;
        int m = 0;
        if (dist(x, y, faceX + faceSpacing, faceY) < hit) m |= BTN_A;
        if (dist(x, y, faceX, faceY + faceSpacing) < hit) m |= BTN_B;
        if (dist(x, y, faceX, faceY - faceSpacing) < hit) m |= BTN_X;
        if (dist(x, y, faceX - faceSpacing, faceY) < hit) m |= BTN_Y;
        float slop = unit * 0.25f;
        if (inflate(rectL, slop, x, y)) m |= BTN_L;
        if (inflate(rectR, slop, x, y)) m |= BTN_R;
        if (inflate(rectSelect, slop, x, y)) m |= BTN_SELECT;
        if (inflate(rectStart, slop, x, y)) m |= BTN_START;
        return m;
    }

    // A tap can be shorter than one emulated frame (16.7 ms); the game polls the
    // pad once per frame and would miss it. Keep each press for at least
    // MIN_HOLD_MS before reporting its release.
    private static final long MIN_HOLD_MS = 50;
    private final long[] pressTime = new long[12];
    private int heldMask;              // bits still alive because of MIN_HOLD_MS
    private int touchMask;             // what the fingers actually press
    private final Runnable releaseHeld = () -> updateMask(touchMask);

    private void updateMask(int fingers) {
        touchMask = fingers;
        long now = android.os.SystemClock.uptimeMillis();
        long nextExpiry = Long.MAX_VALUE;
        heldMask = 0;
        for (int b = 0; b < 12; b++) {
            int bit = 1 << b;
            if ((fingers & bit) != 0) {
                if ((mask & bit) == 0) pressTime[b] = now;
            } else if ((mask & bit) != 0 && now - pressTime[b] < MIN_HOLD_MS) {
                heldMask |= bit;
                nextExpiry = Math.min(nextExpiry, pressTime[b] + MIN_HOLD_MS);
            }
        }
        removeCallbacks(releaseHeld);
        if (nextExpiry != Long.MAX_VALUE) postDelayed(releaseHeld, nextExpiry - now);
        int newMask = fingers | heldMask;
        if (newMask == mask) return;
        boolean pressedNew = (newMask & ~mask) != 0;
        mask = newMask;
        listener.onTouchButtons(mask);
        if (pressedNew && haptics) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY,
                    HapticFeedbackConstants.FLAG_IGNORE_VIEW_SETTING);
        }
        invalidate();
    }

    private static boolean inflate(RectF r, float s, float x, float y) {
        return x >= r.left - s && x <= r.right + s && y >= r.top - s && y <= r.bottom + s;
    }

    private static float dist(float x, float y, float cx, float cy) {
        float dx = x - cx, dy = y - cy;
        return (float) Math.sqrt(dx * dx + dy * dy);
    }

    // ------------------------------------------------------------------ draw

    @Override
    protected void onDraw(Canvas c) {
        if (visibility <= 0.01f || unit == 0) return;
        float a = opacity * visibility;
        stroke.setStrokeWidth(unit * 0.05f);

        drawDpad(c, a);

        drawFace(c, faceX + faceSpacing, faceY, "A", 0xFFD9443B, (mask & BTN_A) != 0, a);
        drawFace(c, faceX, faceY + faceSpacing, "B", 0xFFE8C33A, (mask & BTN_B) != 0, a);
        drawFace(c, faceX, faceY - faceSpacing, "X", 0xFF3B6FD9, (mask & BTN_X) != 0, a);
        drawFace(c, faceX - faceSpacing, faceY, "Y", 0xFF3BA65A, (mask & BTN_Y) != 0, a);

        drawPill(c, rectL, "L", (mask & BTN_L) != 0, a, unit * 0.4f);
        drawPill(c, rectR, "R", (mask & BTN_R) != 0, a, unit * 0.4f);
        drawPill(c, rectSelect, "SELECT", (mask & BTN_SELECT) != 0, a, unit * 0.24f);
        drawPill(c, rectStart, "START", (mask & BTN_START) != 0, a, unit * 0.24f);

        // Menu: three bars.
        fill.setColor(withAlpha(0xFF202020, a * 0.8f));
        c.drawRoundRect(rectMenu, unit * 0.15f, unit * 0.15f, fill);
        fill.setColor(withAlpha(Color.WHITE, a));
        float bx = rectMenu.width() * 0.25f, by = rectMenu.height();
        for (int i = 0; i < 3; i++) {
            float yy = rectMenu.top + by * (0.3f + 0.2f * i);
            c.drawRect(rectMenu.left + bx, yy - unit * 0.025f, rectMenu.right - bx, yy + unit * 0.025f, fill);
        }
    }

    private void drawDpad(Canvas c, float a) {
        fill.setColor(withAlpha(0xFF000000, a * 0.35f));
        c.drawCircle(dpadX, dpadY, dpadR * 1.12f, fill);
        float arm = dpadR * 0.36f;
        int[] dirs = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
        for (int d : dirs) {
            RectF r = new RectF();
            if (d == BTN_UP) r.set(dpadX - arm, dpadY - dpadR, dpadX + arm, dpadY - arm * 0.2f);
            else if (d == BTN_DOWN) r.set(dpadX - arm, dpadY + arm * 0.2f, dpadX + arm, dpadY + dpadR);
            else if (d == BTN_LEFT) r.set(dpadX - dpadR, dpadY - arm, dpadX - arm * 0.2f, dpadY + arm);
            else r.set(dpadX + arm * 0.2f, dpadY - arm, dpadX + dpadR, dpadY + arm);
            boolean on = (mask & d) != 0;
            fill.setColor(withAlpha(on ? 0xFFE0E0E0 : 0xFF3A3A3A, a));
            c.drawRoundRect(r, arm * 0.3f, arm * 0.3f, fill);
            stroke.setColor(withAlpha(0xFFFFFFFF, a * 0.5f));
            c.drawRoundRect(r, arm * 0.3f, arm * 0.3f, stroke);
        }
        fill.setColor(withAlpha(0xFF3A3A3A, a));
        c.drawRect(dpadX - arm, dpadY - arm, dpadX + arm, dpadY + arm, fill);
        // Arrow hints.
        fill.setColor(withAlpha(0xFFFFFFFF, a * 0.7f));
        float t = arm * 0.45f, o = dpadR * 0.68f;
        triangle(c, dpadX, dpadY - o - t, dpadX - t, dpadY - o + t * 0.5f, dpadX + t, dpadY - o + t * 0.5f);
        triangle(c, dpadX, dpadY + o + t, dpadX - t, dpadY + o - t * 0.5f, dpadX + t, dpadY + o - t * 0.5f);
        triangle(c, dpadX - o - t, dpadY, dpadX - o + t * 0.5f, dpadY - t, dpadX - o + t * 0.5f, dpadY + t);
        triangle(c, dpadX + o + t, dpadY, dpadX + o - t * 0.5f, dpadY - t, dpadX + o - t * 0.5f, dpadY + t);
    }

    private void triangle(Canvas c, float x1, float y1, float x2, float y2, float x3, float y3) {
        path.reset();
        path.moveTo(x1, y1);
        path.lineTo(x2, y2);
        path.lineTo(x3, y3);
        path.close();
        c.drawPath(path, fill);
    }

    private void drawFace(Canvas c, float x, float y, String label, int color, boolean on, float a) {
        fill.setColor(withAlpha(on ? lighten(color) : color, a * (on ? 1f : 0.85f)));
        c.drawCircle(x, y, faceR * (on ? 0.94f : 1f), fill);
        stroke.setColor(withAlpha(0xFFFFFFFF, a * 0.6f));
        c.drawCircle(x, y, faceR * (on ? 0.94f : 1f), stroke);
        text.setColor(withAlpha(0xFFFFFFFF, a));
        text.setTextSize(faceR * 0.85f);
        c.drawText(label, x, y - (text.descent() + text.ascent()) / 2, text);
    }

    private void drawPill(Canvas c, RectF r, String label, boolean on, float a, float size) {
        float rad = r.height() / 2;
        fill.setColor(withAlpha(on ? 0xFFE0E0E0 : 0xFF3A3A3A, a));
        c.drawRoundRect(r, rad, rad, fill);
        stroke.setColor(withAlpha(0xFFFFFFFF, a * 0.5f));
        c.drawRoundRect(r, rad, rad, stroke);
        text.setColor(withAlpha(on ? 0xFF202020 : 0xFFFFFFFF, a));
        text.setTextSize(size);
        c.drawText(label, r.centerX(), r.centerY() - (text.descent() + text.ascent()) / 2, text);
    }

    private static int withAlpha(int color, float a) {
        int alpha = (int) (Color.alpha(color) * Math.max(0f, Math.min(1f, a)));
        return (color & 0x00FFFFFF) | (alpha << 24);
    }

    private static int lighten(int c) {
        int r = Math.min(255, Color.red(c) + 60), g = Math.min(255, Color.green(c) + 60),
                b = Math.min(255, Color.blue(c) + 60);
        return Color.rgb(r, g, b);
    }
}
