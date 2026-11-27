package io.github.alttprecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

/**
 * Home screen: stage the player's own ROM, tune settings, start the game.
 * Plain framework views keep the APK small and dependency-free.
 */
public class LauncherActivity extends Activity {
    private static final int REQ_PICK_ROM = 1;

    private Settings settings;
    private TextView status;
    private Button play;
    private Button pick;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        settings = Settings.load(this);
        setContentView(buildUi());
        refreshRomState();
    }

    // ------------------------------------------------------------------ UI

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                getResources().getDisplayMetrics());
    }

    private int color(int id) { return getResources().getColor(id, getTheme()); }

    private View buildUi() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(32), dp(24), dp(24));
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        scroll.addView(root, new ViewGroup.LayoutParams(-1, -2));

        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        root.addView(column, new LinearLayout.LayoutParams(Math.min(dp(560),
                getResources().getDisplayMetrics().widthPixels - dp(48)), -2));

        TextView title = text("A Link to the Past", 28, R.color.text);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        column.addView(title);
        TextView sub = text("Port Android · moteur snesrecomp", 14, R.color.accent);
        sub.setPadding(0, dp(4), 0, dp(20));
        column.addView(sub);

        LinearLayout card = card();
        status = text("", 15, R.color.text_dim);
        card.addView(status);
        pick = button("Choisir ma ROM", false);
        pick.setOnClickListener(v -> pickRom());
        card.addView(pick, buttonParams());
        play = button("Jouer", true);
        play.setOnClickListener(v -> startGame());
        card.addView(play, buttonParams());
        column.addView(card, cardParams());

        LinearLayout opts = card();
        opts.addView(header("Affichage"));
        opts.addView(toggle("Widescreen adaptatif",
                "Élargit la vue selon le format de l'écran (jusqu'à ~21:9)",
                settings.widescreen, v -> settings.widescreen = v));
        opts.addView(toggle("Lissage des pixels", "Filtrage bilinéaire au lieu de pixels nets",
                settings.linearFilter, v -> settings.linearFilter = v));
        opts.addView(header("Contrôles"));
        opts.addView(toggle("Masquer le pavé tactile avec une manette",
                "Réapparaît dès que vous touchez l'écran",
                settings.autoHideTouch, v -> settings.autoHideTouch = v));
        opts.addView(toggle("Vibration", "Retour haptique des boutons tactiles",
                settings.haptics, v -> settings.haptics = v));
        opts.addView(slider("Opacité des boutons", 15, 100, settings.touchOpacity,
                v -> settings.touchOpacity = v));
        opts.addView(slider("Taille des boutons", 60, 150, settings.touchScale,
                v -> settings.touchScale = v));
        column.addView(opts, cardParams());

        TextView legal = text("Aucun contenu du jeu n'est inclus dans cette application. "
                + "Utilisez votre propre copie légale de « The Legend of Zelda: A Link to the "
                + "Past » (USA, v1.0). Manettes Bluetooth/USB prises en charge automatiquement ; "
                + "bouton Guide ou Retour pour le menu pause.", 12, R.color.text_dim);
        legal.setPadding(dp(4), dp(8), dp(4), 0);
        column.addView(legal);
        return scroll;
    }

    private TextView text(String s, float sp, int colorId) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color(colorId));
        return t;
    }

    private TextView header(String s) {
        TextView t = text(s.toUpperCase(), 12, R.color.accent);
        t.setLetterSpacing(0.1f);
        t.setPadding(0, dp(12), 0, dp(4));
        return t;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(18), dp(16), dp(18), dp(18));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(color(R.color.card));
        bg.setCornerRadius(dp(16));
        c.setBackground(bg);
        return c;
    }

    private LinearLayout.LayoutParams cardParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.bottomMargin = dp(16);
        return lp;
    }

    private Button button(String label, boolean primary) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(16);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(12));
        bg.setColor(primary ? color(R.color.accent) : 0x00000000);
        if (!primary) bg.setStroke(dp(1), color(R.color.accent));
        b.setBackground(bg);
        b.setTextColor(primary ? color(R.color.bg) : color(R.color.accent));
        return b;
    }

    private LinearLayout.LayoutParams buttonParams() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, dp(52));
        lp.topMargin = dp(12);
        return lp;
    }

    interface BoolSink { void set(boolean v); }
    interface IntSink { void set(int v); }

    private View toggle(String label, String hint, boolean value, BoolSink sink) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(8), 0, dp(8));
        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);
        texts.addView(text(label, 15, R.color.text));
        texts.addView(text(hint, 12, R.color.text_dim));
        row.addView(texts, new LinearLayout.LayoutParams(0, -2, 1f));
        Switch sw = new Switch(this);
        sw.setChecked(value);
        sw.setThumbTintList(ColorStateList.valueOf(color(R.color.accent)));
        sw.setOnCheckedChangeListener((b, v) -> { sink.set(v); settings.save(this); });
        row.addView(sw);
        row.setOnClickListener(v -> sw.toggle());
        return row;
    }

    private View slider(String label, int min, int max, int value, IntSink sink) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, dp(8), 0, dp(4));
        TextView t = text(label + " : " + value + " %", 15, R.color.text);
        box.addView(t);
        SeekBar sb = new SeekBar(this);
        sb.setMax(max - min);
        sb.setProgress(value - min);
        sb.setProgressTintList(ColorStateList.valueOf(color(R.color.accent)));
        sb.setThumbTintList(ColorStateList.valueOf(color(R.color.accent)));
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                t.setText(label + " : " + (p + min) + " %");
                sink.set(p + min);
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) { settings.save(LauncherActivity.this); }
        });
        box.addView(sb);
        return box;
    }

    // ----------------------------------------------------------------- ROM

    private void refreshRomState() {
        status.setText("Vérification de la ROM…");
        play.setEnabled(false);
        new Thread(() -> {
            boolean ok = RomManager.hasValidRom(this);
            ui.post(() -> showRomState(ok, null));
        }).start();
    }

    private void showRomState(boolean ok, String error) {
        if (error != null) {
            status.setText("✗ " + error);
            status.setTextColor(color(R.color.err));
        } else if (ok) {
            status.setText("✓ ROM vérifiée : A Link to the Past (USA)");
            status.setTextColor(color(R.color.ok));
        } else {
            status.setText("Aucune ROM. Sélectionnez votre fichier .sfc, .smc ou .zip.");
            status.setTextColor(color(R.color.text_dim));
        }
        play.setEnabled(ok);
        play.setAlpha(ok ? 1f : 0.4f);
        pick.setText(ok ? "Changer de ROM" : "Choisir ma ROM");
        if (ok) play.requestFocus(); else pick.requestFocus();
    }

    private void pickRom() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");   // .sfc has no registered MIME type
        startActivityForResult(i, REQ_PICK_ROM);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK_ROM || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        status.setText("Vérification…");
        status.setTextColor(color(R.color.text_dim));
        new Thread(() -> {
            try {
                RomManager.stage(this, uri);
                ui.post(() -> showRomState(true, null));
            } catch (RomManager.RomException e) {
                boolean stillOk = RomManager.hasValidRom(this);
                ui.post(() -> {
                    showRomState(stillOk, e.getMessage());
                });
            }
        }).start();
    }

    private void startGame() {
        settings.save(this);
        settings.writeEngineOverrides(this);
        startActivity(new Intent(this, GameActivity.class));
    }
}
