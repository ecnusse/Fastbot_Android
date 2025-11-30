package com.android.commands.monkey.utils;

import com.google.gson.annotations.SerializedName;


public class InitRequest {
    @SerializedName("takeScreenshots")
    private boolean takeScreenshots;

    @SerializedName("preFailureScreenshots")
    private int preFailureScreenshots;

    @SerializedName("postFailureScreenshots")
    private int postFailureScreenshots;

    @SerializedName("logStamp")
    private String logStamp;

    @SerializedName("deviceOutputRoot")
    private String deviceOutputRoot;

    public String getDeviceOutputRoot() {return deviceOutputRoot;}

    public int getPreFailureScreenshots() {return preFailureScreenshots;}

    public int getPostFailureScreenshots() {return postFailureScreenshots;}

    public String getLogStamp(){
        return logStamp;
    }

    public boolean isTakeScreenshots(){
        return takeScreenshots;
    }

}

