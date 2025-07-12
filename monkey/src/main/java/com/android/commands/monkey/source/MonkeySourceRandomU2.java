/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.commands.monkey.source;

import static com.android.commands.monkey.fastbot.client.ActionType.SCROLL_BOTTOM_UP;
import static com.android.commands.monkey.utils.Config.bytestStatusBarHeight;
import static com.android.commands.monkey.utils.Config.debug;
import static com.android.commands.monkey.utils.Config.refectchInfoCount;
import static com.android.commands.monkey.utils.Config.refectchInfoWaitingInterval;
import static com.android.commands.monkey.utils.Config.startAfterNSecondsofsleep;
import static com.android.commands.monkey.utils.Config.swipeDuration;
import static com.android.commands.monkey.utils.Config.useRandomClick;

import android.annotation.TargetApi;
import android.content.ComponentName;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.PointF;
import android.graphics.Rect;
import android.hardware.display.DisplayManagerGlobal;
import android.os.Build;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;

import com.android.commands.monkey.action.Action;
import com.android.commands.monkey.action.LLMAction;
import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.events.MonkeyEventQueue;
import com.android.commands.monkey.events.MonkeyEventSource;
import com.android.commands.monkey.events.MonkeyEventSourceU2;
import com.android.commands.monkey.events.base.MonkeyActivityEvent;
import com.android.commands.monkey.events.base.MonkeyCommandEvent;
import com.android.commands.monkey.events.base.MonkeyFlipEvent;
import com.android.commands.monkey.events.base.MonkeyKeyEvent;
import com.android.commands.monkey.events.base.MonkeyRotationEvent;
import com.android.commands.monkey.events.base.MonkeyThrottleEvent;
import com.android.commands.monkey.events.base.MonkeyTouchEvent;
import com.android.commands.monkey.events.base.MonkeyTrackballEvent;
import com.android.commands.monkey.events.base.MonkeyWaitEvent;
import com.android.commands.monkey.fastbot.client.ActionType;
import com.android.commands.monkey.framework.AndroidDevice;
import com.android.commands.monkey.tree.TreeBuilder;
import com.android.commands.monkey.utils.JsonRPCResponse;
import com.android.commands.monkey.utils.Logger;
import com.android.commands.monkey.utils.MonkeyPermissionUtil;
import com.android.commands.monkey.utils.MonkeySemaphore;
import com.android.commands.monkey.utils.MonkeyUtils;
import com.android.commands.monkey.utils.OkHttpClient;
import com.android.commands.monkey.utils.ProxyServer;
import com.android.commands.monkey.utils.RandomHelper;
import com.android.commands.monkey.utils.StoneUtils;
import com.android.commands.monkey.utils.U2Client;
import com.android.commands.monkey.utils.UUIDHelper;
import com.android.commands.monkey.utils.Utils;
import com.google.gson.Gson;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NamedNodeMap;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.xml.sax.InputSource;

import java.io.File;
import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import javax.xml.xpath.XPathFactory;

import fi.iki.elonen.NanoHTTPD;
import okhttp3.Response;


/**
 * monkey event queue
 */
public class MonkeySourceRandomU2 implements MonkeyEventSourceU2 {
    public static final int FACTOR_TOUCH = 0;
    public static final int FACTOR_MOTION = 1;
    public static final int FACTOR_PINCHZOOM = 2;
    public static final int FACTOR_TRACKBALL = 3;
    public static final int FACTOR_ROTATION = 4;
    public static final int FACTOR_PERMISSION = 5;
    public static final int FACTOR_NAV = 6;
    public static final int FACTOR_MAJORNAV = 7;
    public static final int FACTOR_SYSOPS = 8;
    public static final int FACTOR_APPSWITCH = 9;
    public static final int FACTOR_FLIP = 10;
    public static final int FACTOR_ANYTHING = 11;
    public static final int FACTORZ_COUNT = 12; // should be last+1
    /**
     * Key events that move around the UI.
     */
    private static final int[] NAV_KEYS = {KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT,};
    /**
     * Key events that perform major navigation options (so shouldn't be sent as
     * much).
     */
    private static final int[] MAJOR_NAV_KEYS = {KeyEvent.KEYCODE_MENU, /*
                                                                          * KeyEvent
                                                                          * .
                                                                          * KEYCODE_SOFT_RIGHT,
                                                                          */
            KeyEvent.KEYCODE_DPAD_CENTER,};
    /**
     * Key events that perform system operations.
     */
    private static final int[] SYS_KEYS = {KeyEvent.KEYCODE_HOME, KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_CALL,
            KeyEvent.KEYCODE_ENDCALL, KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN,
            KeyEvent.KEYCODE_VOLUME_MUTE, KeyEvent.KEYCODE_MUTE,};
    /**
     * If a physical key exists?
     */
    private static final boolean[] PHYSICAL_KEY_EXISTS = new boolean[KeyEvent.getMaxKeyCode() + 1];
    /**
     * Possible screen rotation degrees
     **/
    private static final int[] SCREEN_ROTATION_DEGREES = {Surface.ROTATION_0, Surface.ROTATION_90,
            Surface.ROTATION_180, Surface.ROTATION_270,};
    private static final int GESTURE_TAP = 0;
    private static final int GESTURE_DRAG = 1;
    private static final int GESTURE_PINCH_OR_ZOOM = 2;

    static {
        for (int i = 0; i < PHYSICAL_KEY_EXISTS.length; ++i) {
            PHYSICAL_KEY_EXISTS[i] = true;
        }
        // Only examine SYS_KEYS
        for (int i = 0; i < SYS_KEYS.length; ++i) {
            PHYSICAL_KEY_EXISTS[SYS_KEYS[i]] = KeyCharacterMap.deviceHasKey(SYS_KEYS[i]);
        }
    }

    /**
     * percentages for each type of event. These will be remapped to working
     * values after we read any optional values.
     **/
    private float[] mFactors = new float[FACTORZ_COUNT];
    private List<ComponentName> mMainApps;
    private int mEventCount = 0; // total number of events generated so far
    private MonkeyEventQueue mQ;
    private Random mRandom;
    private int mVerbose = 0;
    private long mThrottle = 0;
    private MonkeyPermissionUtil mPermissionUtil;
    private boolean mKeyboardOpen = false;

    // zhangzhao fixed
    private int mEventId = 0;
    private boolean mRandomizeThrottle = false;
    private String currentActivity = "";
    private HashSet<String> activityHistory = new HashSet<>();
    private HashSet<String> mTotalActivities = new HashSet<>();
    private File mOutputDirectory;
    //private StatsClient.StatsDesc statsInfo = new StatsClient.StatsDesc();
    private String appVersion = "";
    private String packageName = "";

    private static long CLICK_WAIT_TIME = 0L;

    private static long LONG_CLICK_WAIT_TIME = 1000L;

    // new

    /**
     * deviceid from /sdcard/max.uuid, If read null, generate a random one locally
     */
    private String did = UUIDHelper.read();

    /**
     * The last event numbers in MQ.
     */
    private int lastMQEvents = 0;

    /**
     * The period of profiling coverage and other statistics.
     *  */
    private long mProfilePeriod;

    protected final HandlerThread mHandlerThread = new HandlerThread("MonkeySourceRandomU2");
    private final static Gson gson = new Gson();
    private OkHttpClient client;
    private Element hierarchy;
    private DocumentBuilder documentBuilder;
    private final ProxyServer server;
    private final U2Client u2Client;

    private int statusBarHeight = bytestStatusBarHeight;

    private List<LLMAction> actionList = new ArrayList<>();

    private int timestamp = 0;
    private int lastInputTimestamp = -1;

    private int actionIdCounter = 0;

    private HashMap<String, Integer> activityCountHistory = new HashMap();
    //new


    public MonkeySourceRandomU2(Random random, List<ComponentName> MainApps, long throttle, boolean randomizeThrottle,
                                boolean permissionTargetSystem, File outputDirectory, long profilePeriod) {
        // default values for random distributions
        // note, these are straight percentages, to match user input (cmd line
        // args)
        // but they will be converted to 0..1 values before the main loop runs.
        mFactors[FACTOR_TOUCH] = 15.0f;
        mFactors[FACTOR_MOTION] = 10.0f;
        mFactors[FACTOR_TRACKBALL] = 15.0f;
        // Adjust the values if we want to enable rotation by default.
        mFactors[FACTOR_ROTATION] = 0.0f;
        mFactors[FACTOR_NAV] = 25.0f;
        mFactors[FACTOR_MAJORNAV] = 15.0f;
        mFactors[FACTOR_SYSOPS] = 2.0f;
        mFactors[FACTOR_APPSWITCH] = 2.0f;
        mFactors[FACTOR_FLIP] = 1.0f;
        // disbale permission by default
        mFactors[FACTOR_PERMISSION] = 0.0f;
        mFactors[FACTOR_ANYTHING] = 13.0f;
        mFactors[FACTOR_PINCHZOOM] = 2.0f;

        mRandom = random;
        mMainApps = MainApps;
        mQ = new MonkeyEventQueue(random, throttle, randomizeThrottle);
        mPermissionUtil = new MonkeyPermissionUtil();
        mPermissionUtil.setTargetSystemPackages(permissionTargetSystem);
        getTotalAcitivities();
        mOutputDirectory = outputDirectory;
        mProfilePeriod = profilePeriod;
        Logger.println("[MonkeySourceRandomU2] ProfilePeriod: " + mProfilePeriod);


        connect();
        Logger.println("// device uuid is " + did);

        this.u2Client = U2Client.getInstance();
        this.server = new ProxyServer(8090, u2Client, this);
        try {
            server.start(NanoHTTPD.SOCKET_READ_TIMEOUT, false);
            Logger.println("[MonkeySourceRandomU2] proxyServer started. Listening tcp:8090");
        } catch (IOException e) {
            Logger.println("[MonkeySourceRandomU2] Error when trying to start the proxy server：" + e.getMessage());
            e.printStackTrace();
            throw new RuntimeException(e);
        }
    }

    public void connect() {
        client = OkHttpClient.getInstance();
        for (int i = 0; i < 10; i++){
            sleep(2000);
            if (client.connect()) {
                return;
            }
        }
        throw new RuntimeException("Fail to connect to U2Server");
    }

    void sleep(long time) {
        try {
            Thread.sleep(time);

        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public static String getKeyName(int keycode) {
        return KeyEvent.keyCodeToString(keycode);
    }

    /**
     * Looks up the keyCode from a given KEYCODE_NAME. NOTE: This may be an
     * expensive operation.
     *
     * @param keyName the name of the KEYCODE_VALUE to lookup.
     * @returns the intenger keyCode value, or KeyEvent.KEYCODE_UNKNOWN if not
     * found
     */
    public static int getKeyCode(String keyName) {
        return KeyEvent.keyCodeFromString(keyName);
    }

    private static boolean validateKeyCategory(String catName, int[] keys, float factor) {
        if (factor < 0.1f) {
            return true;
        }
        for (int i = 0; i < keys.length; ++i) {
            if (PHYSICAL_KEY_EXISTS[keys[i]]) {
                return true;
            }
        }
        System.err.println("** " + catName + " has no physical keys but with factor " + factor + "%.");
        return false;
    }

    /**
     * Adjust the percentages (after applying user values) and then normalize to
     * a 0..1 scale.
     */
    private boolean adjustEventFactors() {
        // go through all values and compute totals for user & default values
        float userSum = 0.0f;
        float defaultSum = 0.0f;
        int defaultCount = 0;
        for (int i = 0; i < FACTORZ_COUNT; ++i) {
            if (mFactors[i] <= 0.0f) { // user values are zero or negative
                userSum -= mFactors[i];
            } else {
                defaultSum += mFactors[i];
                ++defaultCount;
            }
        }

        // if the user request was > 100%, reject it
        if (userSum > 100.0f) {
            System.err.println("** Event weights > 100%");
            return false;
        }

        // if the user specified all of the weights, then they need to be 100%
        if (defaultCount == 0 && (userSum < 99.9f || userSum > 100.1f)) {
            System.err.println("** Event weights != 100%");
            return false;
        }

        // compute the adjustment necessary
        float defaultsTarget = (100.0f - userSum);
        float defaultsAdjustment = defaultsTarget / defaultSum;

        // fix all values, by adjusting defaults, or flipping user values back
        // to >0
        for (int i = 0; i < FACTORZ_COUNT; ++i) {
            if (mFactors[i] <= 0.0f) { // user values are zero or negative
                mFactors[i] = -mFactors[i];
            } else {
                mFactors[i] *= defaultsAdjustment;
            }
        }

        // if verbose, show factors
        if (mVerbose > 0) {
            System.out.println("// Event percentages:");
            for (int i = 0; i < FACTORZ_COUNT; ++i) {
                System.out.println("//   " + i + ": " + mFactors[i] + "%");
            }
        }

        if (!validateKeys()) {
            return false;
        }

        // finally, normalize and convert to running sum
        float sum = 0.0f;
        for (int i = 0; i < FACTORZ_COUNT; ++i) {
            sum += mFactors[i] / 100.0f;
            mFactors[i] = sum;
        }
        return true;
    }

    /**
     * See if any key exists for non-zero factors.
     */
    private boolean validateKeys() {
        return validateKeyCategory("NAV_KEYS", NAV_KEYS, mFactors[FACTOR_NAV])
                && validateKeyCategory("MAJOR_NAV_KEYS", MAJOR_NAV_KEYS, mFactors[FACTOR_MAJORNAV])
                && validateKeyCategory("SYS_KEYS", SYS_KEYS, mFactors[FACTOR_SYSOPS]);
    }

    /**
     * set the factors
     *
     * @param factors percentages for each type of event
     */
    public void setFactors(float factors[]) {
        int c = FACTORZ_COUNT;
        if (factors.length < c) {
            c = factors.length;
        }
        for (int i = 0; i < c; i++)
            mFactors[i] = factors[i];
    }

    public void setFactors(int index, float v) {
        mFactors[index] = v;
    }

    /**
     * Generates a random motion event. This method counts a down, move, and up
     * as multiple events.
     * <p>
     * TODO: Test & fix the selectors when non-zero percentages TODO: Longpress.
     * TODO: Fling. TODO: Meta state TODO: More useful than the random walk here
     * would be to pick a single random direction and distance, and divvy it up
     * into a random number of segments. (This would serve to generate fling
     * gestures, which are important).
     *
     * @param random  Random number source for positioning
     * @param gesture The gesture to perform.
     */
    private void generatePointerEvent(Random random, int gesture) {
        Display display = DisplayManagerGlobal.getInstance().getRealDisplay(Display.DEFAULT_DISPLAY);

        PointF p1 = randomPoint(random, display);
        PointF v1 = randomVector(random);

        long downAt = SystemClock.uptimeMillis();

        mQ.addLast(new MonkeyTouchEvent(MotionEvent.ACTION_DOWN).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                .setIntermediateNote(false));

        // sometimes we'll move during the touch
        if (gesture == GESTURE_DRAG) {
            int count = random.nextInt(10);
            for (int i = 0; i < count; i++) {
                randomWalk(random, display, p1, v1);

                mQ.addLast(new MonkeyTouchEvent(MotionEvent.ACTION_MOVE).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                        .setIntermediateNote(true));
            }
        } else if (gesture == GESTURE_PINCH_OR_ZOOM) {
            PointF p2 = randomPoint(random, display);
            PointF v2 = randomVector(random);

            randomWalk(random, display, p1, v1);
            mQ.addLast(new MonkeyTouchEvent(
                    MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT)).setDownTime(downAt)
                    .addPointer(0, p1.x, p1.y).addPointer(1, p2.x, p2.y).setIntermediateNote(true));

            int count = random.nextInt(10);
            for (int i = 0; i < count; i++) {
                randomWalk(random, display, p1, v1);
                randomWalk(random, display, p2, v2);

                mQ.addLast(new MonkeyTouchEvent(MotionEvent.ACTION_MOVE).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                        .addPointer(1, p2.x, p2.y).setIntermediateNote(true));
            }

            randomWalk(random, display, p1, v1);
            randomWalk(random, display, p2, v2);
            mQ.addLast(
                    new MonkeyTouchEvent(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT))
                            .setDownTime(downAt).addPointer(0, p1.x, p1.y).addPointer(1, p2.x, p2.y)
                            .setIntermediateNote(true));
        }

        randomWalk(random, display, p1, v1);
        mQ.addLast(new MonkeyTouchEvent(MotionEvent.ACTION_UP).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                .setIntermediateNote(false));
    }

    private PointF randomPoint(Random random, Display display) {
        return new PointF(random.nextInt(display.getWidth()), random.nextInt(display.getHeight()));
    }

    private PointF randomVector(Random random) {
        return new PointF((random.nextFloat() - 0.5f) * 50, (random.nextFloat() - 0.5f) * 50);
    }

    private void randomWalk(Random random, Display display, PointF point, PointF vector) {
        point.x = (float) Math.max(Math.min(point.x + random.nextFloat() * vector.x, display.getWidth()), 0);
        point.y = (float) Math.max(Math.min(point.y + random.nextFloat() * vector.y, display.getHeight()), 0);
    }

    /**
     * Generates a random trackball event. This consists of a sequence of small
     * moves, followed by an optional single click.
     * <p>
     * TODO: Longpress. TODO: Meta state TODO: Parameterize the % clicked TODO:
     * More useful than the random walk here would be to pick a single random
     * direction and distance, and divvy it up into a random number of segments.
     * (This would serve to generate fling gestures, which are important).
     *
     * @param random Random number source for positioning
     */
    private void generateTrackballEvent(Random random) {
        for (int i = 0; i < 10; ++i) {
            // generate a small random step
            int dX = random.nextInt(10) - 5;
            int dY = random.nextInt(10) - 5;

            mQ.addLast(
                    new MonkeyTrackballEvent(MotionEvent.ACTION_MOVE).addPointer(0, dX, dY).setIntermediateNote(i > 0));
        }

        // 10% of trackball moves end with a click
        if (0 == random.nextInt(10)) {
            long downAt = SystemClock.uptimeMillis();

            mQ.addLast(new MonkeyTrackballEvent(MotionEvent.ACTION_DOWN).setDownTime(downAt).addPointer(0, 0, 0)
                    .setIntermediateNote(true));

            mQ.addLast(new MonkeyTrackballEvent(MotionEvent.ACTION_UP).setDownTime(downAt).addPointer(0, 0, 0)
                    .setIntermediateNote(false));
        }
    }

    /**
     * Generates a random screen rotation event.
     *
     * @param random Random number source for rotation degree.
     */
    private void generateRotationEvent(Random random) {
        mQ.addLast(new MonkeyRotationEvent(SCREEN_ROTATION_DEGREES[random.nextInt(SCREEN_ROTATION_DEGREES.length)],
                random.nextBoolean()));
    }

    /**
     * Set the block_widgets interaction attrs to false to disable it during fuzzing.
     * @param document The source xml document.
     * @throws XPathExpressionException .
     */
    private void disableBlockWidgets(Document document) throws XPathExpressionException {
        // filter the block widgets
        XPath xpath = XPathFactory.newInstance().newXPath();
        for (String expr : server.blockWidgets) {
            NodeList nodes = (NodeList) xpath.evaluate(expr, document, XPathConstants.NODESET);
            for (int i = 0; i < nodes.getLength(); i++) {
                Element e = (Element) nodes.item(i);
                setElementAttributes(e);
            }
        }
    }

    private void disableBlockTrees(Document document) throws XPathExpressionException {
        XPath xpath = XPathFactory.newInstance().newXPath();
        for (String expr : server.blockTrees) {
            NodeList nodes = (NodeList) xpath.evaluate(expr, document, XPathConstants.NODESET);
            for (int i = 0; i < nodes.getLength(); i++) {
                Node node = nodes.item(i);
                if (node.getNodeType() == Node.ELEMENT_NODE) {
                    disableElementAndDescendants((Element) node);
                }
            }
        }
    }

    private void disableElementAndDescendants(Element element) {
        setElementAttributes(element);
        // Recursively disable all child elements
        NodeList children = element.getChildNodes();
        for (int i = 0; i < children.getLength(); i++) {
            Node child = children.item(i);
            if (child.getNodeType() == Node.ELEMENT_NODE) {
                disableElementAndDescendants((Element) child);
            }
        }
    }

    public void setElementAttributes(Element element) {
        if (mVerbose > 3) {
            Logger.println("[MonkeySourceRandomU2] Disable element: " + getElementAttributes(element));
        }
        // Disable the current element
        element.setAttribute("clickable", "false");
        element.setAttribute("long-clickable", "false");
        element.setAttribute("scrollable", "false");
        element.setAttribute("checkable", "false");
        element.setAttribute("enabled", "false");
        element.setAttribute("focusable", "false");

        // Log the disabled element
        if (mVerbose > 3) {
            Logger.println("[MonkeySourceRandomU2] Disabled element: " + getElementAttributes(element));
        }
    }

    public Map<String, String> getElementAttributes(Element element) {
        NamedNodeMap attrs = element.getAttributes();
        Map<String, String> map = new HashMap<>();
        for (int i = 0; i < attrs.getLength(); i++) {
            Node attr = attrs.item(i);
            map.put(attr.getNodeName(), attr.getNodeValue());
        }
        return map;
    }

    /**
     * Get the xml Document Builder
     * @return documentBuilder
     */
    public DocumentBuilder getDocumentBuilder() {
        if (documentBuilder == null)
        {
            DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
            try {
                documentBuilder = factory.newDocumentBuilder();
            } catch (ParserConfigurationException e) {
                throw new RuntimeException(e);
            }
        }
        return documentBuilder;
    }

    /**
     * The response from u2 contains all the components on screen.
     * @param tree The root of tree return by u2.
     * @return The root of the current activate testing package.
     */
    public Element getRootElement(Document tree) {
        NodeList childNodes = tree.getDocumentElement().getChildNodes();
        // traverse the child list backwards to filter the input_method keyboard
        for (int i = 0; i < childNodes.getLength(); i++) {
            Node node = childNodes.item(i);
            String cur_package;
            if (node.getNodeType() == Node.ELEMENT_NODE) {
                cur_package = ((Element) node).getAttribute("package");
                if (!"com.android.systemui".equals(cur_package) && !cur_package.contains("inputmethod") && !"android".equals(cur_package)) {
                    if (mVerbose > 3){
                        Logger.println("[MonkeySourceRandomU2] RootElement:"+cur_package);
                    }
                    return (Element) node;
                }
            }
        }
        return null;
    }


    /**
     * Dump hierarchy with u2.
     *  {
     *     "jsonrpc": "2.0",
     *     "id": 1,
     *     "method": "dumpWindowHierarchy",
     *     "params": [
     *         false,
     *         50
     *     ]
     * }
     */
    public void dumpHierarchy() {
        String res;

        if (server.shouldUseCache()){
            // Use the cached hierarchy response in the server.
            Logger.println("[MonkeySourceRandomU2] Latest event is MonkeyEvent. Use the cached hierarchy.");
            res = server.getHierarchyResponseCache();
        }
        else {
            try {
                Response hierachyResponse = u2Client.dumpHierarchy();
                res = hierachyResponse.body().string();
            } catch (IOException e)
            {
                throw new RuntimeException(e);
            }
        }


        JsonRPCResponse res_obj = gson.fromJson(res, JsonRPCResponse.class);
        String xmlString = res_obj.getResult();

        Logger.println("[MonkeySourceRandomU2] Successfully Got hierarchy");
        if (mVerbose > 3) {
            Logger.println("[MonkeySourceRandomU2] The full xmlString is:");
            Logger.println(xmlString);
        }

        Document document;

        try {
            // Use StringReader to transform the String into InputSource
            InputSource is = new InputSource(new StringReader(xmlString));
            // Parse InputSource to get the Document object
            document = getDocumentBuilder().parse(is);
            document.getDocumentElement().normalize();

            disableBlockWidgets(document);
            disableBlockTrees(document);

            hierarchy = getRootElement(document);
            TreeBuilder.filterTree(hierarchy);
//            stringOfGuiTree = hierarchy != null ? TreeBuilder.dumpDocumentStrWithOutTree(hierarchy) : "";
        } catch (Exception e){
            e.printStackTrace();
            throw new RuntimeException(e);
        }
    }
    public Element getRootInActiveWindow(){
        return hierarchy;
    }

    public Element getRootInActiveWindowSlow() {
        dumpHierarchy();
        sleep(1000);
        return getRootInActiveWindow();
    }

    private Element getRootNode(){
        ComponentName topActivityName = null;
        Element rootNode = null;
        int repeat = refectchInfoCount;

        // try to get Element quickly for several times.
        while (repeat-- > 0) {
            topActivityName = AndroidDevice.getTopActivityComponentName();
            rootNode = getRootInActiveWindow();
            // this two operations may not be the same
            if (rootNode == null || topActivityName == null) {
                sleep(refectchInfoWaitingInterval);
                continue;
            }
            Logger.println("// Event id: " + mEventId);
            break;
        }

        // If node is null, try to get Element slow for only once
        if (rootNode == null) {
            topActivityName = AndroidDevice.getTopActivityComponentName();
            rootNode = getRootInActiveWindowSlow();
            if (rootNode != null) {
                Logger.println("// Event id: " + mEventId);
            }
        }
        return rootNode;
    }

    protected void generateKeyEvent(int key) {
        MonkeyKeyEvent e = new MonkeyKeyEvent(KeyEvent.ACTION_DOWN, key);
//        e.setEventSource("LLM");
        addEvent(e);

        e = new MonkeyKeyEvent(KeyEvent.ACTION_UP, key);
//        e.setEventSource("LLM");
        addEvent(e);
    }

    public int getStatusBarHeight() {
        if (this.statusBarHeight == 0) {
            Display display = DisplayManagerGlobal.getInstance().getRealDisplay(Display.DEFAULT_DISPLAY);
            DisplayMetrics dm = new DisplayMetrics();
            display.getMetrics(dm);
            int w = display.getWidth();
            int h = display.getHeight();
            if (w == 1080 && h > 2100) {
                statusBarHeight = (int) (40 * dm.density);
            } else if (w == 1200 && h == 1824) {
                statusBarHeight = (int) (30 * dm.density);
            } else if (w == 1440 && h == 2696) {
                statusBarHeight = (int) (30 * dm.density);
            } else {
                statusBarHeight = (int) (24 * dm.density);
            }
        }
        return this.statusBarHeight;
    }

    /**
     * In mathematics, linear interpolation is a method of curve fitting using linear polynomials
     * to construct new data points within the range of a discrete set of known data points.
     * @param a
     * @param b
     * @param alpha
     * @return
     */
    private static float lerp(float a, float b, float alpha) {
        return (b - a) * alpha + a;
    }

    private void generateScrollEventAt(Rect nodeRect, ActionType type) {
        Rect displayBounds = AndroidDevice.getDisplayBounds();
        if (nodeRect == null) {
            nodeRect = AndroidDevice.getDisplayBounds();
        }

        PointF start = new PointF(nodeRect.exactCenterX(), nodeRect.exactCenterY());
        PointF end;

        switch (type) {
            case SCROLL_BOTTOM_UP:
                int top = getStatusBarHeight();
                if (top < displayBounds.top) {
                    top = displayBounds.top;
                }
                end = new PointF(start.x, top); // top is inclusive
                break;
            case SCROLL_TOP_DOWN:
                end = new PointF(start.x, displayBounds.bottom - 1); // bottom is
                // exclusive
                break;
            case SCROLL_LEFT_RIGHT:
                end = new PointF(displayBounds.right - 1, start.y); // right is
                // exclusive
                break;
            case SCROLL_RIGHT_LEFT:
                end = new PointF(displayBounds.left, start.y); // left is inclusive
                break;
            default:
                throw new RuntimeException("Should not reach here");
        }

        long downAt = SystemClock.uptimeMillis();


        addEvent(new MonkeyTouchEvent(MotionEvent.ACTION_DOWN).setDownTime(downAt).addPointer(0, start.x, start.y)
                .setIntermediateNote(false).setType(1));

        int steps = 10;
        long waitTime = swipeDuration / steps;
        for (int i = 0; i < steps; i++) {
            float alpha = i / (float) steps;
            addEvent(new MonkeyTouchEvent(MotionEvent.ACTION_MOVE).setDownTime(downAt)
                    .addPointer(0, lerp(start.x, end.x, alpha), lerp(start.y, end.y, alpha)).setIntermediateNote(true).setType(1));
            addEvent(new MonkeyWaitEvent(waitTime));
        }

        addEvent(new MonkeyTouchEvent(MotionEvent.ACTION_UP).setDownTime(downAt).addPointer(0, end.x, end.y)
                .setIntermediateNote(false).setType(1));
    }

    public boolean dealWithSystemUI(Element info) {
        if (info == null || info.getAttribute("package") == null){
            Logger.println("get null accessibility node");
            return false;
        }
        String packageName = info.getAttribute("package");
        if(packageName.equals("com.android.systemui")) {
            Logger.println("get notification window or other system windows");
            Rect bounds = AndroidDevice.getDisplayBounds();
            // press home
            generateKeyEvent(KeyEvent.KEYCODE_HOME);
            //scroll up
            generateScrollEventAt(bounds, SCROLL_BOTTOM_UP);
            // launch app
            generateActivityEvents(randomlyPickMainApp(), false);
            generateThrottleEvent(1000);
            return true;
        }
        return false;
    }

    // 校验 Rect 是否合法
    private boolean isRectValid(Rect rect) {
        return rect.left <= rect.right && rect.top <= rect.bottom;
    }


    public static Rect parseBounds(String bounds) {
        // 去除首尾的方括号
        if (!bounds.startsWith("[") || !bounds.endsWith("]")) {

            throw new IllegalArgumentException("Invalid bounds format: " + bounds);
        }
        bounds = bounds.substring(1, bounds.length() - 1);

        // 分割成两个坐标对
        String[] coordinatePairs = bounds.split("\\]\\[");
        if (coordinatePairs.length != 2) {
            throw new IllegalArgumentException("Invalid bounds format: " + bounds);
        }

        // 解析左上角坐标
        String[] leftTop = coordinatePairs[0].split(",");
        if (leftTop.length != 2) {
            throw new IllegalArgumentException("Invalid left-top coordinates: " + coordinatePairs[0]);
        }
        int left = Integer.parseInt(leftTop[0].trim());
        int top = Integer.parseInt(leftTop[1].trim());

        // 解析右下角坐标
        String[] rightBottom = coordinatePairs[1].split(",");
        if (rightBottom.length != 2) {
            throw new IllegalArgumentException("Invalid right-bottom coordinates: " + coordinatePairs[1]);
        }
        int right = Integer.parseInt(rightBottom[0].trim());
        int bottom = Integer.parseInt(rightBottom[1].trim());

        return new Rect(left, top, right, bottom);
    }

    @TargetApi(Build.VERSION_CODES.O)
    private void generateDesribedActions(Element node, List<String> actions) {
        if (node == null || !Boolean.parseBoolean(node.getAttribute("visible-to-user"))) return ;
        // 获取控件的边界
//        Rect bounds = new Rect();
//        node.getBoundsInScreen(bounds);
        String bounds_string = node.getAttribute("bounds");
        Rect bounds = parseBounds(bounds_string);
        if (!isRectValid(bounds)) {
            Logger.warningPrintln("skip invalid bounds view: " + bounds.toShortString());
            return;
        }
//        int left = bounds.left;
//        int top = bounds.top;
//        int width = bounds.width();
//        int height = bounds.height();
//        Logger.println("Bounds" + "Left: " + left + ", Top: " + top + ", Width: " + width + ", Height: " + height);

//        // 计算中心坐标
//        float centerX = bounds.centerX();
//        float centerY = bounds.centerY();

        StringBuilder viewDesc = new StringBuilder();
        String nodeText = node.getAttribute("text") != null ? node.getAttribute("text") : "";
        String nodeContentDesc = node.getAttribute("content-desc") != null ? node.getAttribute("content-desc") : "";
        if (Boolean.parseBoolean(node.getAttribute("checked")) || Boolean.parseBoolean(node.getAttribute("selected"))){
            viewDesc.append("a checked view ");
        }else{
            viewDesc.append("a view ");
        }

        if(!nodeContentDesc.equals("")){
            nodeContentDesc = nodeContentDesc.replace("\n", "  ");
            if (nodeContentDesc.length() > 20) {
                nodeContentDesc = nodeContentDesc.substring(0, 20) + "...";
            }
            viewDesc.append(String.format(" \"%s\"",nodeContentDesc));
        }

        if (!nodeText.equals("")){
            nodeText = nodeText.replace("\n", "  ");
            if (nodeText.length() > 20) {
                nodeText = nodeText.substring(0, 20) + "...";
            }
            viewDesc.append(String.format(" with text \"%s\"", nodeText));
        }

        // 检查属性并添加动作
        List<String> actionDescList = new ArrayList<>();
        if (Boolean.parseBoolean(node.getAttribute("clickable")) || Boolean.parseBoolean(node.getAttribute("checkable")) || Boolean.parseBoolean(node.getAttribute("long-clickable"))) {
            actionDescList.add("can click (" + actionIdCounter++ + ")");
            actionDescList.add("can longclick (" + actionIdCounter++ + ")");
            actionList.add(new LLMAction(ActionType.CLICK, packageName, currentActivity, bounds, viewDesc.toString()));
            actionList.add(new LLMAction(ActionType.LONG_CLICK, packageName, currentActivity, bounds, viewDesc.toString()));
        }
        if ("android.widget.EditText".equals(node.getAttribute("class")) || "android.widget.AutoCompleteTextView".equals(node.getAttribute("class"))) {
            actionDescList.add("can edit (" + actionIdCounter++ + ")");
            actionList.add(new LLMAction(ActionType.CLICK, packageName, currentActivity, bounds, viewDesc.toString()).setInputText(RandomHelper.nextString(20)).setEditText(true).setUseAdbInput(true));
        }
        if (Boolean.parseBoolean(node.getAttribute("scrollable"))) {
            actionDescList.add("can scroll up (" + actionIdCounter++ + ")");
            actionDescList.add("can scroll down (" + actionIdCounter++ + ")");
            actionDescList.add("can scroll left (" + actionIdCounter++ + ")");
            actionDescList.add("can scroll right (" + actionIdCounter++ + ")");
            actionList.add(new LLMAction(ActionType.SCROLL_BOTTOM_UP, packageName, currentActivity, bounds, viewDesc.toString()));
            actionList.add(new LLMAction(ActionType.SCROLL_TOP_DOWN, packageName, currentActivity, bounds, viewDesc.toString()));
            actionList.add(new LLMAction(ActionType.SCROLL_RIGHT_LEFT, packageName, currentActivity, bounds, viewDesc.toString()));
            actionList.add(new LLMAction(ActionType.SCROLL_LEFT_RIGHT, packageName, currentActivity, bounds, viewDesc.toString()));
        }

        // 如果有动作，将其添加到视图信息中
        if (!actionDescList.isEmpty()) {
            viewDesc.append(" that ").append(String.join(", ", actionDescList));
            actions.add(viewDesc.toString());
        }

        // 递归获取子节点
        NodeList children = node.getChildNodes();
        for (int i = 0; i < children.getLength(); i++) {
            Node child = children.item(i);
            if (child.getNodeType() == Node.ELEMENT_NODE) {
                generateDesribedActions((Element) child, actions);
            }
        }

    }

    @TargetApi(Build.VERSION_CODES.O)
    private void generateRandomElemAction(Element node) {
        // dump string list to call generateDesribedActions func
        List<String> actions = new ArrayList<>();
        // only use global var actionList to get all ui element action
        generateDesribedActions(node,actions);
    }

    private void generateAppSwitchEvent() {
        generateKeyEvent(KeyEvent.KEYCODE_APP_SWITCH);
        generateThrottleEvent(500);
        if (RandomHelper.nextBoolean()) {
            Logger.println("press HOME after app switch");
            generateKeyEvent(KeyEvent.KEYCODE_HOME);
        } else {
            Logger.println("press BACK after app switch");
            generateKeyEvent(KeyEvent.KEYCODE_BACK);
        }
        generateThrottleEvent(mThrottle);
    }

    protected void generateActivateEvent() { // duplicated with custmozie
        Logger.infoPrintln("generate app switch events.");
        generateAppSwitchEvent();
    }

    protected void generateClickEventAt(Rect nodeRect, long waitTime, boolean useRandomClick) {
        Rect bounds = nodeRect;
        if (bounds == null) {
            Logger.warningPrintln("Error to fetch bounds.");
            bounds = AndroidDevice.getDisplayBounds();
        }

        PointF p1;
        if (useRandomClick) {
            int width = bounds.width() > 0 ? getRandom().nextInt(bounds.width()) : 0;
            int height = bounds.height() > 0 ? getRandom().nextInt(bounds.height()) : 0;
            p1 = new PointF(bounds.left + width, bounds.top + height);
        } else
            p1 = new PointF(bounds.left + bounds.width()/2.0f, bounds.top + bounds.height()/2.0f);
        if (!bounds.contains((int) p1.x, (int) p1.y)) {
            Logger.warningFormat("Invalid bounds: %s", bounds);
            return;
        }
//        p1 = shieldBlackRect(p1);

        long downAt = SystemClock.uptimeMillis();

        addEvent(new MonkeyTouchEvent(MotionEvent.ACTION_DOWN).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                .setIntermediateNote(false));

        if (waitTime > 0) {
            MonkeyWaitEvent we = new MonkeyWaitEvent(waitTime);
            addEvent(we);
        }

        addEvent(new MonkeyTouchEvent(MotionEvent.ACTION_UP).setDownTime(downAt).addPointer(0, p1.x, p1.y)
                .setIntermediateNote(false));
    }
    protected void generateClickEventAt(Rect nodeRect, long waitTime) {
        generateClickEventAt(nodeRect, waitTime, useRandomClick);
    }

    public Random getRandom() {
        return mRandom;
    }

    private void generateClearEvent(Rect bounds) {
        generateClickEventAt(bounds, LONG_CLICK_WAIT_TIME);
        generateKeyEvent(KeyEvent.KEYCODE_DEL);
        generateClickEventAt(bounds, CLICK_WAIT_TIME);
    }

    private void attemptToSendTextByKeyEvents(String inputText) {
        char[] szRes = inputText.toCharArray(); // Convert String to Char array

        KeyCharacterMap CharMap;
        if (Build.VERSION.SDK_INT >= 11) // My soft runs until API 5
            CharMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
        else
            CharMap = KeyCharacterMap.load(KeyCharacterMap.ALPHA);

        KeyEvent[] events = CharMap.getEvents(szRes);

        for (int i = 0; i < events.length; i += 2) {
            generateKeyEvent(events[i].getKeyCode());
        }
        generateKeyEvent(KeyEvent.KEYCODE_ENTER);
    }

    private void doInput(LLMAction action) {
        String inputText = action.getInputText();
        boolean useAdbInput = action.isUseAdbInput();
        if (inputText != null && !inputText.equals("")) {
            Logger.println("Input text is " + inputText);
            if (action.isClearText())
                generateClearEvent(action.getBoundingBox());

            if (action.isRawInput()) {
                if (!AndroidDevice.sendText(inputText))
                    attemptToSendTextByKeyEvents(inputText);
                return;
            }

            Logger.println("MonkeyCommandEvent added " + inputText);
            addEvent(new MonkeyCommandEvent("input text " + inputText));

        } else {
            if (lastInputTimestamp == timestamp) {
                Logger.warningPrintln("checkVirtualKeyboard: Input only once.");
                return;
            } else {
                lastInputTimestamp = timestamp;
            }
            if (action.isEditText() || AndroidDevice.isVirtualKeyboardOpened()) {
                generateKeyEvent(KeyEvent.KEYCODE_ESCAPE);
            }
        }
    }
    private void generateEventsForActionInternal(Action action) {
        ActionType actionType = action.getType();
        Logger.println("action type: " + actionType.toString());
        switch (actionType) {
            case BACK:
                generateKeyEvent(KeyEvent.KEYCODE_BACK);
                break;
            case ROTATE_SCREEN:
                generateRotationEvent(mRandom);
                break;
            case ACTIVATE:
                generateActivateEvent();
                break;
            case START:
                generateActivityEvents(randomlyPickMainApp(), false);
                break;
            case CLICK:
                generateClickEventAt(((LLMAction) action).getBoundingBox(), CLICK_WAIT_TIME);
                doInput((LLMAction) action);
                break;
            case LONG_CLICK:
                long waitTime = ((LLMAction) action).getWaitTime();
                if (waitTime == 0) {
                    waitTime = LONG_CLICK_WAIT_TIME;
                }
                generateClickEventAt(((LLMAction) action).getBoundingBox(), waitTime);
                break;
            case SCROLL_BOTTOM_UP:
            case SCROLL_TOP_DOWN:
            case SCROLL_LEFT_RIGHT:
            case SCROLL_RIGHT_LEFT:
                generateScrollEventAt(((LLMAction) action).getBoundingBox(), action.getType());
                break;
            default:
                throw new RuntimeException("Should not reach here");
        }
    }


    private void generateEventsForAction(Action action) {
        generateEventsForActionInternal(action);
//        long throttle = action.getThrottle();
//        generateThrottleEvent(throttle);
    }

    /**
     * generate a random event based on mFactor
     */
    private void generateEvents() {
        Element rootNode = getRootNode();
        if(dealWithSystemUI(rootNode))
            return;

        // generate Action based on ui element
        generateRandomElemAction(rootNode);

        // radnomly pick a action from current ui action list
        int actionId = mRandom.nextInt(actionList.size());
        LLMAction randomAction = actionList.get(actionId);

        // improve click event probability
        if (mRandom.nextDouble() < 0.6){
            randomAction.setType(ActionType.CLICK);
        }

        // generate corresponding monkey event
        generateEventsForAction(randomAction);

        // reset actionList for new ui screen
        actionList.clear();
    }

    public boolean validate() {
        boolean ret = true;
        // only populate & dump permissions if enabled
        if (mFactors[FACTOR_PERMISSION] != 0.0f) {
            ret &= mPermissionUtil.populatePermissionsMapping();
            if (ret && mVerbose >= 2) {
                mPermissionUtil.dump();
            }
        }
        return ret & adjustEventFactors();
    }

    public void setVerbose(int verbose) {
        mVerbose = verbose;
    }

    /**
     * generate an activity event
     */
    public void generateActivity() {
        MonkeyActivityEvent e = new MonkeyActivityEvent(mMainApps.get(mRandom.nextInt(mMainApps.size())));
        mQ.addLast(e);
    }


    //zhangzhao fixed

    /**
     * if the queue is empty, we generate events first
     *
     * @return the first event in the queue
     */
    public MonkeyEvent getNextEvent() {
        checkAppActivity();
        if (checkMonkeyStepDone()){
            if (shouldProfile()){
                Logger.println("[MonkeySourceRandomU2] Profiling coverage...");
                u2GetCoverage();
            }
            MonkeySemaphore.doneMonkey.release();
            if (mVerbose > 3){
                Logger.println("[MonkeySourceRandomU2] release semaphore： doneMonkey");
            }
        }
        if (mQ.isEmpty()) {
            try {
                if (mVerbose > 3) {
                    Logger.println("[MonkeySourceRandomU2] wait semaphore: stepMonkey");
                }
                MonkeySemaphore.stepMonkey.acquire();
                if (mVerbose > 3) {
                    Logger.println("[MonkeySourceRandomU2] acquired semaphore: stepMonkey");
                }
                Logger.println("[MonkeySourceRandomU2] stepsCount: " + server.stepsCount);
                if (server.monkeyIsOver) {
                    Logger.println("[MonkeySourceRandomU2] received signal: MonkeyIsOver");
                    return null;
                }
//                File currentScreen = getCurrentScreen();
//                if (tarpitDetector.detectedUiTarpit(currentScreen, lastScreen)) {
//                    Logger.println("detect ui tarpit!");
//                }
                generateEvents();
//                lastScreen=currentScreen;
            }catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        }
        // 二次防御性检查（确保mQ不为空）
        if (mQ.isEmpty()) {
            Logger.errorPrintln("严重错误：事件队列仍为空，生成紧急返回事件");
            MonkeyKeyEvent e = new MonkeyKeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK);
            mQ.addLast(e);
            e = new MonkeyKeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK);
            mQ.addLast(e);
        }
        mEventCount++;
        Logger.infoPrintln("event counts:"+mEventCount);
        lastMQEvents = mQ.size();
        MonkeyEvent e = mQ.getFirst();
        mQ.removeFirst();
        return e;
    }

    /**
     * Check if the previous monkey step has been finished.
     * Algorithm: the event queue is empty and the length of event queue change from 1 to 0
     * This is for checking an edge case: As fastbot starts, the event queue is empty, but this
     * does not represent a monkey event was finished.
     * @return a monkey event was finished
     */
    private boolean checkMonkeyStepDone() {
        return (!hasEvent() && lastMQEvents == 1);
    }

    private void u2GetCoverage() {
        HashSet<String> set = mTotalActivities;

        String[] testedActivities = this.activityHistory.toArray(new String[0]);
        int j = 0;
        String activity = "";
        for (String testedActivity : testedActivities) {
            activity = testedActivity;
            if (set.contains(activity)) {
                j++;
            }
        }

        float f = 0;
        int s = set.size();
        if (s > 0) {
            f = 1.0f * j / s * 100;
        }

        String[] totalActivities = set.toArray(new String[0]);
        server.saveCoverageStatistics(
                new CoverageData(server.stepsCount, f, totalActivities, testedActivities, activityCountHistory)
        );
    }

    private final boolean hasEvent() {
        return !mQ.isEmpty();
    }

    private boolean shouldProfile(){
        return mProfilePeriod > 0 && server.stepsCount != 0 && server.stepsCount % mProfilePeriod == 0;
    }

    public boolean appCrashed(String processName, int pid, String shortMsg, String longMsg, long timeMillis,
                              String stackTrace, String version) {
        return false;
    }

    public void checkAppActivity() {
        ComponentName cn = AndroidDevice.getTopActivityComponentName();
        if (cn == null) {
            Logger.println(": debug, gettask api error");
            clearEvent();
            startRandomMainApp();
            return;
        }
        String className = cn.getClassName();
        String pkg = cn.getPackageName();
        boolean allow = MonkeyUtils.getPackageFilter().checkEnteringPackage(pkg);

        if (allow) {
            if (!this.currentActivity.equals(className)) {
                this.currentActivity = className;
                activityHistory.add(this.currentActivity);
                activityCountHistory.put(
                        currentActivity,
                        StoneUtils.getOrDefaultFromHashMap(activityCountHistory, this.currentActivity, 0) + 1
                );
                Logger.println("// [Monkey] current activity is " + this.currentActivity);
            }
            return;
        }

        Logger.println("// the top activity is " + className + ", not testing app, need inject restart app");
        clearEvent();
        startRandomMainApp();
        return;
    }

    /**
     * Get the top Activity info from the Activity stack
     * @return Component name of the top activity
     */
    protected ComponentName getTopActivityComponentName() {
        return AndroidDevice.getTopActivityComponentName();
    }

    public void updateActivityHistory() {
        ComponentName cn = getTopActivityComponentName();
        if (cn == null) {
            Logger.println("// get activity api error");
            return;
        }
        String className = cn.getClassName();
        if (!this.currentActivity.equals(className)) {
            this.currentActivity = className;
            activityHistory.add(this.currentActivity);
            activityCountHistory.put(
                    currentActivity,
                    StoneUtils.getOrDefaultFromHashMap(activityCountHistory, this.currentActivity, 0) + 1
            );
            Logger.println("// [Script] current activity is " + this.currentActivity);
            timestamp++;
        }
    }
    
    private final void clearEvent() {
        while (!mQ.isEmpty()) {
            MonkeyEvent e = mQ.removeFirst();
        }
    }

    private final void addEvent(MonkeyEvent event) {
        mQ.addLast(event);
        event.setEventId(mEventId++);
    }

    public ComponentName randomlyPickMainApp() {
        int total = mMainApps.size();
        int index = mRandom.nextInt(total);
        return mMainApps.get(index);
    }

    protected void startRandomMainApp() {
        generateActivityEvents(randomlyPickMainApp(), false);
    }

    protected void generateActivityEvents(ComponentName app, boolean clearPackage) {
        MonkeyActivityEvent e = new MonkeyActivityEvent(app);
        addEvent(e);
        generateThrottleEvent(startAfterNSecondsofsleep); // waiting for the loading of apps

    }

    protected void generateThrottleEvent(long base) {
        long throttle = base;
        if (mRandomizeThrottle && (mThrottle > 0)) {
            throttle = mRandom.nextLong();
            if (throttle < 0) {
                throttle = -throttle;
            }
            throttle %= base;
            ++throttle;
        }
        if (throttle < 0) {
            throttle = -throttle;
        }
        addEvent(new MonkeyThrottleEvent(throttle));
    }

    private void getTotalAcitivities() {
        try {
            for (String p : MonkeyUtils.getPackageFilter().getmValidPackages()) {
                PackageInfo packageInfo = AndroidDevice.packageManager.getPackageInfo(p, PackageManager.GET_ACTIVITIES);
                if (packageInfo != null) {
                    if (packageInfo.packageName.equals("com.android.packageinstaller"))
                        continue;
                    if (packageInfo.activities != null) {
                        for (ActivityInfo activityInfo : packageInfo.activities) {
                            mTotalActivities.add(activityInfo.name);
                        }
                    }
                }
            }

        } catch (Exception e) {
        }
    }

    public HashSet<String> getmTotalAcitivities() {
        return mTotalActivities;
    }


    private void printCoverage() {
        HashSet<String> set = getmTotalAcitivities();

        Logger.println("Total app activities:");
        int i = 0;
        for (String activity : set) {
            i++;
            Logger.println(String.format("%4d %s", i, activity));
        }

        String[] testedActivities = this.activityHistory.toArray(new String[0]);
        Arrays.sort(testedActivities);
        int j = 0;
        String activity = "";
        Logger.println("Explored app activities:");
        for (i = 0; i < testedActivities.length; i++) {
            activity = testedActivities[i];
            if (set.contains(activity)) {
                Logger.println(String.format("%4d %s", j + 1, activity));
                j++;
            }
        }

        float f = 0;
        int s = set.size();
        if (s > 0) {
            f = 1.0f * j / s * 100;
            Logger.println("Activity of Coverage: " + f + "%");
        }

        String[] totalActivities = set.toArray(new String[0]);
        Arrays.sort(totalActivities);


        Utils.activityStatistics(mOutputDirectory, testedActivities, totalActivities, new ArrayList<Map<String, String>>(), f, new HashMap<String, Integer>());

    }

    public void setAttr(String packageName, String appVersion) {
        this.appVersion = appVersion;
        this.packageName = packageName;
    }

    public void tearDown() {
        if (!shouldProfile()){
            u2GetCoverage();
        }
        server.tearDown();
        printCoverage();
    }

}
