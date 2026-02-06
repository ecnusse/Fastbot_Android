package com.android.commands.monkey.utils;

import com.google.gson.Gson;
import java.io.IOException;
import java.util.Arrays;

import fi.iki.elonen.NanoHTTPD;
import okhttp3.MediaType;
import okhttp3.Protocol;
import okhttp3.ResponseBody;


public class U2Client extends ScriptDriverClient {

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

        int maxRetries = 1;
        IOException lastEx = null;
        for (int i = 0; i < maxRetries; i++) {
            try {
                return client.post(url, gson.toJson(requestObj));
            } catch (IOException e) {
                lastEx = e;
                if (i < maxRetries - 1) {
                    StoneUtils.sleep(1000);
                }
            }
        }
        Logger.println("Error when dumping hierarchy: " + lastEx.getMessage());
        Logger.println("Return null");
        return null;
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
