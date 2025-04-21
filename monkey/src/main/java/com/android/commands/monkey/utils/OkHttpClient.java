package com.android.commands.monkey.utils;

import java.io.IOException;

import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.logging.HttpLoggingInterceptor;

public class OkHttpClient {

    private static final OkHttpClient INSTANCE = new OkHttpClient();
    private static final MediaType JSON = MediaType.parse("application/json; charset=utf-8");
    // 连接状态
    private boolean loaded = false;

    private final okhttp3.OkHttpClient client;

    // 日志拦截器，初始化时设定日志级别为 BODY
    private HttpLoggingInterceptor loggingInterceptor = new HttpLoggingInterceptor(
            new HttpLoggingInterceptor.Logger() {
                @Override
                public void log(String message) {
                    int maxLength = 300;
                    if (message != null && message.length() > maxLength) {
                        // 截取前 maxLength 个字符，加上截断提示
                        Logger.println(message.substring(0, maxLength) + "...[truncated]");
                    } else {
                        Logger.println(message);
                    }
                }
            }
    );

    private OkHttpClient() {
        // 设置日志级别，打印所有请求和响应的详细信息
        loggingInterceptor.setLevel(HttpLoggingInterceptor.Level.BODY);

        client = new okhttp3.OkHttpClient.Builder()
                .addInterceptor(loggingInterceptor)
                .build();
        connect();
    }

    public static OkHttpClient getInstance() {
        return INSTANCE;
    }

    public HttpUrl.Builder get_url_builder() {
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
//            Logger.println("Connection Request: " + request);
            Response response = client.newCall(request).execute();
            if (response.body() != null) {
                String result = response.body().string();
                // 如果返回内容为 "pong"，则设置 loaded 为 true
//                Logger.println("Connection Response: " + result);
                // TODO
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

    public Response newCall(Request request) throws IOException {
        Response response = client.newCall(request).execute();

        if (!response.isSuccessful()) {
            Logger.errorPrintln("Failed with code:" + response.code());
        }
        return response;
    }

    public Response get(final String url) throws IOException {
        Request request = new Request.Builder()
                .url(url)
                .build();
        return newCall(request);
    }

    public Response post(final String url, String json) throws IOException {
        RequestBody body = RequestBody.create(JSON, json);
        Request request = new Request.Builder()
                .url(url)
                .post(body)
                .build();
        return newCall(request);
    }

    public Response delete(final String url, String json) throws IOException {
        RequestBody body = RequestBody.create(JSON, json);
        Request request = new Request.Builder()
                .url(url)
                .delete(body)
                .build();
        return newCall(request);
    }
}
