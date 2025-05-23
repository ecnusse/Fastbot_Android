/*
 * Copyright (c) 2020 Bytedance Inc.
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

package com.android.commands.monkey.events.base;

import android.app.IActivityManager;
import android.os.RemoteException;
import android.view.IWindowManager;
import android.os.Build;
import android.view.Display;
import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.utils.Logger;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

/**
 * monkey screen rotation event
 */
public class MonkeyRotationEvent extends MonkeyEvent {

    public final int mRotationDegree;
    public final boolean mPersist;

    /**
     * Construct a rotation Event.
     *
     * @param degree  Possible rotation degrees, see constants in
     *                anroid.view.Suface.
     * @param persist Should we keep the rotation lock after the orientation change.
     */
    public MonkeyRotationEvent(int degree, boolean persist) {
        super(EVENT_TYPE_ROTATION);
        mRotationDegree = degree;
        mPersist = persist;
    }

    @Override
    public int injectEvent(IWindowManager iwm, IActivityManager iam, int verbose) {
        if (verbose > 0) {
            Logger.println(":Sending rotation degree=" + mRotationDegree + ", persist=" + mPersist);
        }

        // inject rotation event
        try {

            if (Build.VERSION.SDK_INT >= 35){
//                return MonkeyEvent.INJECT_SUCCESS;
                try{
                    Class<?> iwmClass = iwm.getClass();
                    Method freezeMethod = iwmClass.getMethod("freezeDisplayRotation", int.class, int.class, String.class);
                    freezeMethod.invoke(iwm, Display.DEFAULT_DISPLAY, mRotationDegree, "com.bytedance.fastbot");
                } catch (InvocationTargetException | IllegalAccessException |
                         NoSuchMethodException e) {
                    throw new RuntimeException(e);
                }
            }
            else {
                iwm.freezeRotation(mRotationDegree);
            }
            if (!mPersist) {
                if (Build.VERSION.SDK_INT >= 35){
                    try{
                        Class<?> iwmClass = iwm.getClass();
                        Method thawMethod = iwmClass.getMethod("thawDisplayRotation", int.class, String.class);
                        thawMethod.invoke(iwm, Display.DEFAULT_DISPLAY, "com.bytedance.fastbot");
                    } catch (InvocationTargetException | IllegalAccessException |
                             NoSuchMethodException e) {
                        throw new RuntimeException(e);
                    }
                } else {
                    iwm.thawRotation();
                }
            }
            return MonkeyEvent.INJECT_SUCCESS;
        } catch (RemoteException ex) {
            return MonkeyEvent.INJECT_ERROR_REMOTE_EXCEPTION;
        }
    }
}
