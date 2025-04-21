package com.android.commands.monkey.utils;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.ImageWriter;

import com.google.gson.Gson;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

import fi.iki.elonen.NanoHTTPD;

import static com.android.commands.monkey.utils.Config.takeScreenshotForEveryStep;

import fi.iki.elonen.NanoHTTPD;

public class ProxyServer extends NanoHTTPD {

    private final OkHttpClient client;
    private final ScriptDriverClient scriptDriverClient;
    private final static Gson gson = new Gson();
    private boolean useCache = false;
    private String hierarchyResponseCache;

    private ImageWriterQueue mImageWriter;

    private ImageWriterQueue getImageWriter() {
        return mImageWriter;
    }

    public boolean shouldUseCache() {
        return this.useCache;
    }

    public String getHierarchyResponseCache() {
        return this.hierarchyResponseCache;
    }

    public ProxyServer(int port, ScriptDriverClient scriptDriverClient) {
        super(port);
        this.client = OkHttpClient.getInstance();
        this.scriptDriverClient = scriptDriverClient;

        // start the image writer thread
        mImageWriter = new ImageWriterQueue();
        Thread imageThread = new Thread(mImageWriter);
        imageThread.start();
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
        Logger.println("[Proxy Server] Forwarding");
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

        try {
            Logger.println("[ProxyServer] Dumping hierarchy");
            okhttp3.Response hierarchyResponse = scriptDriverClient.dumpHierarchy();

            if (takeScreenshotForEveryStep){
                Logger.println("[ProxyServer] Taking Screenshot");
                okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
                saveScreenshot(screenshotResponse);
            }

            this.useCache = true;
            return generateServerResponse(hierarchyResponse, true);
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    /**
     * Generate proxy response from the ui automation server, which finally respond to PC
     * @param okhttpResponse the okhttp3.response from ui automation server
     * @return The NanoHttpD response
     * @throws IOException .
     */
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
                    ""
            );
        }
    }

    /**
     * Save the screenshot to /sdcard/screenshots
     * @param screenshotResponse The okhttp3.Response from ui automation server
     * @return the screenshot is taken successfully
     */
    private boolean saveScreenshot(okhttp3.Response screenshotResponse) {
        Logger.println("[ProxyServer] Parsing bitmap with base64.");
        // 获取 Base64 编码的字符串
        String res;
        try {
            res = screenshotResponse.body().string();
        }
        catch (IOException e) {
            Logger.errorPrintln("[ProxyServer] [Error] ");
            return false;
        }

        JsonRPCResponse res_obj = gson.fromJson(res, JsonRPCResponse.class);
        String base64Data = res_obj.getResult();

        // Logger.println("[ProxyServer] Got base64Data: " + base64Data);
        // Decode the response with android.util.Base64
        byte[] decodedBytes = android.util.Base64.decode(base64Data, android.util.Base64.DEFAULT);

        Logger.println("[ProxyServer] base64 Decoded");
        // Parse the bytes into bitmap
        Bitmap bitmap = BitmapFactory.decodeByteArray(decodedBytes, 0, decodedBytes.length);

        if (bitmap == null){
            Logger.println("[ProxyServer][Error] Failed to parse screenshot response to bitmap");
            return false;
        } else {
            SimpleDateFormat sdf = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.ENGLISH);
            String currentDateTime = sdf.format(new Date());

            // create the screenshot file
            File screenshotFile = new File(
                "/sdcard/screenshots",
                String.format(Locale.ENGLISH, "screenshot-%s.png", currentDateTime)
            );
            Logger.println("[ProxyServer] Adding the screenshot to ImageWriter");
            mImageWriter.add(bitmap, screenshotFile);
            return true;
        }
    }

    /**
     * Generate proxy response from the ui automation server, which finally respond to PC.
     * Meanwhile, cache the hierarchy to accelerate the stepMonkey request
     * @param okhttpResponse the okhttp3.response from ui automation server
     * @param setHierarchyCache cache the hierarchy when doing stepMonkey
     * @return The NanoHttpD response
     * @throws IOException .
     */
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

            // save Screenshot while forwarding the request
            Logger.println("[Proxy Server] Detected script method, saving screenshot.");
            okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
            saveScreenshot(screenshotResponse);

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