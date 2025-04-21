package com.android.commands.monkey.utils;

public interface ScriptDriverClient {
    static ScriptDriverClient getInstance() {
        return null;
    }

    // TODO rewrite the connect method to enable reconnecting the server.
//    boolean connect();
    okhttp3.Response dumpHierarchy();
    okhttp3.Response takeScreenshot();
}
