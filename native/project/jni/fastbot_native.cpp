/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su
 */
#include "fastbot_native.h"
#include "Model.h"
#include "ModelReusableAgent.h"
#include "utils.hpp"
#include "StateFactory.h"
#include "Element.h"
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

static fastbotx::ModelPtr _fastbot_model = nullptr;

//getAction
jstring JNICALL Java_com_bytedance_fastbot_AiClient_b0bhkadf(JNIEnv *env, jobject, jstring activity,
                                                             jstring xmlDescOfGuiTree) {
    if (nullptr == _fastbot_model) {
        _fastbot_model = fastbotx::Model::create();
    }
    const char *xmlDescriptionCString = env->GetStringUTFChars(xmlDescOfGuiTree, nullptr);
    const char *activityCString = env->GetStringUTFChars(activity, nullptr);
    std::string xmlString = std::string(xmlDescriptionCString);
    std::string activityString = std::string(activityCString);
    std::string operationString = _fastbot_model->getOperate(xmlString, activityString);
    LOGD("do action opt is : %s", operationString.c_str());
    env->ReleaseStringUTFChars(xmlDescOfGuiTree, xmlDescriptionCString);
    env->ReleaseStringUTFChars(activity, activityCString);
    return env->NewStringUTF(operationString.c_str());
}

// for single device, just addAgent as empty device //InitAgent
void JNICALL Java_com_bytedance_fastbot_AiClient_fgdsaf5d(JNIEnv *env, jobject, jint agentType,
                                                          jstring packageName, jint deviceType) {
    if (nullptr == _fastbot_model) {
        _fastbot_model = fastbotx::Model::create();
    }
    auto algorithmType = (fastbotx::AlgorithmType) agentType;
    auto agentPointer = _fastbot_model->addAgent("", algorithmType,
                                                 (fastbotx::DeviceType) deviceType);
    const char *packageNameCString = "";
    if (env)
        packageNameCString = env->GetStringUTFChars(packageName, nullptr);
    _fastbot_model->setPackageName(std::string(packageNameCString));

    BLOG("init agent with type %d, %s,  %d", agentType, packageNameCString, deviceType);
    if (algorithmType == fastbotx::AlgorithmType::Reuse) {
        auto reuseAgentPtr = std::dynamic_pointer_cast<fastbotx::ModelReusableAgent>(agentPointer);
        reuseAgentPtr->loadReuseModel(std::string(packageNameCString));
        // Reset per-episode state when initializing the agent for a new run/round
        reuseAgentPtr->beginNewEpisode();
        BLOG("Called beginNewEpisode() on reuse agent after load");
        if (env)
            env->ReleaseStringUTFChars(packageName, packageNameCString);
    }
}

// load ResMapping
void JNICALL
Java_com_bytedance_fastbot_AiClient_jdasdbil(JNIEnv *env, jobject, jstring resMappingFilepath) {
    if (nullptr == _fastbot_model) {
        _fastbot_model = fastbotx::Model::create();
    }
    const char *resourceMappingPath = env->GetStringUTFChars(resMappingFilepath, nullptr);
    auto preference = _fastbot_model->getPreference();
    if (preference) {
        preference->loadMixResMapping(std::string(resourceMappingPath));
    }
    env->ReleaseStringUTFChars(resMappingFilepath, resourceMappingPath);
}

// to check if a point is in black widget area
jboolean JNICALL
Java_com_bytedance_fastbot_AiClient_nkksdhdk(JNIEnv *env, jobject, jstring activity, jfloat pointX,
                                             jfloat pointY) {
    bool isShield = false;
    if (nullptr == _fastbot_model) {
        BLOGE("%s", "model null, check point failed!");
        return isShield;
    }
    const char *activityStr = env->GetStringUTFChars(activity, nullptr);
    auto preference = _fastbot_model->getPreference();
    if (preference) {
        isShield = preference->checkPointIsInBlackRects(std::string(activityStr),
                                                        static_cast<int>(pointX),
                                                        static_cast<int>(pointY));
    }
    env->ReleaseStringUTFChars(activity, activityStr);
    return isShield;
}

jstring JNICALL Java_com_bytedance_fastbot_AiClient_getNativeVersion(JNIEnv *env, jclass clazz) {
    return env->NewStringUTF(FASTBOT_VERSION);
}

// Helper: process addCurrentPageAsPrecondition, return 0 on success, non-zero on failure
static int processAddCurrentPageAsPrecondition(const std::string &xmlString) {
    if (_fastbot_model == nullptr) {
        BLOGE("Model not initialized!");
        return 2; // model not initialized
    }

    if (xmlString.empty()) {
        BLOGE("addCurrentPageAsPrecondition: empty XML passed from Java");
        return 3; // empty xml
    }

    // parse xml into Element
    auto element = fastbotx::Element::createFromXml(xmlString);
    if (element == nullptr) {
        BLOGE("addCurrentPageAsPrecondition: failed to parse XML into Element");
        return 4; // parse failed
    }

    // create state from element
    auto activityStr = std::make_shared<std::string>("");
    fastbotx::StatePtr state = nullptr;
    try {
        state = fastbotx::StateFactory::createState(fastbotx::AlgorithmType::Reuse, activityStr, element);
    } catch (const std::exception &e) {
        BLOGE("addCurrentPageAsPrecondition: exception creating State: %s", e.what());
        return 5; // state creation exception
    }

    if (state == nullptr) {
        BLOGE("addCurrentPageAsPrecondition: state creation returned null");
        return 6; // null state
    }

    // 获取当前的 ModelReusableAgent 实例
    auto agent = _fastbot_model->getAgent("");
    auto reuseAgent = std::dynamic_pointer_cast<fastbotx::ModelReusableAgent>(agent);

    if (reuseAgent == nullptr) {
        BLOGE("addCurrentPageAsPrecondition: failed to get ModelReusableAgent instance");
        return 7; // agent not found
    }

    // Now call the agent API with the parsed state
    try {
        reuseAgent->addCurrentPageAsPrecondition(state);
    } catch (const std::exception &e) {
        BLOGE("addCurrentPageAsPrecondition: exception while calling agent: %s", e.what());
        return 8;
    }

    BLOG("reuseAgent->addCurrentPageAsPrecondition(state) called successfully");
    return 0;
}

// New sync JNI wrapper returning status
jint JNICALL Java_com_bytedance_fastbot_AiClient_addCurrentPageAsPreconditionSync(JNIEnv *env, jobject obj, jstring xml) {
    const char *xmlCString = nullptr;
    std::string xmlString;
    if (xml != nullptr) xmlCString = env->GetStringUTFChars(xml, nullptr);
    xmlString = xmlCString ? std::string(xmlCString) : std::string();

    int status = processAddCurrentPageAsPrecondition(xmlString);

    if (xmlCString) env->ReleaseStringUTFChars(xml, xmlCString);
    return status;
}

// Existing void wrapper kept for compatibility; call sync and throw on non-zero
void JNICALL Java_com_bytedance_fastbot_AiClient_addCurrentPageAsPrecondition(JNIEnv *env, jobject obj, jstring xml) {
    if (_fastbot_model == nullptr) {
        BLOGE("Model not initialized!");
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass) env->ThrowNew(exClass, "Model not initialized in native code");
        return;
    }

    const char *xmlCString = nullptr;
    if (xml != nullptr) xmlCString = env->GetStringUTFChars(xml, nullptr);
    std::string xmlString = xmlCString ? std::string(xmlCString) : std::string();

    int status = processAddCurrentPageAsPrecondition(xmlString);
    if (xmlCString) env->ReleaseStringUTFChars(xml, xmlCString);

    if (status != 0) {
        // create an informative message
        std::string msg = "addCurrentPageAsPrecondition failed, status=" + std::to_string(status);
        BLOGE("%s", msg.c_str());
        jclass exClass = env->FindClass("java/lang/RuntimeException");
        if (exClass) env->ThrowNew(exClass, msg.c_str());
    }
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    // Log via our macro so it goes to Android logcat under FastbotNative tag
    BLOG("JNI_OnLoad called, native library initialized");
    return JNI_VERSION_1_6;
}

#ifdef __cplusplus
}
#endif
