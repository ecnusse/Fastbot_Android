package com.android.commands.monkey.utils;

import java.util.concurrent.Semaphore;

public class MonkeySemaphore {
    public static final Semaphore stepMonkey = new Semaphore(0);
    public static final Semaphore doneMonkey = new Semaphore(0);
}
