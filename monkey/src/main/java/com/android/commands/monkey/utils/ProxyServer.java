package com.android.commands.monkey.utils;

import com.google.gson.Gson;

import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import fi.iki.elonen.NanoHTTPD;

public class ProxyServer extends NanoHTTPD {

    private final OkHttpClient client;
    private final static Gson gson = new Gson();
    private boolean useCache = false;
    private String hierarchyResponseCache;

    public boolean shouldUseCache() {
        return this.useCache;
    }

    public String getHierarchyResponseCache() {
        return this.hierarchyResponseCache;
    }

    public ProxyServer(int port) {
        super(port);
        this.client = OkHttpClient.getInstance();
    }

    @Override
    public Response serve(IHTTPSession session) {
        String method = session.getMethod().name();
        String uri = session.getUri();

        // 用于存储解析请求体数据
        Map<String, String> files = new HashMap<>();
        String requestBody = "";

        // 如果请求有请求体（POST、PUT、DELETE等方法），解析 body 数据
        if (session.getMethod() == Method.POST ||
                session.getMethod() == Method.PUT ||
                session.getMethod() == Method.DELETE
        ) {
            try {
                session.parseBody(files);
                requestBody = files.get("postData");
                if (requestBody == null) {
                    requestBody = "";
                }
            } catch (IOException | ResponseException e) {
                return newFixedLengthResponse(
                        Response.Status.INTERNAL_ERROR,
                        "text/plain",
                        "请求体解析错误: " + e.getMessage());
            }
        }

        if (uri.equals("/stepMonkey") && session.getMethod() == Method.GET)
        {
            return stepMonkey();
        }
        return forward(uri, method, requestBody);
    }

    private Response stepMonkey(){
        Logger.println("[ProxyServer] 收到请求: stepMonkey");
        MonkeySemaphore.stepMonkey.release();
        Logger.println("[ProxyServer] 释放信号量: stepMonkey");
        try {
            MonkeySemaphore.doneMonkey.acquire();
            Logger.println("[ProxyServer] 收到信号量: doneMonkey");
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }

        String url = client.get_url_builder().addPathSegments("jsonrpc/0").build().toString();

        JsonRPCRequest requestObj = new JsonRPCRequest(
                "dumpWindowHierarchy",
                Arrays.asList(false, 50)
        );

        try {
            okhttp3.Response hierarchyResponse = client.post(url, gson.toJson(requestObj));
            this.useCache = true;
            return generateServerResponse(hierarchyResponse, true);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    private Response generateServerResponse(okhttp3.Response okhttpResponse) throws IOException{
        // 读取转发请求返回的响应数据
        if (okhttpResponse != null && okhttpResponse.body() != null) {
            String body = okhttpResponse.body().string();
            // 查找对应的状态码，不存在时默认为 OK
            Response.Status status = Response.Status.lookup(okhttpResponse.code());
            if (status == null) {
                status = Response.Status.OK;
            }
            // 尝试从响应中获取 Content-Type，如果为空则默认 "application/json"
            String contentType = okhttpResponse.header("Content-Type", "application/json");
            return newFixedLengthResponse(status, contentType, body);
        } else {
            return newFixedLengthResponse(
                    Response.Status.NO_CONTENT,
                    "text/plain",
                    "");
        }
    }

    private Response generateServerResponse(okhttp3.Response okhttpResponse, boolean setHierarchyCache) throws IOException{
        // 读取转发请求返回的响应数据
        if (okhttpResponse != null && okhttpResponse.body() != null) {
            String body = okhttpResponse.body().string();
            if (setHierarchyCache) this.hierarchyResponseCache = body;
            // 查找对应的状态码，不存在时默认为 OK
            Response.Status status = Response.Status.lookup(okhttpResponse.code());
            if (status == null) {
                status = Response.Status.OK;
            }
            // 尝试从响应中获取 Content-Type，如果为空则默认 "application/json"
            String contentType = okhttpResponse.header("Content-Type", "application/json");
            return newFixedLengthResponse(status, contentType, body);
        } else {
            return newFixedLengthResponse(
                    Response.Status.NO_CONTENT,
                    "text/plain",
                    "");
        }
    }



    private Response forward(String uri, String method, String requestBody){
        // 构造转发URL，这里假设使用 OkHttpClient 的 get_url_builder() 返回的构建器，
        // 将当前请求的 URI 拼接到目标服务地址上。
        // 注意：根据实际需求对 uri 的处理可能需要更多验证与调整。
        String targetUrl = client.get_url_builder()
                .addPathSegments(uri.startsWith("/") ? uri.substring(1) : uri)
                .build()
                .toString();

        try {
            okhttp3.Response forwardedResponse;

            // 根据请求方法调用相应的转发函数
            if ("GET".equalsIgnoreCase(method)) {
                forwardedResponse = client.get(targetUrl);
            } else if ("POST".equalsIgnoreCase(method)) {
                forwardedResponse = client.post(targetUrl, requestBody);
            } else if ("DELETE".equalsIgnoreCase(method)) {
                forwardedResponse = client.delete(targetUrl, requestBody);
            } else {
                return newFixedLengthResponse(
                        Response.Status.BAD_REQUEST,
                        "text/plain",
                        "不支持的 HTTP 方法: " + method);
            }

            return generateServerResponse(forwardedResponse);
        } catch (IOException ex) {
            return newFixedLengthResponse(
                    Response.Status.INTERNAL_ERROR,
                    "text/plain",
                    "请求转发错误: " + ex.getMessage());
        }
        finally {
            this.useCache = false;
        }
    }
}