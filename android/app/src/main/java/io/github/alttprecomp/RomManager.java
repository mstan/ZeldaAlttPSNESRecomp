package io.github.alttprecomp;

import android.content.Context;
import android.net.Uri;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Stages the user's own ROM into app-private storage.
 *
 * Nothing from the game ships in the APK: the player picks their legally
 * dumped "A Link to the Past (USA)" image, it is checked against the exact
 * digest the recompilation targets, and copied to files/zelda.sfc. A 512-byte
 * copier header is stripped and a .zip containing the image is accepted.
 */
final class RomManager {
    static final String ROM_FILE = "zelda.sfc";
    static final int ROM_SIZE = 1024 * 1024;
    private static final String EXPECTED_SHA256 =
            "66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb";

    static final class RomException extends Exception {
        RomException(String msg) { super(msg); }
    }

    private RomManager() {}

    static File romFile(Context ctx) {
        return new File(ctx.getFilesDir(), ROM_FILE);
    }

    /** True when a verified ROM is already staged. */
    static boolean hasValidRom(Context ctx) {
        File f = romFile(ctx);
        if (!f.isFile() || f.length() != ROM_SIZE) return false;
        try (InputStream in = new FileInputStream(f)) {
            return EXPECTED_SHA256.equals(sha256(readAll(in, ROM_SIZE + 1)));
        } catch (IOException e) {
            return false;
        }
    }

    /** Reads, verifies and atomically installs the ROM behind {@code uri}. */
    static void stage(Context ctx, Uri uri) throws RomException {
        byte[] data;
        try (InputStream raw = ctx.getContentResolver().openInputStream(uri)) {
            if (raw == null) throw new RomException("Impossible d'ouvrir le fichier.");
            data = readAll(raw, 16 * 1024 * 1024);
        } catch (IOException e) {
            throw new RomException("Lecture impossible : " + e.getMessage());
        }
        if (data.length >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4) {
            data = extractFromZip(data);
        }
        if (data.length == ROM_SIZE + 512) {
            byte[] stripped = new byte[ROM_SIZE];
            System.arraycopy(data, 512, stripped, 0, ROM_SIZE);
            data = stripped;
        }
        if (data.length != ROM_SIZE) {
            throw new RomException("Taille inattendue (" + data.length
                    + " octets). La ROM USA fait exactement 1 Mio.");
        }
        if (!EXPECTED_SHA256.equals(sha256(data))) {
            throw new RomException("Cette ROM ne correspond pas à « A Link to the Past (USA) » "
                    + "v1.0 non modifiée (SHA-256 différent).");
        }
        File dst = romFile(ctx);
        File tmp = new File(ctx.getFilesDir(), ROM_FILE + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.getFD().sync();
        } catch (IOException e) {
            throw new RomException("Écriture impossible : " + e.getMessage());
        }
        if (!tmp.renameTo(dst)) {
            tmp.delete();
            throw new RomException("Impossible d'installer la ROM.");
        }
        // The engine caches the ROM path; point it at the staged copy.
        try (FileOutputStream out = new FileOutputStream(new File(ctx.getFilesDir(), "rom.cfg"))) {
            out.write((dst.getAbsolutePath() + "\n").getBytes("UTF-8"));
        } catch (IOException ignored) {
        }
    }

    private static byte[] extractFromZip(byte[] zip) throws RomException {
        try (ZipInputStream zin = new ZipInputStream(new java.io.ByteArrayInputStream(zip))) {
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                String n = e.getName().toLowerCase();
                if (!e.isDirectory() && (n.endsWith(".sfc") || n.endsWith(".smc"))) {
                    return readAll(zin, 16 * 1024 * 1024);
                }
            }
        } catch (IOException ex) {
            throw new RomException("Archive ZIP illisible : " + ex.getMessage());
        }
        throw new RomException("Aucun fichier .sfc/.smc dans l'archive.");
    }

    private static byte[] readAll(InputStream in, int limit) throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream(ROM_SIZE + 1024);
        byte[] buf = new byte[64 * 1024];
        int n;
        while ((n = in.read(buf)) > 0) {
            bos.write(buf, 0, n);
            if (bos.size() > limit) throw new IOException("fichier trop volumineux");
        }
        return bos.toByteArray();
    }

    private static String sha256(byte[] data) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-256").digest(data);
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format("%02x", b & 0xff));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }
}
