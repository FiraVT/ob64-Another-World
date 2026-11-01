package com.ogrebattle64.recomp;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.TextView;
import android.view.Gravity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;

/**
 * Prepares the app's internal storage before the game starts:
 *
 *  - copies the mods bundled in the APK (assets/mods/*.nrm) into files/mods,
 *    and enables the ones listed in assets/mods/enabled.txt the first time;
 *  - asks for the player's ROM once, converts a byte-swapped dump to .z64,
 *    checks its SHA-1 and stores it as files/ogre64.z64, where the native
 *    side looks for a ROM "beside the executable" (launcher.cpp).
 *
 * The runtime keeps its own validated copy (files/ogrebattle64-us-rev1.z64)
 * after the first boot, and this activity then deletes ogre64.z64.
 */
public class RomSetupActivity extends Activity {
    private static final int PICK_ROM = 1;
    private static final String ROM_NAME = "ogre64.z64";
    private static final String STORED_ROM_NAME = "ogrebattle64-us-rev1.z64";
    private static final String ROM_SHA1 = "d26b14bee422a09581ce438d45ba9fec9783fbb8";
    private static final String BUNDLED_ROM = "rom/ogre64.z64";

    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setTextSize(18);
        status.setPadding(48, 48, 48, 48);
        setContentView(status);

        try {
            installBundledMods();
        } catch (IOException e) {
            status.setText("Could not install the bundled mods: " + e.getMessage());
        }

        File stored = new File(getFilesDir(), STORED_ROM_NAME);
        File rom = new File(getFilesDir(), ROM_NAME);
        if (stored.isFile()) {
            if (rom.isFile()) {
                rom.delete();
            }
            launchGame();
        } else if (rom.isFile()) {
            launchGame();
        } else if (hasBundledRom()) {
            installBundledRom();
        } else {
            askForRom(null);
        }
    }

    /**
     * A personal build (OGRE_BUNDLE_ROM=1 tools/build.sh android) carries the
     * developer's own ROM as assets/rom/ogre64.z64. It goes through the same
     * byte-order and SHA-1 checks as a picked file.
     */
    private boolean hasBundledRom() {
        try (InputStream in = getAssets().open(BUNDLED_ROM)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    private void installBundledRom() {
        status.setText("Installing the game...");
        final Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            String error;
            try (InputStream in = getAssets().open(BUNDLED_ROM)) {
                error = importRomBytes(readAll(in));
            } catch (IOException e) {
                error = "Could not read the bundled ROM: " + e.getMessage();
            }
            final String result = error;
            main.post(() -> {
                if (result == null) {
                    launchGame();
                } else {
                    askForRom(result);
                }
            });
        }).start();
    }

    private void askForRom(String error) {
        status.setText((error != null ? error + "\n\n" : "")
                + "Choose your Ogre Battle 64 (USA, Rev A) ROM.\n(.z64, .n64 or .v64)");
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_ROM);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_ROM) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            status.setText("A ROM is needed to play. Restart the app to choose one.");
            return;
        }
        final Uri uri = data.getData();
        status.setText("Copying the ROM...");
        final Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            final String error = importRom(uri);
            main.post(() -> {
                if (error == null) {
                    launchGame();
                } else {
                    askForRom(error);
                }
            });
        }).start();
    }

    /** Copies the ROM into internal storage as big-endian .z64; null on success. */
    private String importRom(Uri uri) {
        byte[] data;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            data = readAll(in);
        } catch (IOException e) {
            return "Could not read that file: " + e.getMessage();
        }
        return importRomBytes(data);
    }

    /** Stores a ROM image as big-endian .z64 after its checks; null on success. */
    private String importRomBytes(byte[] data) {
        if (data.length < 0x1000) {
            return "That file is too small to be an N64 ROM.";
        }
        int b0 = data[0] & 0xFF, b1 = data[1] & 0xFF;
        if (b0 == 0x37 && b1 == 0x80) {
            // .v64: 16-bit byte-swapped.
            for (int i = 0; i + 1 < data.length; i += 2) {
                byte t = data[i]; data[i] = data[i + 1]; data[i + 1] = t;
            }
        } else if (b0 == 0x40 && b1 == 0x12) {
            // .n64: 32-bit little-endian.
            for (int i = 0; i + 3 < data.length; i += 4) {
                byte t0 = data[i], t1 = data[i + 1];
                data[i] = data[i + 3]; data[i + 1] = data[i + 2];
                data[i + 2] = t1; data[i + 3] = t0;
            }
        } else if (b0 != 0x80 || b1 != 0x37) {
            return "That file is not an N64 ROM.";
        }
        if (!ROM_SHA1.equals(sha1(data))) {
            return "That ROM is not Ogre Battle 64 (USA, Rev A).";
        }
        File out = new File(getFilesDir(), ROM_NAME);
        try (OutputStream os = new FileOutputStream(out)) {
            os.write(data);
        } catch (IOException e) {
            out.delete();
            return "Could not save the ROM: " + e.getMessage();
        }
        return null;
    }

    private void installBundledMods() throws IOException {
        AssetManager assets = getAssets();
        String[] names = assets.list("mods");
        if (names == null) {
            return;
        }
        File modsDir = new File(getFilesDir(), "mods");
        modsDir.mkdirs();
        for (String name : names) {
            if (!name.endsWith(".nrm")) {
                continue;
            }
            try (InputStream in = assets.open("mods/" + name);
                 OutputStream os = new FileOutputStream(new File(modsDir, name))) {
                os.write(readAll(in));
            }
        }
        // mods.json is the runtime's record of which mods are on. Write it only
        // once, so the player's own choices in the MODS tab are kept.
        File modsJson = new File(getFilesDir(), "mods.json");
        if (!modsJson.exists()) {
            String[] enabled;
            try (InputStream in = assets.open("mods/enabled.txt")) {
                enabled = new String(readAll(in), "UTF-8").trim().split("\\s+");
            } catch (IOException e) {
                enabled = new String[0];
            }
            StringBuilder list = new StringBuilder();
            for (String id : enabled) {
                if (id.isEmpty()) continue;
                if (list.length() > 0) list.append(", ");
                list.append('"').append(id).append('"');
            }
            String json = "{\n    \"enabled_mods\": [" + list + "],\n"
                    + "    \"latest_game_mode\": \"\",\n"
                    + "    \"mod_order\": [" + list + "]\n}\n";
            try (OutputStream os = new FileOutputStream(modsJson)) {
                os.write(json.getBytes("UTF-8"));
            }
        }
    }

    private void launchGame() {
        startActivity(new Intent(this, OgreActivity.class));
        finish();
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream(1 << 20);
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) {
            bos.write(buf, 0, n);
        }
        return bos.toByteArray();
    }

    private static String sha1(byte[] data) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-1").digest(data);
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format("%02x", b & 0xFF));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }
}

