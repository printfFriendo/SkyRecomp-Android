package com.apolinario.skyrecomp;

import android.os.Bundle;
import android.util.Log;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    private static final String TAG = "SkyRecomp";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
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
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "SDL3",
            "skygame",
            "main"
        };
    }
}
