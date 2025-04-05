package com.android.commands.monkey.utils;

public interface ScriptDriverClient {
    static ScriptDriverClient getInstance() {
        return null;
    }
    okhttp3.Response dumpHierarchy();
    okhttp3.Response takeScreenshot();
}
