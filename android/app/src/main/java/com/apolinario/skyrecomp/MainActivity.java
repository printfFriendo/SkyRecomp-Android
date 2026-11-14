package com.apolinario.skyrecomp;

import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import android.view.Display;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    private static final String TAG = "SkyRecomp";
    public static final int CMD_HAPTIC_CLICK = 0x8001;
    public static final int REQ_PICK_ROM = 0x9001;
    public static final int REQ_PICK_KEYS = 0x9002;

    private static MainActivity sInstance = null;

    public static native void nativeOnFilePicked(int reqCode, int fd, String filename, long size);

    public static void openFilePicker(final int reqCode) {
        if (sInstance == null) {
            Log.e(TAG, "Cannot open file picker: MainActivity instance is null");
            return;
        }
        sInstance.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                    sInstance.startActivityForResult(intent, reqCode);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to launch ACTION_OPEN_DOCUMENT intent: " + e.getMessage());
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if ((requestCode == REQ_PICK_ROM || requestCode == REQ_PICK_KEYS) && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception ignored) {}

            String displayName = (requestCode == REQ_PICK_ROM) ? "game.nsp" : "prod.keys";
            long fileSize = 0;
            try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int nameIdx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    int sizeIdx = cursor.getColumnIndex(OpenableColumns.SIZE);
                    if (nameIdx != -1) displayName = cursor.getString(nameIdx);
                    if (sizeIdx != -1) fileSize = cursor.getLong(sizeIdx);
                }
            } catch (Exception e) {
                Log.w(TAG, "Could not resolve file metadata: " + e.getMessage());
            }

            try {
                ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                if (pfd != null) {
                    int fd = pfd.detachFd();
                    Log.i(TAG, "User selected file: " + displayName + " (" + fileSize + " bytes), fd=" + fd);
                    nativeOnFilePicked(requestCode, fd, displayName, fileSize);
                    return;
                }
            } catch (Exception e) {
                Log.e(TAG, "Failed to open ParcelFileDescriptor for " + uri + ": " + e.getMessage());
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        sInstance = this;
        final Thread.UncaughtExceptionHandler defaultHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread thread, Throwable throwable) {
                Log.e(TAG, "FATAL UNCAUGHT JAVA EXCEPTION in thread " + thread.getName(), throwable);
                try {
                    File[] dirs = new File[] { getExternalFilesDir(null), getFilesDir() };
                    for (File d : dirs) {
                        if (d != null) {
                            if (!d.exists()) d.mkdirs();
                            File crashFile = new File(d, "skyrecomp_java_crash.log");
                            java.io.PrintWriter pw = new java.io.PrintWriter(new java.io.FileWriter(crashFile, true));
                            pw.println("=== JAVA UNCAUGHT EXCEPTION ===");
                            pw.println("Date: " + new java.util.Date().toString());
                            pw.println("Thread: " + thread.getName());
                            throwable.printStackTrace(pw);
                            pw.println();
                            pw.flush();
                            pw.close();
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to write java crash log: " + e.getMessage());
                }
                if (defaultHandler != null) {
                    defaultHandler.uncaughtException(thread, throwable);
                } else {
                    System.exit(1);
                }
            }
        });

        try {
            // Configure display cutout (camera hole punch / notch) to allow true edge-to-edge drawing
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }

            // Keep screen on while playing
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

            // Request highest display refresh rate (90Hz / 120Hz / 144Hz) for smooth high FPS gameplay
            configureHighRefreshRate();

            applyImmersiveMode();

            // Guarantee Android external files directory exists
            File extFiles = getExternalFilesDir(null);
            if (extFiles != null) {
                if (!extFiles.exists()) extFiles.mkdirs();
                File nomedia = new File(extFiles, ".nomedia");
                if (!nomedia.exists()) nomedia.createNewFile();
                Log.i(TAG, "External files directory initialized at: " + extFiles.getAbsolutePath());
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to initialize external files directory: " + e.getMessage());
        }
        try {
            System.loadLibrary("c++_shared");
            System.loadLibrary("SDL3");
            System.loadLibrary("main");
            try {
                System.loadLibrary("skygame");
                Log.i(TAG, "libskygame.so loaded into process address space");
            } catch (Throwable t) {
                Log.w(TAG, "libskygame.so dynamic load: " + t.getMessage());
            }
        } catch (Throwable t) {
            Log.e(TAG, "Core library pre-load notice: " + t.getMessage());
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    private void applyImmersiveMode() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                getWindow().setDecorFitsSystemWindows(false);
                WindowInsetsController controller = getWindow().getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_FULLSCREEN);
            }
        } catch (Exception ignored) {}
    }

    private void configureHighRefreshRate() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            try {
                Display display = null;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    display = getDisplay();
                } else {
                    WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
                    if (wm != null) display = wm.getDefaultDisplay();
                }
                if (display != null) {
                    Display.Mode[] modes = display.getSupportedModes();
                    Display.Mode bestMode = null;
                    float maxRate = 0.0f;
                    for (Display.Mode mode : modes) {
                        if (mode.getRefreshRate() > maxRate) {
                            maxRate = mode.getRefreshRate();
                            bestMode = mode;
                        }
                    }
                    if (bestMode != null && maxRate > 60.0f) {
                        WindowManager.LayoutParams params = getWindow().getAttributes();
                        params.preferredDisplayModeId = bestMode.getModeId();
                        getWindow().setAttributes(params);
                        Log.i(TAG, "Configured high refresh rate mode: " + maxRate + " Hz (mode ID " + bestMode.getModeId() + ")");
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "Could not set high refresh rate mode: " + e.getMessage());
            }
        }
    }

    @Override
    public boolean onUnhandledMessage(int command, Object param) {
        if (command == CMD_HAPTIC_CLICK) {
            triggerHapticClick();
            return true;
        }
        return super.onUnhandledMessage(command, param);
    }

    private void triggerHapticClick() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                VibratorManager vm = (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                if (vm != null) {
                    vm.getDefaultVibrator().vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_CLICK));
                }
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
                if (v != null) {
                    v.vibrate(VibrationEffect.createOneShot(20, VibrationEffect.DEFAULT_AMPLITUDE));
                }
            } else {
                Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
                if (v != null) {
                    v.vibrate(20);
                }
            }
        } catch (Exception ignored) {}
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "SDL3",
            "main"
        };
    }
}
