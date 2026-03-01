package com.android.commands.monkey.utils;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import com.android.commands.monkey.action.Action;
import com.android.commands.monkey.fastbot.client.Operate;
import com.android.commands.monkey.source.CoverageData;
import com.android.commands.monkey.source.MonkeySourceApeU2;
import com.google.gson.Gson;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import fi.iki.elonen.NanoHTTPD;

// import static com.android.commands.monkey.utils.Config.takeScreenshotForEveryStep;

import org.json.JSONException;
import org.json.JSONObject;

public class ProxyServer extends NanoHTTPD {

    private final OkHttpClient client;
    private final ScriptDriverClient scriptDriverClient;
    private final static Gson gson = new Gson();
    private final MonkeySourceApeU2 eventSource;
    private String hierarchyResponseCache;
    public boolean useCache = false;
    public boolean takeScreenshots = false;
    public String logStamp;
    public int stepsCount = 0;

    private File outputDir;
    private ImageWriterQueue mImageWriter;

    public boolean monkeyIsOver;
    public List<String> blockWidgets;

    public List<String> blockTrees;
    public int preFailureScreenshots = 0;
    public int postFailureScreenshots = 0;

//    private CoverageData mCoverageData;
    private int mVerbose = 1;

    private HashSet<String> u2ExtMethods = new HashSet<>(
            Arrays.asList("click", "setText", "swipe", "drag", "setOrientation", "pressKey")
    );
    // private Operate mOperate;


    public String getHierarchyResponseCache() {
        return this.hierarchyResponseCache;
    }

    public ProxyServer(int port, ScriptDriverClient scriptDriverClient, MonkeySourceApeU2 eventSource) {
        super(port);
        this.client = OkHttpClient.getInstance();
        this.scriptDriverClient = scriptDriverClient;
        this.eventSource = eventSource;
    }

    private void startImageWriter(){
        if (preFailureScreenshots == 0){
            Logger.println("take all screenshots.");
            mImageWriter = new ImageWriterQueue();
        } else {
            Logger.println(String.format("take %d screenshots before failure", preFailureScreenshots));
            Logger.println(String.format("take %d screenshots after failure", postFailureScreenshots));
            mImageWriter = new CacheImageWriterQueue();
            ((CacheImageWriterQueue) mImageWriter).setCacheSize(preFailureScreenshots);
            ((CacheImageWriterQueue) mImageWriter).setPostFailureN(postFailureScreenshots);
        }
        Thread imageThread = new Thread(mImageWriter);
        imageThread.start();
    }

    public void processFailureNScreenshots() {
        mImageWriter.flush();
        if (mImageWriter instanceof CacheImageWriterQueue){
            ((CacheImageWriterQueue) mImageWriter).resetPostFailureNCounter();
        }
    }

    public String peekImageQueue() {
        return mImageWriter.lastImage;
    }

    @Override
    public Response serve(IHTTPSession session) {
        String method = session.getMethod().name();
        String uri = session.getUri();

        if (session.getMethod() == Method.GET && uri.equals("/stopMonkey"))
        {
            monkeyIsOver = true;
            MonkeySemaphore.stepMonkey.release();
            return newFixedLengthResponse(
                Response.Status.OK,
                "text/plain",
                "Monkey Stopped"
            );
        }

        if (session.getMethod() == Method.GET && uri.equals("/propertyFirstSatisfied")) {
            return handlePropertyFirstSatisfied();
        }

        // parse the request body data
        Map<String, String> data = new HashMap<>();
        String requestBody = "";


        // parse the request body
        if (session.getMethod() == Method.POST ||
                session.getMethod() == Method.PUT ||
                session.getMethod() == Method.DELETE
        ) {
            try {
                session.parseBody(data);
                requestBody = data.get("postData");
                if (requestBody == null) {
                    requestBody = "";
                }
                if (mVerbose > 3){
                    Logger.println(requestBody);
                }
            } catch (IOException | ResponseException e) {
                return newFixedLengthResponse(
                        Response.Status.INTERNAL_ERROR,
                        "text/plain",
                        "Error when parsing post data: " + e.getMessage());
            }
        }

        if (uri.equals("/init") && session.getMethod() == Method.POST){
            InitRequest req = new Gson().fromJson(requestBody, InitRequest.class);
            takeScreenshots = req.isTakeScreenshots();
            logStamp = req.getLogStamp();
            preFailureScreenshots = req.getPreFailureScreenshots();
            postFailureScreenshots = req.getPostFailureScreenshots();
            String deviceOutputRoot = req.getDeviceOutputRoot();
            outputDir = new File(new File(deviceOutputRoot), "output_" + logStamp);
            Logger.println("Init: ");
            Logger.println("    takeScreenshots: " + takeScreenshots);
            Logger.println("    preFailureScreenshots: " + preFailureScreenshots);
            Logger.println("    postFailureScreenshots: " + postFailureScreenshots);
            Logger.println("    logStamp: " + logStamp);
            Logger.println("    outputDir: " + outputDir);
            if (!StoneUtils.ensureDir(outputDir)){
                Logger.errorPrintln("Fail to create output dir: " + outputDir);
                Logger.errorPrintln("Please specify a valid outputDir");
            }
            startImageWriter();
            return newFixedLengthResponse(
                    Response.Status.OK,
                    "text/plain",
                    "outputDir:" + outputDir
            );
        }

        if (uri.equals("/stepMonkey") && session.getMethod() == Method.POST)
        {
            StepMonkeyRequest req = new Gson().fromJson(requestBody, StepMonkeyRequest.class);
            stepsCount = req.getStepsCount();
            Logger.println("[ProxyServer] stepsCount: " + stepsCount);
            return stepMonkey(req.getBlockWidgets(), req.getBlockTrees());
        }

        if (uri.equals("/sendInfo") && session.getMethod() == Method.POST)
        {
            if (requestBody.contains("kill_apps")){
                JSONObject obj = new JSONObject();
                try {
                    obj.put("Type", "Monkey");
                    obj.put("MonkeyStepsCount", stepsCount+1);
                    obj.put("Time", Logger.getCurrentTimeStamp());
                    obj.put("Info", "kill_apps");
                    obj.put("Screenshot", "");
                    saveLog(obj, "steps.log");
                } catch (JSONException e){
                    Logger.errorPrintln("Error when recording log.");
                }
            }
            return newFixedLengthResponse(
                Response.Status.OK,
                "text/plain",
                "OK"
            );
        }

        if (uri.equals("/logScript") && session.getMethod() == Method.POST)
        {
            try {
                JSONObject jsonRPCBody = new JSONObject(requestBody);
                String screenshot_file = "";
                if (takeScreenshots){
                    if (jsonRPCBody.getString("kind").equals("invariant")){
                        screenshot_file = peekImageQueue();
                    } else {
                        okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
                        screenshot_file = saveScreenshot(screenshotResponse);
                    }
                }
                recordLog(jsonRPCBody, screenshot_file);
                String state = jsonRPCBody.getString("state");
                if (state.equals("fail") || state.equals("error")){
                    processFailureNScreenshots();
                }
            }catch (JSONException e){
                Logger.println("Error when parsing jsonrpc request body: " + requestBody);
            }
            return newFixedLengthResponse(
                    Response.Status.OK,
                    "text/plain",
                    "OK"
            );
        }

        if (uri.equals("/dumpHierarchy")) {
            return getHierarchyResponse();
        }

        if (uri.equals("/jsonrpc/0"))
        {
            this.eventSource.updateActivityHistory();
            JSONObject jsonRPCBody;
            String RPCmethod = "";
            try {
                jsonRPCBody = new JSONObject(requestBody);
                RPCmethod = jsonRPCBody.getString("method");
            }catch (JSONException e){
                Logger.println("Error when parsing jsonrpc request body: " + requestBody);
            }
            String screenshot_file = "";
            if (u2ExtMethods.contains(RPCmethod)){
                // An ExtMehod will lead to state transition, making the useCache false.
                this.useCache = false;
                Logger.println("[Proxy Server] Detected script method: " + RPCmethod);
                if (takeScreenshots) {
                    okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
                    screenshot_file = saveScreenshot(screenshotResponse);
                }
                // save Screenshot while forwarding the request
                recordLog(requestBody, screenshot_file);
            }
        }
        Logger.println("[Proxy Server] Forwarding");
        return forward(uri, method, requestBody);
    }




    public File getDeviceOutputDir(){
        return outputDir;
    }

    public void saveCoverageStatistics(CoverageData coverageData){
        if (mVerbose > 3){
            Logger.println("Saving coverageData: server.stepsCount: " + stepsCount);
        }
        saveLog(coverageData.toJSON(), "coverage.log");
    }

    private Response stepMonkey(List<String> blockWidgets, List<String> blockTrees){
        this.blockWidgets = blockWidgets;
        this.blockTrees = blockTrees;
        Logger.println("[ProxyServer] receive request: stepMonkey");
        if (!blockWidgets.isEmpty()){
            Logger.println("              blockWidgets: " + blockWidgets);
        }
        if (!blockTrees.isEmpty()){
            Logger.println("              blockTrees: " + blockTrees);
        }
        MonkeySemaphore.stepMonkey.release();
        if (mVerbose > 3) {
            Logger.println("[ProxyServer] release semaphore: stepMonkey");
        }
        try {
            MonkeySemaphore.doneMonkey.acquire();
            if (mVerbose > 3){
                Logger.println("[ProxyServer] acquired semaphore: doneMonkey");
            }
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }

        Logger.println("[ProxyServer] Finish monkey step. Dumping hierarchy");
        return getHierarchyResponse();
    }

    private Response getHierarchyResponse() {
        okhttp3.Response hierarchyResponse = scriptDriverClient.dumpHierarchy();
        if (hierarchyResponse != null && hierarchyResponse.body() != null) {
            String body = null;
            try {
                body = hierarchyResponse.body().string();
            } catch (IOException e) {
                return getEmptyResponse();
            }
            this.hierarchyResponseCache = body;
            if (!this.hierarchyResponseCache.isEmpty()){
                this.useCache = true;
            }
            // check the response code
            Response.Status status = Response.Status.lookup(hierarchyResponse.code());
            if (status == null) {
                status = Response.Status.OK;
            }
            String contentType = hierarchyResponse.header("Content-Type", "application/json");
            return newFixedLengthResponse(status, contentType, body);
        } else {
            return getEmptyResponse();
        }
    }

    private Response getEmptyResponse(){
        String emptyResultJson = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"\"}";
        return newFixedLengthResponse(
                Response.Status.OK,
                "application/json",
                emptyResultJson
        );
    }

    private boolean recordLog(Operate op, String screenshot_file, String activityName){
        JSONObject obj = new JSONObject();
        try {
            obj.put("Type", "Monkey");
            obj.put("MonkeyStepsCount", stepsCount);
            obj.put("Time", Logger.getCurrentTimeStamp());
            activityName = activityName != null ? activityName : "";
            obj.put("Activity", activityName);
            obj.put("Info", op.toJson());
            obj.put("Screenshot", screenshot_file);
            saveLog(obj, "steps.log");
        } catch (JSONException e){
            Logger.errorPrintln("Error when recording log.");
            return false;
        }
        return true;
    }

    private boolean recordLog(Action action, String screenshot_file){
        JSONObject obj = new JSONObject();
        try {
            obj.put("Type", "Fuzz");
            obj.put("MonkeyStepsCount", stepsCount);
            obj.put("Time", Logger.getCurrentTimeStamp());
            obj.put("Info", "FUZZ");
            obj.put("Screenshot", screenshot_file);
            saveLog(obj, "steps.log");
        } catch (JSONException e){
            Logger.errorPrintln("Error when recording log.");
            return false;
        }
        return true;
    }

    private boolean recordLog(String U2ReqBody, String screenshot_file){
        JSONObject obj = new JSONObject();
        try {
            obj.put("Type", "Script");
            obj.put("MonkeyStepsCount", stepsCount);
            obj.put("Time", Logger.getCurrentTimeStamp());
            obj.put("Info", U2ReqBody);
            obj.put("Screenshot", screenshot_file);
            saveLog(obj, "steps.log");
        } catch (JSONException e){
            Logger.errorPrintln("Error when recording log.");
            return false;
        }
        return true;
    }

    private boolean recordLog(JSONObject logObject, String screenshot_file){
        JSONObject obj = new JSONObject();
        try {
            obj.put("Type", "ScriptInfo");
            obj.put("MonkeyStepsCount", stepsCount);
            obj.put("Time", Logger.getCurrentTimeStamp());
            obj.put("Info", logObject.toString());
            obj.put("Screenshot", screenshot_file);
            saveLog(obj, "steps.log");
        } catch (JSONException e){
            Logger.println("Error when recording log.");
            return false;
        }
        return true;
    }

    private void saveLog(JSONObject obj, String file_name){
        if (!StoneUtils.ensureDir(outputDir))
        {
            Logger.errorPrintln("outputDir: '" + outputDir + "' not exists.");
        }
        File logFile = new File(outputDir, file_name);
        try {
            StoneUtils.writeStringToFile(logFile, String.format("%s\n", obj.toString()), true);
        } catch (IOException e){
            Logger.errorPrintln("Error when saving log: " + logFile);
            e.printStackTrace();
        }
    }

    /**
     * Generate proxy response from the ui automation server, which finally respond to PC
     * @param okhttpResponse the okhttp3.response from ui automation server
     * @return The NanoHttpD response
     * @throws IOException .
     */
    private Response generateServerResponse(okhttp3.Response okhttpResponse) throws IOException{
        if (okhttpResponse != null && okhttpResponse.body() != null) {
            String body = okhttpResponse.body().string();

            Response.Status status = Response.Status.lookup(okhttpResponse.code());
            if (status == null) {
                status = Response.Status.OK;
            }

            String contentType = okhttpResponse.header("Content-Type", "application/json");
            return newFixedLengthResponse(status, contentType, body);
        } else {
            return getEmptyResponse();
        }
    }

    /**
     * Save the screenshot to /sdcard/screenshots
     * @param screenshotResponse The okhttp3.Response from ui automation server
     * @return the screenshot file_name
     */
    private String saveScreenshot(okhttp3.Response screenshotResponse) {
        if (mVerbose > 3){
            Logger.println("[ProxyServer] Parsing bitmap with base64.");
        }
        String res;
        try {
            res = screenshotResponse.body().string();
        }
        catch (IOException e) {
            Logger.errorPrintln("[ProxyServer] [Error]");
            return "";
        }

        JsonRPCResponse res_obj = gson.fromJson(res, JsonRPCResponse.class);
        String base64Data = res_obj.getResult();

        if (base64Data == null){
            Logger.println("[ProxyServer][Error] failed to take the screenshot.");
            return "";
        }
        // Decode the response with android.util.Base64
        byte[] decodedBytes = android.util.Base64.decode(base64Data, android.util.Base64.DEFAULT);

        Logger.println("[ProxyServer] base64 Decoded");
        // Parse the bytes into bitmap
        Bitmap bitmap = BitmapFactory.decodeByteArray(decodedBytes, 0, decodedBytes.length);
        String screenshot_file = getScreenshotName();

        if (bitmap == null){
            Logger.println("[ProxyServer][Error] Failed to parse screenshot response to bitmap");
            return "";
        }
        // Ensure screenshots dir
        File screenshotDir = new File(outputDir, "screenshots");
        Logger.println("Screenshots will be saved to: " + screenshotDir);
        if (!screenshotDir.exists()) {
            boolean created = screenshotDir.mkdirs();
            if (!created) {
                Logger.println("[ProxyServer][Error] Failed to create screenshots directory");
                return "";
            }
        }

        // create the screenshot file
        File screenshotFile = new File(
            screenshotDir,
            screenshot_file
        );
        Logger.println("[ProxyServer] Adding the screenshot to ImageWriter");
        mImageWriter.add(bitmap, screenshotFile);
        return screenshot_file;

    }

    private String getScreenshotName(){
        SimpleDateFormat sdf = new SimpleDateFormat("HHmmssSSS", Locale.ENGLISH);
        String currentDateTime = sdf.format(new Date());
        return String.format(Locale.ENGLISH, "screenshot-%d-%s.png", stepsCount, currentDateTime);
    }


    private Response forward(String uri, String method, String requestBody){
        String targetUrl = client.get_url_builder()
                .addPathSegments(uri.startsWith("/") ? uri.substring(1) : uri)
                .build()
                .toString();

        try {
            okhttp3.Response forwardedResponse;

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
                        "Unsupport method: " + method);
            }

            return generateServerResponse(forwardedResponse);
        } catch (IOException ex) {
            return newFixedLengthResponse(
                    Response.Status.INTERNAL_ERROR,
                    "text/plain",
                    "Error When forwarding: " + ex.getMessage());
        }
    }

    public void tearDown(){
        mImageWriter.tearDown();
    }

    public void setVerbose(int verbose) {
        this.mVerbose = verbose;
    }

    /**
     * record the monkey action (generated by fastbot AiClient)
     * @param operate
     */
    public void recordMonkeyStep(Operate operate, String activityName) {
        String screenshot_file = "";
        if (takeScreenshots){
            Logger.println("[ProxyServer] Taking Screenshot");
            okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
            screenshot_file = saveScreenshot(screenshotResponse);
        }
        recordLog(operate, screenshot_file, activityName);
    }

    /**
     * record the fuzz action
     * (Different from monkey operation, will be triggered when error in dumping hierarchy)
     * @param action
     */
    public void recordMonkeyStep(Action action){
        String screenshot_file = "";
        if (takeScreenshots){
            Logger.println("[ProxyServer] Taking Screenshot");
            okhttp3.Response screenshotResponse = scriptDriverClient.takeScreenshot();
            screenshot_file = saveScreenshot(screenshotResponse);
        }
        recordLog(action, screenshot_file);
    }

    private Response handlePropertyFirstSatisfied() {
        // Use a synchronous call with timeout: call sendCurrentPageAsPrecondition and wait for up to TIMEOUT_MS.
        final int TIMEOUT_MS = 8000; // 8 seconds timeout, tunable
        if (this.eventSource == null) {
            Logger.errorPrintln("[ProxyServer] eventSource is null, cannot send precondition");
            return newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "{\"status\":\"error\",\"message\":\"eventSource not available\"}");
        }

        java.util.concurrent.ExecutorService ex = java.util.concurrent.Executors.newSingleThreadExecutor();
        java.util.concurrent.Future<Boolean> f = ex.submit(new java.util.concurrent.Callable<Boolean>() {
            @Override
            public Boolean call() throws Exception {
                try {
                    return ProxyServer.this.eventSource.sendCurrentPageAsPrecondition();
                } catch (Throwable t) {
                    Logger.errorPrintln("[ProxyServer] sendCurrentPageAsPrecondition threw: " + t.getMessage());
                    return false;
                }
            }
        });

        boolean success = false;
        try {
            success = f.get(TIMEOUT_MS, java.util.concurrent.TimeUnit.MILLISECONDS);
        } catch (java.util.concurrent.TimeoutException te) {
            f.cancel(true);
            Logger.errorPrintln("[ProxyServer] sendCurrentPageAsPrecondition timed out after " + TIMEOUT_MS + "ms");
            ex.shutdownNow();
            return newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "{\"status\":\"error\",\"message\":\"timeout\"}");
        } catch (Exception e) {
            Logger.errorPrintln("[ProxyServer] Exception when waiting for sendCurrentPageAsPrecondition: " + e.getMessage());
            ex.shutdownNow();
            return newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "{\"status\":\"error\",\"message\":\"exception\"}");
        } finally {
            ex.shutdownNow();
        }

        if (success) {
            Logger.println("[ProxyServer] sendCurrentPageAsPrecondition succeeded");
            return newFixedLengthResponse(Response.Status.OK, "text/plain", "{\"status\":\"ok\"}");
        } else {
            Logger.errorPrintln("[ProxyServer] sendCurrentPageAsPrecondition reported failure");
            return newFixedLengthResponse(Response.Status.INTERNAL_ERROR, "text/plain", "{\"status\":\"error\",\"message\":\"native failed\"}");
        }
    }
}
