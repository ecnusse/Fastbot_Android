package com.android.commands.monkey.utils;

import com.google.gson.Gson;
import java.io.IOException;
import java.util.Arrays;



public class U2Client implements ScriptDriverClient {

    private static final U2Client INSTANCE = new U2Client();
    private final static Gson gson = new Gson();
    private OkHttpClient client;

    public static U2Client getInstance() {
        return INSTANCE;
    }

    public U2Client() {
          client = OkHttpClient.getInstance();
    }

    public okhttp3.Response dumpHierarchy() {
        String url = client.get_url_builder().addPathSegments("jsonrpc/0").build().toString();

        JsonRPCRequest requestObj = new JsonRPCRequest(
                "dumpWindowHierarchy",
                Arrays.asList(false, 50)
        );

        try {
            return client.post(url, gson.toJson(requestObj));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public okhttp3.Response takeScreenshot() {
        String url = client.get_url_builder().addPathSegments("jsonrpc/0").build().toString();

        JsonRPCRequest requestObj = new JsonRPCRequest(
                "takeScreenshot",
                Arrays.asList(1, 80)
        );

        try {
            return client.post(url, gson.toJson(requestObj));
        } catch (
                IOException e) {
            throw new RuntimeException(e);
        }
    }
}
