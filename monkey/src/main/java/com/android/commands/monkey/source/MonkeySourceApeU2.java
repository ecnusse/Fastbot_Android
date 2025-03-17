package com.android.commands.monkey.source;

import android.content.ComponentName;
import android.os.HandlerThread;

import com.android.commands.monkey.utils.Logger;
import com.bytedance.fastbot.OkHttpClient;

import java.io.File;
import java.util.List;
import java.util.Random;

public class MonkeySourceApeU2 extends MonkeySourceApeNative{

    protected final HandlerThread mHandlerThread = new HandlerThread("MonkeySourceApeU2");

    public MonkeySourceApeU2(Random random, List<ComponentName> MainApps, long throttle,
                             boolean randomizeThrottle, boolean permissionTargetSystem, File outputDirectory)
    {
        super(random, MainApps, throttle, randomizeThrottle, permissionTargetSystem, outputDirectory);
    }

    @Override
    public void connect()
    {
        for (int i = 0; i < 200; i++){
            boolean res = OkHttpClient.getInstance().connect();
            Logger.println(res ? "Success": "Failed");
            try {
                Thread.sleep(5000);
            } catch (InterruptedException e) {
                Logger.println(e);
            }
        }
    }
}
