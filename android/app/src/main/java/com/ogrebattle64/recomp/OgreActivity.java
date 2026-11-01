package com.ogrebattle64.recomp;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/** The game itself: SDL's activity, loading the recompiled game from libmain.so. */
public class OgreActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // libc++_shared first: libSDL2 and libmain both link it.
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }
}

