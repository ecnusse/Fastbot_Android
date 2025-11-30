package com.android.commands.monkey.utils;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class ScreenshotsTaskManager {
    private static final ScreenshotsTaskManager INSTANCE = new ScreenshotsTaskManager();
    private final ExecutorService executor = Executors.newFixedThreadPool(3);

    public static ScreenshotsTaskManager getInstance() {
        return INSTANCE;
    }

}
