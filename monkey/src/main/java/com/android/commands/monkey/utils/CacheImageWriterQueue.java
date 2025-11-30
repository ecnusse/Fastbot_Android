/*
 * Copyright 2020 Advanced Software Technologies Lab at ETH Zurich, Switzerland
 *
 * Modified - Copyright (c) 2020 Bytedance Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.commands.monkey.utils;

import android.graphics.Bitmap;

import java.io.File;


public class CacheImageWriterQueue extends ImageWriterQueue {

    private int cacheSize;
    private int postFailureNCounter = -1;
    private int postFailureN = -1;

    @Override
    public void run() {
    }

    @Override
    public synchronized void add(Bitmap map, File dst) {
        lastImage = dst.getName();
        requestQueue.add(new Req(map, dst));
        while (requestQueue.size() > this.cacheSize) {
            requestQueue.removeFirst();
        }
        checkPostN();
    }

    public void setCacheSize(int cacheSize){
        this.cacheSize = cacheSize;
    }

    public void setPostFailureN(int postFailureN){
        this.postFailureN = postFailureN;
    }

    public void checkPostN(){
        if (postFailureNCounter == -1 || postFailureN <= 0) return;
        postFailureNCounter--;
        if (postFailureNCounter == 0) flush();
    }

    public void resetPostFailureNCounter(){
        if (postFailureN <= 0) return;

        if (postFailureNCounter > 0) flush();
        postFailureNCounter = postFailureN;
    }

    @Override
    public synchronized void tearDown() {}

}



