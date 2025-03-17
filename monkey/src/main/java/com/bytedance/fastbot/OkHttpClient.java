package com.bytedance.fastbot;

import com.android.commands.monkey.utils.Logger;

import java.io.IOException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class OkHttpClient {

    private static final OkHttpClient INSTANCE = new OkHttpClient();
    private static final MediaType JSON = MediaType.parse("application/json; charset=utf-8");
    // 连接状态
    private boolean loaded = false;

    private final okhttp3.OkHttpClient client;

    private OkHttpClient() {
        client = new okhttp3.OkHttpClient();
        connect();
    }

    public static OkHttpClient getInstance() {
        return INSTANCE;
    }

    private HttpUrl.Builder get_url_builder() {
        return new HttpUrl.Builder()
                          .scheme("http")
                          .host("127.0.0.1")
                          .port(9008);
    }

    // 连接方法，通过 OkHttpClient 发送请求
    public boolean connect() {
        String url = get_url_builder().addPathSegment("ping")
                                      .build()
                                      .toString();

        Request request = new Request.Builder()
                                     .url(url)
                                     .build();

        try {
            Logger.println("Request: " + request);
            Response response = client.newCall(request).execute();
            if (response.body() != null) {
                String result = response.body().string();
                // 如果返回内容为 "pong"，则设置 loaded 为 true
                Logger.println("Response: " + result);
                loaded = "pong".equals(result);
            } else {
                loaded = false;
            }
        } catch (IOException e) {
            Logger.println(e);
            loaded = false;
        }
        return loaded;
    }

    public boolean isLoaded() {
        return loaded;
    }

    public void newCall(Request request, Callback callback){
        client.newCall(request).enqueue(callback);
    }

    public void get(final String url, Callback callback) {
        final Request request = new Request.Builder()
                .url(url)
                .build();
    }

    public void post(final String url, String json, Callback callback) {
        RequestBody body = RequestBody.create(JSON, json);
        final Request request = new Request.Builder()
                .url(url)
                .post(body)
                .build();
        Call call = client.newCall(request);
        call.enqueue(callback);
    }

    public void delete(final String url, String json, Callback callback) {
        RequestBody body = RequestBody.create(JSON, json);
        Request request = new Request.Builder()
                .url(url)
                .delete(body)
                .build();
        Call call = client.newCall(request);
        call.enqueue(callback);
    }
}