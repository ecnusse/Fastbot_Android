package com.android.commands.monkey.source;

import static com.android.commands.monkey.fastbot.client.ActionType.SCROLL_BOTTOM_UP;
import static com.android.commands.monkey.fastbot.client.ActionType.SCROLL_TOP_DOWN;
import static com.android.commands.monkey.framework.AndroidDevice.stopPackage;
import static com.android.commands.monkey.utils.Config.bytestStatusBarHeight;
import static com.android.commands.monkey.utils.Config.defaultGUIThrottle;
import static com.android.commands.monkey.utils.Config.doHistoryRestart;
import static com.android.commands.monkey.utils.Config.doHoming;
import static com.android.commands.monkey.utils.Config.execPreShell;
import static com.android.commands.monkey.utils.Config.execPreShellEveryStartup;
import static com.android.commands.monkey.utils.Config.execSchema;
import static com.android.commands.monkey.utils.Config.execSchemaEveryStartup;
import static com.android.commands.monkey.utils.Config.historyRestartRate;
import static com.android.commands.monkey.utils.Config.homeAfterNSecondsofsleep;
import static com.android.commands.monkey.utils.Config.homingRate;
import static com.android.commands.monkey.utils.Config.imageWriterCount;
import static com.android.commands.monkey.utils.Config.refectchInfoCount;
import static com.android.commands.monkey.utils.Config.refectchInfoWaitingInterval;
import static com.android.commands.monkey.utils.Config.saveGUITreeToXmlEveryStep;
import static com.android.commands.monkey.utils.Config.schemaTraversalMode;
import static com.android.commands.monkey.utils.Config.scrollAfterNSecondsofsleep;
import static com.android.commands.monkey.utils.Config.startAfterDoScrollAction;
import static com.android.commands.monkey.utils.Config.startAfterDoScrollActionTimes;
import static com.android.commands.monkey.utils.Config.startAfterDoScrollBottomAction;
import static com.android.commands.monkey.utils.Config.startAfterDoScrollBottomActionTimes;
import static com.android.commands.monkey.utils.Config.startAfterNSecondsofsleep;
import static com.android.commands.monkey.utils.Config.swipeDuration;
import static com.android.commands.monkey.utils.Config.takeScreenshotForEveryStep;
import static com.android.commands.monkey.utils.Config.throttleForExecPreSchema;
import static com.android.commands.monkey.utils.Config.throttleForExecPreShell;
import static com.android.commands.monkey.utils.Config.useRandomClick;

import android.app.IActivityManager;
import android.app.UiAutomation;
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
import android.view.IWindowManager;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;

import com.android.commands.monkey.Monkey;
import com.android.commands.monkey.action.Action;
import com.android.commands.monkey.action.FuzzAction;
import com.android.commands.monkey.action.ModelAction;
import com.android.commands.monkey.events.CustomEvent;
import com.android.commands.monkey.events.CustomEventFuzzer;
import com.android.commands.monkey.events.MonkeyEvent;
import com.android.commands.monkey.events.MonkeyEventQueue;
import com.android.commands.monkey.events.MonkeyEventSource;
import com.android.commands.monkey.events.base.MonkeyActivityEvent;
import com.android.commands.monkey.events.base.MonkeyCommandEvent;
import com.android.commands.monkey.events.base.MonkeyDataActivityEvent;
import com.android.commands.monkey.events.base.MonkeyIMEEvent;
import com.android.commands.monkey.events.base.MonkeyKeyEvent;
import com.android.commands.monkey.events.base.MonkeyRotationEvent;
import com.android.commands.monkey.events.base.MonkeySchemaEvent;
import com.android.commands.monkey.events.base.MonkeyThrottleEvent;
import com.android.commands.monkey.events.base.MonkeyTouchEvent;
import com.android.commands.monkey.events.base.MonkeyWaitEvent;
import com.android.commands.monkey.events.base.mutation.MutationAirplaneEvent;
import com.android.commands.monkey.events.base.mutation.MutationAlwaysFinishActivityEvent;
import com.android.commands.monkey.events.base.mutation.MutationWifiEvent;
import com.android.commands.monkey.events.customize.ClickEvent;
import com.android.commands.monkey.events.customize.ShellEvent;
import com.android.commands.monkey.fastbot.client.ActionType;
import com.android.commands.monkey.fastbot.client.Operate;
import com.android.commands.monkey.framework.AndroidDevice;
import com.android.commands.monkey.provider.SchemaProvider;
import com.android.commands.monkey.provider.ShellProvider;
import com.android.commands.monkey.tree.TreeBuilder;
import com.android.commands.monkey.utils.Config;
import com.android.commands.monkey.utils.ImageWriterQueue;
import com.android.commands.monkey.utils.JsonRPCResponse;
import com.android.commands.monkey.utils.Logger;
import com.android.commands.monkey.utils.MonkeySemaphore;
import com.android.commands.monkey.utils.MonkeyUtils;
import com.android.commands.monkey.utils.OkHttpClient;
import com.android.commands.monkey.utils.ProxyServer;
import com.android.commands.monkey.utils.RandomHelper;
import com.android.commands.monkey.utils.U2Client;
import com.android.commands.monkey.utils.UUIDHelper;
import com.android.commands.monkey.utils.Utils;
import com.bytedance.fastbot.AiClient;
import com.google.gson.Gson;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NamedNodeMap;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.xml.sax.InputSource;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Random;
import java.util.Stack;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.xpath.XPath;
import javax.xml.xpath.XPathConstants;
import javax.xml.xpath.XPathExpressionException;
import javax.xml.xpath.XPathFactory;

import fi.iki.elonen.NanoHTTPD;
import okhttp3.Response;

public class MonkeySourceApeU2 implements MonkeyEventSource {

    private static long CLICK_WAIT_TIME = 0L;
    private static long LONG_CLICK_WAIT_TIME = 1000L;
    /**
     * UiAutomation client and connection
     */
    protected UiAutomation mUiAutomation;
    public Monkey monkey = null;

    private int timestamp = 0;
    private int lastInputTimestamp = -1;

    private List<ComponentName> mMainApps;
    private Map<String, String[]> packagePermissions;
    /**
     * total number of events generated so far
     */
    private int mEventCount = 0;
    /**
     * monkey event queue
     */
    private final MonkeyEventQueue mQ;
    /**
     * The last event numbers in MQ.
     */
    private int lastMQEvents = 0;
    /**
     * debug level
     */
    protected int mVerbose = 0;
    /**
     * The delay between event inputs
     **/
    protected long mThrottle = defaultGUIThrottle;
    /**
     * Whether to randomize each throttle (0-mThrottle ms) inserted between
     * events.
     */
    private boolean mRandomizeThrottle = false;
    /**
     * random generator
     */
    private Random mRandom;

    protected int mEventId = 0;
    /**
     * customize the height of the top tarbar of the device, this area needs to be cropped out
     */
    private int statusBarHeight = bytestStatusBarHeight;

    private File mOutputDirectory;
    /**
     * screenshot asynchronous storage queue
     */
    private ImageWriterQueue[] mImageWriters;
    /**
     * Record tested activities, but there are activities that may miss quick jumps
     */
    private HashSet<String> activityHistory = new HashSet<>();
    private String currentActivity = "";
    /**
     * appliaction total、stub、plugin activity
     */
    private HashSet<String> mTotalActivities = new HashSet<>();
    private HashSet<String> stubActivities = new HashSet<>();
    private HashSet<String> pluginActivities = new HashSet<>();

    protected static Locale stringFormatLocale = Locale.ENGLISH;

    protected int timeStep = 0;
    /**
     * deviceid from /sdcard/max.uuid, If read null, generate a random one locally
     */
    private String did = UUIDHelper.read();
    /**
     * execute shell only on first startup
     */
    private boolean firstExecShell = true;
    /**
     * Execute schema only on first startup
     */
    private boolean firstSchema = true;
    /**
     * Record executed schemas for schema traversal
     */
    private Stack<String> schemaStack = new Stack<>();

    private String appVersion = "";
    private String packageName = "";

    /**
     * user-defined application launcher activity intent
     */
    private String intentAction = null;
    /**
     * user-defined application launcher activity intent data
     */
    private String intentData = null;
    /**
     * user-defined application launcher activity, not used for now
     */
    private String quickActivity = null;

    private int appRestarted = 0;
    protected boolean fullFuzzing = true;

    private String stringOfGuiTree;

    protected final HandlerThread mHandlerThread = new HandlerThread("MonkeySourceApeU2");
    private final static Gson gson = new Gson();
    private OkHttpClient client;
    private Element hierarchy;
    private DocumentBuilder documentBuilder;
    private final ProxyServer server;
    private final U2Client u2Client;

    public MonkeySourceApeU2(Random random, List<ComponentName> MainApps,
                                 long throttle, boolean randomizeThrottle, boolean permissionTargetSystem,
                                 File outputDirectory){

        mRandom = random;
        mMainApps = MainApps;
        mThrottle = throttle;
        mRandomizeThrottle = randomizeThrottle;
        mQ = new MonkeyEventQueue(random, 0, false); // we manage throttle
        mOutputDirectory = outputDirectory;

        packagePermissions = new HashMap<>();
        for (ComponentName app : MainApps) {
            packagePermissions.put(app.getPackageName(), AndroidDevice.getGrantedPermissions(app.getPackageName()));
        }

        getTotalActivities();

        connect();
        Logger.println("// device uuid is " + did);

        this.u2Client = U2Client.getInstance();
        this.server = new ProxyServer(8090, u2Client);
        try {
            server.start(NanoHTTPD.SOCKET_READ_TIMEOUT, false);
            Logger.println("[MonkeySourceApeU2] proxyServer started. Listening tcp:8090");
        } catch (IOException e) {
            Logger.println("[MonkeySourceApeU2] Error when trying to start the proxy server：" + e.getMessage());
            e.printStackTrace();
            throw new RuntimeException(e);
        }
    }

    /**
     * If this activity could be interacted with. Should be in white list or not in blacklist or
     * not specified.
     * @param cn Component Name of this activity
     * @return If could be interacted, return true
     */
    private boolean checkAppActivity(ComponentName cn) {
        return cn == null || MonkeyUtils.getPackageFilter().checkEnteringPackage(cn.getPackageName());
    }

    private final void clearEvent() {
        while (!mQ.isEmpty()) {
            MonkeyEvent e = mQ.removeFirst();
        }
    }

    public void setVerbose(int verbose) {
        mVerbose = verbose;
    }

    protected void checkAppActivity() {
        ComponentName cn = getTopActivityComponentName();
        if (cn == null) {
            Logger.println("// get activity api error");
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
                Logger.println("// current activity is " + this.currentActivity);
                timestamp++;
            }
        }else
            dealWithBlockedActivity(cn);
    }

    /**
     * If the given component is not allowed to interact with, start a random app or
     * generating a fuzzing action
     * @param cn Component that is not allowed to interact with
     */
    private void dealWithBlockedActivity(ComponentName cn) {
        String className = cn.getClassName();
        if (!hasEvent()) {
            if (appRestarted == 0) {
                Logger.println("// the top activity is " + className + ", not testing app, need inject restart app");
                startRandomMainApp();
                appRestarted = 1;
            } else {
                if (!AndroidDevice.isAtPhoneLauncher(className)) {
                    Logger.println("// the top activity is " + className + ", not testing app, need inject fuzz event");
                    Action fuzzingAction = generateFuzzingAction(true);
                    generateEventsForAction(fuzzingAction);
                } else {
                    fullFuzzing = false;
                }
                appRestarted = 0;
            }
        }
    }

    protected void startRandomMainApp() {
        generateActivityEvents(randomlyPickMainApp(), false, false);
    }

    /**
     * if the queue is empty, we generate events first
     *
     * @return the first event in the queue
     */
    public MonkeyEvent getNextEvent() {
        checkAppActivity();
        if (checkMonkeyStepDone()){
            MonkeySemaphore.doneMonkey.release();
            Logger.println("[MonkeySourceApeU2] release semaphore： doneMonkey");
        }
        if (!hasEvent()) {
            try {
                Logger.println("[MonkeySourceApeU2] wait semaphore: stepMonkey");
                MonkeySemaphore.stepMonkey.acquire();
                Logger.println("[MonkeySourceApeU2] release semaphore: stepMonkey");
                if (server.monkeyIsOver) {
                    Logger.println("[MonkeySourceApeU2] received signal: MonkeyIsOver");
                    return null;
                }
                generateEvents();
            } catch (RuntimeException e) {
                Logger.errorPrintln(e.getMessage());
                e.printStackTrace();
                clearEvent();
                return null;
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        }
        mEventCount++;
        lastMQEvents = mQ.size();
        //  Logger.println("MQ Events Size is " + lastMQEvents);
        return popEvent();
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


    /**
     * generate an activity event
     */
    private final MonkeyEvent popEvent() {
        return mQ.removeFirst();
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
            Logger.println("[MonkeySourceApeU2] Latest event is MonkeyEvent. Use the cached hierarchy.");
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

        Logger.println("[MonkeySourceApeU2] Successfully Got hierarchy");
        if (mVerbose > 3) {
            Logger.println("[MonkeySourceApeU2] The full xmlString is:");
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

            hierarchy = getRootElement(document);
            TreeBuilder.filterTree(hierarchy);
            stringOfGuiTree = hierarchy != null ? TreeBuilder.dumpDocumentStrWithOutTree(hierarchy) : "";
        } catch (Exception e){
            e.printStackTrace();
            throw new RuntimeException(e);
        }
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
                Logger.println("[MonkeySourceApeU2] Disable element: " + getElementAttributes(e));
                e.setAttribute("clickable", "false");
                e.setAttribute("long-clickable", "false");
                e.setAttribute("scrollable", "false");
                e.setAttribute("checkable", "false");
                e.setAttribute("enabled", "false");
                e.setAttribute("focusable", "false");

                // Logger.println("[MonkeySourceApeU2] Disabled element: " + getElementAttributes(e));
            }
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
                        Logger.println("[MonkeySourceApeU2] RootElement:"+cur_package);
                    }
                    return (Element) node;
                }
            }
        }
        return null;
    }

    public Element getHierarchy() {
        return hierarchy;
    }

    void resetRotation() {
        addEvent(new MonkeyRotationEvent(Surface.ROTATION_0, false));
    }

    private final void addEvent(MonkeyEvent event) {
        mQ.addLast(event);
        event.setEventId(mEventId++);
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
            generateActivityEvents(randomlyPickMainApp(), false, false);
            generateThrottleEvent(1000);
            return true;
        }
        return false;
    }


    public Element getRootInActiveWindow(){
        return hierarchy;
    }

    protected void generateEvents() {
        long start = System.currentTimeMillis();
        if (hasEvent()) {
            return;
        }

        resetRotation();
        ComponentName topActivityName = null;
        String stringOfGuiTree = "";
        Action fuzzingAction = null;
        Element info = null;

        int repeat = refectchInfoCount;
        dumpHierarchy();
        stringOfGuiTree = this.stringOfGuiTree;
//        Logger.println("Repeat: " + stringOfGuiTree);

        // try to get AccessibilityNodeInfo quickly for several times.
        while (repeat-- > 0) {
            topActivityName = this.getTopActivityComponentName();
            info = getRootInActiveWindow();
            // this two operations may not be the same
            if (info == null || topActivityName == null) {
                sleep(refectchInfoWaitingInterval);
                continue;
            }

            Logger.println("// Event id: " + mEventId);
            if (dealWithSystemUI(info))
                return;
            break;
        }

        // If node is null, try to get AccessibilityNodeInfo slow for only once
        if (info == null) {
            topActivityName = this.getTopActivityComponentName();
            info = getRootInActiveWindowSlow();
            if (info != null) {
                Logger.println("// Event id: " + mEventId);
                if (dealWithSystemUI(info))
                    return;
            }
        }

        // If node is not null, build tree and recycle this resource.
        if (info != null) {
            stringOfGuiTree = this.stringOfGuiTree;
            if (mVerbose > 3) {
                Logger.println("[MonkeySourceApeU2] StringOfGuiTree for agent in fastbot:");
                Logger.println(stringOfGuiTree);
            }
            // info.recycle();
        }

        // For user specified actions, during executing, fuzzing is not allowed.
        boolean allowFuzzing = true;

        Logger.println("topActivity Name: " + topActivityName);
//        Logger.println("GuiTree: " + stringOfGuiTree);

        if (topActivityName != null && !"".equals(stringOfGuiTree)) {
            try {
                long rpc_start = System.currentTimeMillis();

                Logger.println("// Dumped stringOfGuiTree");
                Logger.println("topActivityName: " + topActivityName.getClassName());
//                Logger.println(stringOfGuiTree);

                Operate operate = AiClient.getAction(topActivityName.getClassName(), stringOfGuiTree);
                operate.throttle += (int) this.mThrottle;
                // For user specified actions, during executing, fuzzing is not allowed.
                allowFuzzing = operate.allowFuzzing;
                ActionType type = operate.act;
                Logger.println("action type: " + type.toString());
                Logger.println("rpc cost time: " + (System.currentTimeMillis() - rpc_start));

                Rect rect = new Rect(0, 0, 0, 0);
                List<PointF> pointFloats = new ArrayList<>();

                if (type.requireTarget()) {
                    List<Short> points = operate.pos;
                    if (points != null && points.size() >= 4) {
                        rect = new Rect((Short) points.get(0), (Short) points.get(1), (Short) points.get(2), (Short) points.get(3));
                    } else {
                        type = ActionType.NOP;
                    }
                }

                timeStep++;
                String sid = operate.sid;
                String aid = operate.aid;
                long timeMillis = System.currentTimeMillis();

                if (saveGUITreeToXmlEveryStep) {
                    checkOutputDir();
                    File xmlFile = new File(checkOutputDir(), String.format(stringFormatLocale,
                            "step-%d-%s-%s-%s.xml", timeStep, sid, aid, timeMillis));
                    Logger.infoFormat("Saving GUI tree to %s at step %d %s %s",
                            xmlFile, timeStep, sid, aid);

                    BufferedWriter out;
                    try {
                        out = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(xmlFile, false)));
                        out.write(stringOfGuiTree);
                        out.flush();
                        out.close();
                    } catch (java.io.FileNotFoundException e) {
                    } catch (java.io.IOException e) {
                    }
                }

                if (takeScreenshotForEveryStep) {
                    checkOutputDir();
                    File screenshotFile = new File(checkOutputDir(), String.format(stringFormatLocale,
                            "step-%d-%s-%s-%s.png", timeStep, sid, aid, timeMillis));
                    Logger.infoFormat("Saving screen shot to %s at step %d %s %s",
                            screenshotFile, timeStep, sid, aid);
                    // takeScreenshot(screenshotFile);
                }

                ModelAction modelAction = new ModelAction(type, topActivityName, pointFloats, rect);
                modelAction.setThrottle(operate.throttle);

                // Complete the info for specific action type
                switch (type) {
                    case CLICK:
                        modelAction.setInputText(operate.text);
                        modelAction.setClearText(operate.clear);
                        modelAction.setEditText(operate.editable);
                        modelAction.setRawInput(operate.rawinput);
                        modelAction.setUseAdbInput(operate.adbinput);
                        break;
                    case LONG_CLICK:
                        modelAction.setWaitTime(operate.waitTime);
                        break;
                    case SHELL_EVENT:
                        modelAction.setShellCommand(operate.text);
                        modelAction.setWaitTime(operate.waitTime);
                        break;
                    default:
                        break;
                }

                generateEventsForAction(modelAction);

                // check if could select next fuzz action from full fuzz-able action options.
                switch (type) {
                    case RESTART:
                    case CLEAN_RESTART:
                    case CRASH:
                        fullFuzzing = false;
                        break;
                    case BACK:
                        fullFuzzing = !AndroidDevice.isAtAppMain(topActivityName.getClassName(), topActivityName.getPackageName());
                        break;
                    default:
                        fullFuzzing = !AndroidDevice.isAtPhoneLauncher(topActivityName.getClassName());
                        break;
                }

            } catch (Exception e) {
                e.printStackTrace();
                generateThrottleEvent(mThrottle);
            }
        } else {
            Logger.println(
                    "// top activity is null or the corresponding tree is null, " +
                            "accessibility maybe error, fuzz needed."
            );
            fuzzingAction = generateFuzzingAction(fullFuzzing);
            generateEventsForAction(fuzzingAction);
        }
    }

    protected final boolean hasEvent() {
        return !mQ.isEmpty();
    }

    void sleep(long time) {
        try {
            Thread.sleep(time);

        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    public File getOutputDir() {
        return mOutputDirectory;
    }


    public Element getRootInActiveWindowSlow() {
        dumpHierarchy();
        sleep(1000);
        return getRootInActiveWindow();
    }

    /**
     * Get the top Activity info from the Activity stack
     * @return Component name of the top activity
     */
    protected ComponentName getTopActivityComponentName() {
        return AndroidDevice.getTopActivityComponentName();
    }

    protected File checkOutputDir() {
        File dir = getOutputDir();
        if (!dir.exists()) {
            dir.mkdirs();
        }
        return dir;
    }

    /**
     * According to the action type of the action argument, generate its corresponding
     * event, and set throttle if necessary.
     * @param action generated action, could be action from native model, or generated fuzzing
     *               action from CustomEventFuzzer
     */
    protected void generateEventsForAction(Action action) {
        generateEventsForActionInternal(action);
        // If this action is for fuzzing, we don't need extra throttle time.
        long throttle = (action instanceof FuzzAction ? 0 : action.getThrottle());
        generateThrottleEvent(throttle);
    }

    /**
     * According to the action type of the action argument, generate its corresponding
     * event
     * @param action generated action, could be action from native model, or generated fuzzing
     *               action from CustomEventFuzzer
     */
    private void generateEventsForActionInternal(Action action) {
        ActionType actionType = action.getType();
        switch (actionType) {
            case FUZZ:
                generateFuzzingEvents((FuzzAction) action);
                break;
            case START:
                generateActivityEvents(randomlyPickMainApp(), false, false);
                break;
            case RESTART:
                restartPackage(randomlyPickMainApp(), false, "start action(RESTART)");
                break;
            case CLEAN_RESTART:
                restartPackage(randomlyPickMainApp(), true, "start action(CLEAN_RESTART)");
                break;
            case NOP:
                generateThrottleEvent(action.getThrottle());
                break;
            case ACTIVATE:
                generateActivateEvent();
                break;
            case BACK:
                generateKeyEvent(KeyEvent.KEYCODE_BACK);
                break;
            case CLICK:
                generateClickEventAt(((ModelAction) action).getBoundingBox(), CLICK_WAIT_TIME);
                doInput((ModelAction) action);
                break;
            case LONG_CLICK:
                long waitTime = ((ModelAction) action).getWaitTime();
                if (waitTime == 0) {
                    waitTime = LONG_CLICK_WAIT_TIME;
                }
                generateClickEventAt(((ModelAction) action).getBoundingBox(), waitTime);
                break;
            case SCROLL_BOTTOM_UP:
            case SCROLL_TOP_DOWN:
            case SCROLL_LEFT_RIGHT:
            case SCROLL_RIGHT_LEFT:
                generateScrollEventAt(((ModelAction) action).getBoundingBox(), action.getType());
                break;
            case SCROLL_BOTTOM_UP_N:
                // Scroll from bottom to up for [0,3+5] times.
                int scroll_B_T_N = 3 + RandomHelper.nextInt(5);
                while (scroll_B_T_N-- > 0) {
                    generateScrollEventAt(((ModelAction) action).getBoundingBox(), SCROLL_BOTTOM_UP);
                }
                break;
            case SHELL_EVENT:
                ModelAction modelAction = (ModelAction)action;
                ShellEvent shellEvent = new ShellEvent(modelAction.getShellCommand(), modelAction.getWaitTime());
                List<MonkeyEvent> monkeyEvents = shellEvent.generateMonkeyEvents();
                addEvents(monkeyEvents);
                break;
            default:
                throw new RuntimeException("Should not reach here");
        }
    }

    private final void addEvents(List<MonkeyEvent> events){
        for (int i = 0; i < events.size(); i++) {
            addEvent(events.get(i));
        }
    }

    /**
     * According to the returned action from native model, parse the text inside, and
     * input those texts if IME is activated.
     * @param action returned action from native model
     */
    private void doInput(ModelAction action) {
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

            if (!useAdbInput) {
                Logger.println("MonkeyIMEEvent added " + inputText);
                addEvent(new MonkeyIMEEvent(inputText));
            } else {
                Logger.println("MonkeyCommandEvent added " + inputText);
                addEvent(new MonkeyCommandEvent("input text " + inputText));
            }

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

    private void generateClearEvent(Rect bounds) {
        generateClickEventAt(bounds, LONG_CLICK_WAIT_TIME);
        generateKeyEvent(KeyEvent.KEYCODE_DEL);
        generateClickEventAt(bounds, CLICK_WAIT_TIME);
    }

    protected void generateActivateEvent() { // duplicated with custmozie
        Logger.infoPrintln("generate app switch events.");
        generateAppSwitchEvent();
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

    protected void generateClickEventAt(Rect nodeRect, long waitTime) {
        generateClickEventAt(nodeRect, waitTime, useRandomClick);
    }

    /**
     * Generate events for activity
     * @param app The info about this activity.
     * @param clearPackage If should delete the user data and revoke granted permissions
     * @param startFromHistory If need to start activity form history stack
     */
    protected void generateActivityEvents(ComponentName app, boolean clearPackage, boolean startFromHistory) {
        if (clearPackage) {
            clearPackage(app.getPackageName());
        }
        generateShellEvents();
        boolean startbyHistory = false; // if should start activity from history stack
        if (startFromHistory && doHistoryRestart && RandomHelper.toss(historyRestartRate)) {
            Logger.println("start from history task");
            startbyHistory = true;
        }
        if (intentData != null) { // if not null, start activity with intent and the data inside
            MonkeyDataActivityEvent e = new MonkeyDataActivityEvent(app, intentAction, intentData, quickActivity, startbyHistory);
            addEvent(e);
        } else { // default
            MonkeyActivityEvent e = new MonkeyActivityEvent(app, startbyHistory);
            addEvent(e);
        }
        generateThrottleEvent(startAfterNSecondsofsleep); // waiting for the loading of apps
        generateSchemaEvents();
        generateActivityScrollEvents();
    }

    /**
     * Calling this method, you could delete the user data and revoke granted permission of
     * this specific package.
     * @param packageName The package name of which data to delete.
     */
    public void clearPackage(String packageName) {
        String[] permissions = this.packagePermissions.get(packageName);
        if (permissions == null) {
            Logger.warningPrintln("Stop clearing untracked package: " + packageName);
            return;
        }
        if(AndroidDevice.clearPackage(packageName, permissions))
            Logger.infoPrintln("Package "+packageName+" cleared.");
    }

    /**
     * Generate click event at the given rectangle area
     * @param nodeRect the given rectangle area to click
     * @param waitTime after performing click, the time to wait for
     * @param useRandomClick if should perform click randomly in rectangle area
     */
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
        p1 = shieldBlackRect(p1);
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

    public boolean validate() {
        client = OkHttpClient.getInstance();
        return client.connect();
    }


    public Random getRandom() {
        return mRandom;
    }

    /**
     * According to user specified schema, choose schema randomly or in order.
     */
    private void generateSchemaEvents() {
        if (execSchema) {
            if (firstSchema || execSchemaEveryStartup) {
                String schema = SchemaProvider.randomNext(); // choose schema randomly

                if (schemaTraversalMode) { // choose schema in order
                    if (schemaStack.empty()) {
                        ArrayList<String> strings = SchemaProvider.getStrings();
                        for (String s : strings) {
                            schemaStack.push(s);
                        }
                    }
                    if (schemaStack.empty()) return;

                    schema = schemaStack.pop();
                }

                if ("".equals(schema)) return;

                Logger.println("fastbot exec schema: " + schema);
                MonkeySchemaEvent e = new MonkeySchemaEvent(schema);
                addEvent(e);

                generateThrottleEvent(throttleForExecPreSchema);
                this.firstSchema = false;
            }
        }
    }

    private void generateActivityScrollEvents() {
        if (startAfterDoScrollAction) {
            int i = startAfterDoScrollActionTimes;
            while (i-- > 0) {
                generateScrollEventAt(AndroidDevice.getDisplayBounds(), SCROLL_TOP_DOWN);
                generateThrottleEvent(scrollAfterNSecondsofsleep);
            }
        }

        if (startAfterDoScrollBottomAction) {
            int i = startAfterDoScrollBottomActionTimes;
            while (i-- > 0) {
                generateScrollEventAt(AndroidDevice.getDisplayBounds(), SCROLL_BOTTOM_UP);
                generateThrottleEvent(scrollAfterNSecondsofsleep);
            }
        }
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


    /**
     * Grant permission to testing app
     * @param packageName package name of the testing app
     * @param reason the reason to grant permission
     */
    public void grantRuntimePermissions(String packageName, String reason) {
        String[] permissions = this.packagePermissions.get(packageName);
        if (permissions == null) {
            Logger.warningPrintln("Stop granting permissions to untracked package: " + packageName);
            return;
        }
        AndroidDevice.grantRuntimePermissions(packageName, permissions, reason);
    }

    /**
     * Pick an activity that we can interact with.
     * @return Chosen activity component name
     */
    public ComponentName randomlyPickMainApp() {
        int total = mMainApps.size();
        int index = mRandom.nextInt(total);
        return mMainApps.get(index);
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
     * Generate mutation event and execute it
     * @param iwm IWindowManager instance
     * @param iam IActivityManager instance
     * @param verbose verbose
     */
    public void startMutation(IWindowManager iwm, IActivityManager iam, int verbose) {
        MonkeyEvent event = null;
        double total = Config.doMutationAirplaneFuzzing + Config.doMutationMutationAlwaysFinishActivitysFuzzing
                + Config.doMutationWifiFuzzing;
        double rate = RandomHelper.nextDouble();
        if (rate < Config.doMutationMutationAlwaysFinishActivitysFuzzing) {
            event = new MutationAlwaysFinishActivityEvent();
        } else if (rate < Config.doMutationMutationAlwaysFinishActivitysFuzzing
                + Config.doMutationWifiFuzzing) {
            event = new MutationWifiEvent();
        } else if (rate < total){
            event = new MutationAirplaneEvent();
        }
        if (event != null) {
            event.injectEvent(iwm, iam, mVerbose);
        }
    }

    /**
     * Restart the specific package
     * @param cn Component Name of the specific app activity
     * @param clearPackage If should clear user data and permissions or not
     * @param reason String reason to restart package
     */
    protected void restartPackage(ComponentName cn, boolean clearPackage, String reason) {
        if (doHoming && RandomHelper.toss(homingRate)) {
            Logger.println("press HOME before app kill");
            generateKeyEvent(KeyEvent.KEYCODE_HOME);
            generateThrottleEvent(homeAfterNSecondsofsleep);
        }
        String packageName = cn.getPackageName();
        Logger.infoPrintln("Try to restart package " + packageName + " for " + reason);
        stopPackage(cn.getPackageName());
        generateActivityEvents(cn, clearPackage, true);
    }

    protected void generateKeyEvent(int key) {
        MonkeyKeyEvent e = new MonkeyKeyEvent(KeyEvent.ACTION_DOWN, key);
        addEvent(e);

        e = new MonkeyKeyEvent(KeyEvent.ACTION_UP, key);
        addEvent(e);
    }

    /**
     * According to user specified shell commands, choose command randomly
     */
    private void generateShellEvents() {
        if (execPreShell) {
            String command = ShellProvider.randomNext(); // choose command randomly
            if (!"".equals(command) && (firstExecShell || execPreShellEveryStartup)) {
                Logger.println("shell: " + command);
                try {
                    AndroidDevice.executeCommandAndWaitFor(command.split(" "));
                    sleep(throttleForExecPreShell);
                    this.firstExecShell = false;
                } catch (Exception e) {
                }
            }
        }
    }


    /**
     * Grant permission to all testing app
     * @param reason the reason to grant permission
     */
    public void grantRuntimePermissions(String reason) {
        for (ComponentName cn : mMainApps) {
            grantRuntimePermissions(cn.getPackageName(), reason);
        }
    }

    private void getTotalActivities() {
        try {
            for (String p : MonkeyUtils.getPackageFilter().getmValidPackages()) {
                PackageInfo packageInfo = AndroidDevice.packageManager.getPackageInfo(p, PackageManager.GET_ACTIVITIES);
                if (packageInfo != null) {
                    if (packageInfo.packageName.equals("com.android.packageinstaller"))
                        continue;
                    if(packageInfo.activities != null){
                        for (ActivityInfo activityInfo : packageInfo.activities) {
                            mTotalActivities.add(activityInfo.name);
                        }
                    }
                }
            }
        } catch (Exception e) {
        }
    }

    private String getAppVersionCode() {
        try {
            for (String p : MonkeyUtils.getPackageFilter().getmValidPackages()) {
                PackageInfo packageInfo = AndroidDevice.packageManager.getPackageInfo(p, PackageManager.GET_ACTIVITIES);
                if (packageInfo != null) {
                    if (packageInfo.packageName.equals(this.packageName)) {
                        return packageInfo.versionName;
                    }
                }
            }

        } catch (Exception e) {
        }
        return "";
    }

    protected void generateThrottleEvent(long base) {
        long throttle = base;
        if (mRandomizeThrottle && (throttle > 0)) {
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

    /**
     * Return a generated fuzzing action, which could be from complete fuzzing list or simplified
     * fuzzing list
     * @param sampleFromAllFuzzingActions if should select fuzzing action from all possible
     *                                   fuzzing options
     * @return A wrapped action object, containing the generated fuzzing actions.
     */
    protected FuzzAction generateFuzzingAction(boolean sampleFromAllFuzzingActions) {
        List<CustomEvent> events = sampleFromAllFuzzingActions ?
                CustomEventFuzzer.generateFuzzingEvents() :
                CustomEventFuzzer.generateSimplifyFuzzingEvents();
        return new FuzzAction(events);
    }

    /**
     * Generate monkey events according to CustomEvents inside FuzzAction
     * @param action Object of FuzzAction, containing all corresponding CustomEvents
     */
    private void generateFuzzingEvents(FuzzAction action) {
        List<CustomEvent> events = action.getFuzzingEvents();
        long throttle = action.getThrottle();
        for (CustomEvent event : events) {
            if (event instanceof ClickEvent) {
                PointF point = ((ClickEvent) event).getPoint();
                point = shieldBlackRect(point);
                ((ClickEvent) event).setPoint(point);
            }
            List<MonkeyEvent> monkeyEvents = event.generateMonkeyEvents();
            for (MonkeyEvent me : monkeyEvents) {
                if (me == null) {
                    throw new RuntimeException();
                }
                addEvent(me);
            }
            generateThrottleEvent(throttle);
        }
    }

    private PointF shieldBlackRect(PointF p) {
        // move to native: AiClient.checkPointIsShield
        int retryTimes = 10;
        PointF p1 = p;
        do {
            if (!AiClient.checkPointIsShield(this.currentActivity, p1)) {
                break;
            }
            // re generate a point
            Rect displayBounds = AndroidDevice.getDisplayBounds();
            float unitx = displayBounds.height() / 20.0f;
            float unity = displayBounds.width() / 10.0f;
            p1.x = p.x + retryTimes * unitx * RandomHelper.nextInt(8);
            p1.y = p.y + retryTimes * unity * RandomHelper.nextInt(17);
            p1.x = p1.x % displayBounds.width();
            p1.y = p1.y % displayBounds.height();
        } while (retryTimes-- > 0);
        return p1;
    }

    public void setAttribute(String packageName, String appVersion, String intentAction, String intentData, String quickActivity) {
        this.packageName = packageName;
        this.appVersion = (!appVersion.equals("")) ? appVersion : this.getAppVersionCode();
        this.intentAction = intentAction;
        this.intentData = intentData;
        this.quickActivity = quickActivity;
    }

    /**
     * Init and loading reuse model
     */
    public void initReuseAgent() {
        AiClient.InitAgent(AiClient.AlgorithmType.Reuse, this.packageName);
    }

    private void printCoverage() {
        HashSet<String> set = mTotalActivities;

        Logger.println("Total app activities:");
        int i = 0;
        for (String activity : set) {
            i++;
            Logger.println(String.format(Locale.ENGLISH,"%4d %s", i, activity));
        }

        String[] testedActivities = this.activityHistory.toArray(new String[0]);
        Arrays.sort(testedActivities);
        int j = 0;
        String activity = "";
        Logger.println("Explored app activities:");
        for (i = 0; i < testedActivities.length; i++) {
            activity = testedActivities[i];
            if (set.contains(activity)) {
                Logger.println(String.format(Locale.ENGLISH,"%4d %s", j + 1, activity));
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

    public void tearDown() {
        server.tearDown();
        printCoverage();
    }

}
