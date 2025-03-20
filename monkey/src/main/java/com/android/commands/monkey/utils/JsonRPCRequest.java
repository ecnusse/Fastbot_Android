package com.android.commands.monkey.utils;

import java.util.List;

public class JsonRPCRequest {
    private String jsonrpc;
    private int id;
    private String method;
    private List<Object> params;

    public JsonRPCRequest(String jsonrpc, int id, String method, List<Object> params) {
        this.jsonrpc = jsonrpc;
        this.id = id;
        this.method = method;
        this.params = params;
    }

    public JsonRPCRequest(String method, List<Object> params)
    {
        this.jsonrpc = "2.0";
        this.id = 1;
        this.method = method;
        this.params = params;
    }

    // 如果需要，可以提供 getter 和 setter 方法
    public String getJsonrpc() {
        return jsonrpc;
    }

    public void setJsonrpc(String jsonrpc) {
        this.jsonrpc = jsonrpc;
    }

    public int getId() {
        return id;
    }

    public void setId(int id) {
        this.id = id;
    }

    public String getMethod() {
        return method;
    }

    public void setMethod(String method) {
        this.method = method;
    }

    public List<Object> getParams() {
        return params;
    }

    public void setParams(List<Object> params) {
        this.params = params;
    }
}

