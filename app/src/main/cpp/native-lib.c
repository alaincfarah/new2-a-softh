#include <jni.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

// PJSIP (pjsua) headers
#include <pjsua-lib/pjsua.h>

#define TAG "native-lib"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static JavaVM *g_vm = NULL;
static jobject g_cb_obj = NULL; // Global ref to NativeSip.Callback

static jmethodID mid_onLog = NULL;
static jmethodID mid_onReg = NULL;
static jmethodID mid_onIncoming = NULL;
static jmethodID mid_onCallState = NULL;
static jmethodID mid_onCallMedia = NULL;

static pj_bool_t g_started = PJ_FALSE;
static pjsua_acc_id g_acc_id = PJSUA_INVALID_ID;

typedef struct call_ctx {
    pj_bool_t on_hold;
    pj_bool_t recording;
    pjsua_recorder_id rec_id;
    pjsua_conf_port_id rec_slot;
    pjsua_conf_port_id call_slot;
    int consult_call_id; // for warm transfer
} call_ctx;

static call_ctx g_calls[PJSUA_MAX_CALLS];

static JNIEnv *get_env(pj_bool_t *needs_detach) {
    *needs_detach = PJ_FALSE;
    if (!g_vm) return NULL;
    JNIEnv *env = NULL;
    jint res = (*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0) {
            return NULL;
        }
        *needs_detach = PJ_TRUE;
    } else if (res != JNI_OK) {
        return NULL;
    }
    return env;
}

static void detach_env_if_needed(pj_bool_t needs_detach) {
    if (needs_detach && g_vm) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}

static void cb_log(const char *msg) {
    if (!g_cb_obj || !mid_onLog) return;
    pj_bool_t needs_detach;
    JNIEnv *env = get_env(&needs_detach);
    if (!env) return;
    jstring jmsg = (*env)->NewStringUTF(env, msg ? msg : "");
    (*env)->CallVoidMethod(env, g_cb_obj, mid_onLog, jmsg);
    (*env)->DeleteLocalRef(env, jmsg);
    detach_env_if_needed(needs_detach);
}

static void cb_reg(int code, const char *reason) {
    if (!g_cb_obj || !mid_onReg) return;
    pj_bool_t needs_detach;
    JNIEnv *env = get_env(&needs_detach);
    if (!env) return;
    jstring jreason = (*env)->NewStringUTF(env, reason ? reason : "");
    (*env)->CallVoidMethod(env, g_cb_obj, mid_onReg, (jint) code, jreason);
    (*env)->DeleteLocalRef(env, jreason);
    detach_env_if_needed(needs_detach);
}

static void cb_incoming(int call_id, const char *remote_uri, const char *cname) {
    if (!g_cb_obj || !mid_onIncoming) return;
    pj_bool_t needs_detach;
    JNIEnv *env = get_env(&needs_detach);
    if (!env) return;
    jstring jremote = (*env)->NewStringUTF(env, remote_uri ? remote_uri : "");
    jstring jcname = (*env)->NewStringUTF(env, cname ? cname : "");
    (*env)->CallVoidMethod(env, g_cb_obj, mid_onIncoming, (jint) call_id, jremote, jcname);
    (*env)->DeleteLocalRef(env, jremote);
    (*env)->DeleteLocalRef(env, jcname);
    detach_env_if_needed(needs_detach);
}

static void cb_call_state(int call_id, int state, const char *state_text) {
    if (!g_cb_obj || !mid_onCallState) return;
    pj_bool_t needs_detach;
    JNIEnv *env = get_env(&needs_detach);
    if (!env) return;
    jstring jst = (*env)->NewStringUTF(env, state_text ? state_text : "");
    (*env)->CallVoidMethod(env, g_cb_obj, mid_onCallState, (jint) call_id, (jint) state, jst);
    (*env)->DeleteLocalRef(env, jst);
    detach_env_if_needed(needs_detach);
}

static void cb_call_media(int call_id, pj_bool_t active) {
    if (!g_cb_obj || !mid_onCallMedia) return;
    pj_bool_t needs_detach;
    JNIEnv *env = get_env(&needs_detach);
    if (!env) return;
    (*env)->CallVoidMethod(env, g_cb_obj, mid_onCallMedia, (jint) call_id, (jboolean) (active ? JNI_TRUE : JNI_FALSE));
    detach_env_if_needed(needs_detach);
}

static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id, pjsip_rx_data *rdata) {
    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);

    pjsua_call_info ci;
    pjsua_call_get_info(call_id, &ci);

    char remote[256];
    pj_ansi_strncpy(remote, ci.remote_info.ptr, sizeof(remote));
    remote[sizeof(remote) - 1] = '\0';

    char cname[128];
    cname[0] = '\0';
    if (ci.remote_contact.slen > 0) {
        // Often remote_info contains: "Name" <sip:...>
        // pjsua provides remote_info; use pjsip parsing if needed.
    }

    // Best-effort: use ci.remote_info prefix as "cname".
    // Extract between quotes if present.
    const char *ri = remote;
    const char *q1 = strchr(ri, '\"');
    const char *q2 = q1 ? strchr(q1 + 1, '\"') : NULL;
    if (q1 && q2 && q2 > q1 + 1) {
        size_t n = (size_t) (q2 - (q1 + 1));
        if (n >= sizeof(cname)) n = sizeof(cname) - 1;
        memcpy(cname, q1 + 1, n);
        cname[n] = '\0';
    } else {
        // fallback: everything before '<'
        const char *lt = strchr(ri, '<');
        if (lt && lt > ri) {
            size_t n = (size_t) (lt - ri);
            while (n > 0 && (ri[n - 1] == ' ')) n--;
            if (n >= sizeof(cname)) n = sizeof(cname) - 1;
            memcpy(cname, ri, n);
            cname[n] = '\0';
        }
    }

    cb_incoming(call_id, remote, cname);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event *e) {
    PJ_UNUSED_ARG(e);
    pjsua_call_info ci;
    pjsua_call_get_info(call_id, &ci);

    cb_call_state(call_id, (int) ci.state, ci.state_text.ptr);

    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
        call_ctx *ctx = &g_calls[call_id];
        if (ctx->recording) {
            if (ctx->call_slot != PJSUA_INVALID_ID && ctx->rec_slot != PJSUA_INVALID_ID) {
                pjsua_conf_disconnect(ctx->call_slot, ctx->rec_slot);
            }
            if (ctx->rec_id != PJSUA_INVALID_ID) {
                pjsua_recorder_destroy(ctx->rec_id);
            }
        }
        memset(ctx, 0, sizeof(*ctx));
        ctx->rec_id = PJSUA_INVALID_ID;
        ctx->rec_slot = PJSUA_INVALID_ID;
        ctx->call_slot = PJSUA_INVALID_ID;
        ctx->consult_call_id = -1;
        ctx->on_hold = PJ_FALSE;
        ctx->recording = PJ_FALSE;
    }
}

static void on_call_media_state(pjsua_call_id call_id) {
    pjsua_call_info ci;
    pjsua_call_get_info(call_id, &ci);

    pj_bool_t active = PJ_FALSE;
    for (unsigned i = 0; i < ci.media_cnt; i++) {
        if (ci.media[i].type == PJMEDIA_TYPE_AUDIO &&
            ci.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
            active = PJ_TRUE;
            pjsua_conf_connect(ci.conf_slot, 0);
            pjsua_conf_connect(0, ci.conf_slot);
            g_calls[call_id].call_slot = ci.conf_slot;
        }
    }
    cb_call_media(call_id, active);
}

static void on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info *info) {
    PJ_UNUSED_ARG(acc_id);
    if (!info) return;
    cb_reg((int) info->cbparam->code, info->cbparam->reason.ptr);
}

static void init_default_call_ctx(void) {
    for (int i = 0; i < PJSUA_MAX_CALLS; i++) {
        memset(&g_calls[i], 0, sizeof(g_calls[i]));
        g_calls[i].rec_id = PJSUA_INVALID_ID;
        g_calls[i].rec_slot = PJSUA_INVALID_ID;
        g_calls[i].call_slot = PJSUA_INVALID_ID;
        g_calls[i].consult_call_id = -1;
    }
}

static int ensure_started(const char *user_agent) {
    if (g_started) return 0;

    pj_status_t status;
    status = pjsua_create();
    if (status != PJ_SUCCESS) return -1;

    pjsua_config cfg;
    pjsua_config_default(&cfg);
    cfg.cb.on_incoming_call = &on_incoming_call;
    cfg.cb.on_call_state = &on_call_state;
    cfg.cb.on_call_media_state = &on_call_media_state;
    cfg.cb.on_reg_state2 = &on_reg_state2;

    if (user_agent && user_agent[0]) {
        pj_str_t ua = pj_str((char *) user_agent);
        cfg.user_agent = ua;
    }

    pjsua_logging_config log_cfg;
    pjsua_logging_config_default(&log_cfg);
    log_cfg.console_level = 4;
    log_cfg.level = 4;

    pjsua_media_config media_cfg;
    pjsua_media_config_default(&media_cfg);
    media_cfg.clock_rate = 16000;
    media_cfg.snd_clock_rate = 16000;

    status = pjsua_init(&cfg, &log_cfg, &media_cfg);
    if (status != PJ_SUCCESS) return -2;

    // Transport UDP (FreePBX typical). TCP/TLS can be added similarly.
    pjsua_transport_config tcfg;
    pjsua_transport_config_default(&tcfg);
    tcfg.port = 5060;
    status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &tcfg, NULL);
    if (status != PJ_SUCCESS) return -3;

    // Start
    status = pjsua_start();
    if (status != PJ_SUCCESS) return -4;

    init_default_call_ctx();
    g_started = PJ_TRUE;
    cb_log("PJSUA started");
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeInit(JNIEnv *env, jclass clazz, jobject cb, jstring userAgent) {
    PJ_UNUSED_ARG(clazz);

    if ((*env)->GetJavaVM(env, &g_vm) != 0) {
        return -100;
    }

    if (g_cb_obj) {
        (*env)->DeleteGlobalRef(env, g_cb_obj);
        g_cb_obj = NULL;
    }
    g_cb_obj = (*env)->NewGlobalRef(env, cb);

    jclass cbCls = (*env)->GetObjectClass(env, cb);
    mid_onLog = (*env)->GetMethodID(env, cbCls, "onLog", "(Ljava/lang/String;)V");
    mid_onReg = (*env)->GetMethodID(env, cbCls, "onRegistrationState", "(ILjava/lang/String;)V");
    mid_onIncoming = (*env)->GetMethodID(env, cbCls, "onIncomingCall", "(ILjava/lang/String;Ljava/lang/String;)V");
    mid_onCallState = (*env)->GetMethodID(env, cbCls, "onCallState", "(IILjava/lang/String;)V");
    mid_onCallMedia = (*env)->GetMethodID(env, cbCls, "onCallMediaState", "(IZ)V");

    const char *ua = NULL;
    if (userAgent) ua = (*env)->GetStringUTFChars(env, userAgent, NULL);
    int rc = ensure_started(ua);
    if (ua && userAgent) (*env)->ReleaseStringUTFChars(env, userAgent, ua);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeDestroy(JNIEnv *env, jclass clazz) {
    PJ_UNUSED_ARG(clazz);
    if (g_started) {
        pjsua_destroy();
        g_started = PJ_FALSE;
    }
    if (g_cb_obj) {
        (*env)->DeleteGlobalRef(env, g_cb_obj);
        g_cb_obj = NULL;
    }
    g_acc_id = PJSUA_INVALID_ID;
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeRegister(JNIEnv *env, jclass clazz, jstring domain, jstring ext, jstring password, jstring proxy) {
    PJ_UNUSED_ARG(clazz);
    if (!g_started) return -200;

    const char *cdomain = (*env)->GetStringUTFChars(env, domain, NULL);
    const char *cext = (*env)->GetStringUTFChars(env, ext, NULL);
    const char *cpass = (*env)->GetStringUTFChars(env, password, NULL);
    const char *cproxy = proxy ? (*env)->GetStringUTFChars(env, proxy, NULL) : NULL;

    char id_uri[256];
    char reg_uri[256];
    snprintf(id_uri, sizeof(id_uri), "sip:%s@%s", cext, cdomain);
    snprintf(reg_uri, sizeof(reg_uri), "sip:%s", cdomain);

    if (g_acc_id != PJSUA_INVALID_ID) {
        pjsua_acc_del(g_acc_id);
        g_acc_id = PJSUA_INVALID_ID;
    }

    pjsua_acc_config acc_cfg;
    pjsua_acc_config_default(&acc_cfg);
    acc_cfg.id = pj_str(id_uri);
    acc_cfg.reg_uri = pj_str(reg_uri);
    acc_cfg.cred_count = 1;
    acc_cfg.cred_info[0].realm = pj_str("*");
    acc_cfg.cred_info[0].scheme = pj_str("digest");
    acc_cfg.cred_info[0].username = pj_str((char *) cext);
    acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
    acc_cfg.cred_info[0].data = pj_str((char *) cpass);

    if (cproxy && cproxy[0]) {
        acc_cfg.proxy_cnt = 1;
        acc_cfg.proxy[0] = pj_str((char *) cproxy);
    }

    // Prefer codecs: G.729 (if available), PCMU, PCMA.
    // Note: G.729 requires a codec implementation and is not provided by default due to licensing.
    pjsua_codec_set_priority(&pj_str("G729/8000"), PJMEDIA_CODEC_PRIO_HIGHEST);
    pjsua_codec_set_priority(&pj_str("PCMU/8000"), PJMEDIA_CODEC_PRIO_NEXT_HIGHER);
    pjsua_codec_set_priority(&pj_str("PCMA/8000"), PJMEDIA_CODEC_PRIO_NEXT_HIGHER);

    pj_status_t st = pjsua_acc_add(&acc_cfg, PJ_TRUE, &g_acc_id);

    (*env)->ReleaseStringUTFChars(env, domain, cdomain);
    (*env)->ReleaseStringUTFChars(env, ext, cext);
    (*env)->ReleaseStringUTFChars(env, password, cpass);
    if (proxy && cproxy) (*env)->ReleaseStringUTFChars(env, proxy, cproxy);

    return (st == PJ_SUCCESS) ? 0 : -201;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeUnregister(JNIEnv *env, jclass clazz) {
    PJ_UNUSED_ARG(env);
    PJ_UNUSED_ARG(clazz);
    if (g_acc_id == PJSUA_INVALID_ID) return 0;
    pjsua_acc_del(g_acc_id);
    g_acc_id = PJSUA_INVALID_ID;
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeMakeCall(JNIEnv *env, jclass clazz, jstring dstUri) {
    PJ_UNUSED_ARG(clazz);
    if (g_acc_id == PJSUA_INVALID_ID) return -300;
    const char *cdst = (*env)->GetStringUTFChars(env, dstUri, NULL);
    pj_str_t uri = pj_str((char *) cdst);
    pjsua_call_id call_id = PJSUA_INVALID_ID;
    pj_status_t st = pjsua_call_make_call(g_acc_id, &uri, 0, NULL, NULL, &call_id);
    (*env)->ReleaseStringUTFChars(env, dstUri, cdst);
    return (st == PJ_SUCCESS) ? (jint) call_id : -301;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeAnswer(JNIEnv *env, jclass clazz, jint callId) {
    PJ_UNUSED_ARG(env);
    PJ_UNUSED_ARG(clazz);
    pj_status_t st = pjsua_call_answer((pjsua_call_id) callId, 200, NULL, NULL);
    return (st == PJ_SUCCESS) ? 0 : -400;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeHangup(JNIEnv *env, jclass clazz, jint callId) {
    PJ_UNUSED_ARG(env);
    PJ_UNUSED_ARG(clazz);
    pj_status_t st = pjsua_call_hangup((pjsua_call_id) callId, 0, NULL, NULL);
    return (st == PJ_SUCCESS) ? 0 : -401;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeHoldToggle(JNIEnv *env, jclass clazz, jint callId) {
    PJ_UNUSED_ARG(env);
    PJ_UNUSED_ARG(clazz);

    call_ctx *ctx = &g_calls[callId];
    pj_status_t st;
    if (!ctx->on_hold) {
        st = pjsua_call_set_hold((pjsua_call_id) callId, NULL);
        if (st == PJ_SUCCESS) ctx->on_hold = PJ_TRUE;
    } else {
        st = pjsua_call_reinvite((pjsua_call_id) callId, PJ_TRUE, NULL);
        if (st == PJ_SUCCESS) ctx->on_hold = PJ_FALSE;
    }
    return (st == PJ_SUCCESS) ? 0 : -500;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeBlindTransfer(JNIEnv *env, jclass clazz, jint callId, jstring dstUri) {
    PJ_UNUSED_ARG(clazz);
    const char *cdst = (*env)->GetStringUTFChars(env, dstUri, NULL);
    pj_str_t uri = pj_str((char *) cdst);
    pj_status_t st = pjsua_call_xfer((pjsua_call_id) callId, &uri, NULL);
    (*env)->ReleaseStringUTFChars(env, dstUri, cdst);
    return (st == PJ_SUCCESS) ? 0 : -600;
}

// Warm transfer strategy:
// 1) put original call on hold
// 2) make consult call to target
// 3) once consult is answered, do xfer-replaces from original to consult
JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeWarmTransfer(JNIEnv *env, jclass clazz, jint callId, jstring dstUri) {
    PJ_UNUSED_ARG(clazz);
    if (g_acc_id == PJSUA_INVALID_ID) return -700;

    call_ctx *ctx = &g_calls[callId];

    // Put original on hold
    pj_status_t st = pjsua_call_set_hold((pjsua_call_id) callId, NULL);
    if (st != PJ_SUCCESS) return -701;
    ctx->on_hold = PJ_TRUE;

    const char *cdst = (*env)->GetStringUTFChars(env, dstUri, NULL);
    pj_str_t uri = pj_str((char *) cdst);
    pjsua_call_id consult_id = PJSUA_INVALID_ID;
    st = pjsua_call_make_call(g_acc_id, &uri, 0, NULL, NULL, &consult_id);
    (*env)->ReleaseStringUTFChars(env, dstUri, cdst);
    if (st != PJ_SUCCESS) return -702;

    ctx->consult_call_id = (int) consult_id;

    // The app UI should wait until consult is answered, then invoke blind transfer if needed.
    // For a minimal demo, we immediately attempt xfer-replaces; servers may reject until confirmed.
    pjsua_call_info consult_ci;
    pjsua_call_get_info(consult_id, &consult_ci);

    pj_str_t replaces = consult_ci.call_id; // not correct header; pjsua has helper:
    // Use xfer_replaces API; it builds Replaces from consult call automatically.
    st = pjsua_call_xfer_replaces((pjsua_call_id) callId, consult_id, 0, NULL);
    return (st == PJ_SUCCESS) ? 0 : -703;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeStartRecording(JNIEnv *env, jclass clazz, jint callId, jstring wavPath) {
    PJ_UNUSED_ARG(clazz);
    const char *cpath = (*env)->GetStringUTFChars(env, wavPath, NULL);

    call_ctx *ctx = &g_calls[callId];
    if (ctx->recording) {
        (*env)->ReleaseStringUTFChars(env, wavPath, cpath);
        return 0;
    }

    pj_str_t path = pj_str((char *) cpath);
    pj_status_t st = pjsua_recorder_create(&path, 0, NULL, 0, 0, &ctx->rec_id);
    if (st != PJ_SUCCESS) {
        (*env)->ReleaseStringUTFChars(env, wavPath, cpath);
        return -800;
    }

    ctx->rec_slot = pjsua_recorder_get_conf_port(ctx->rec_id);

    pjsua_call_info ci;
    pjsua_call_get_info((pjsua_call_id) callId, &ci);
    ctx->call_slot = ci.conf_slot;

    // Record both directions by connecting call conference slot to recorder.
    st = pjsua_conf_connect(ctx->call_slot, ctx->rec_slot);
    if (st == PJ_SUCCESS) {
        ctx->recording = PJ_TRUE;
    }

    (*env)->ReleaseStringUTFChars(env, wavPath, cpath);
    return (st == PJ_SUCCESS) ? 0 : -801;
}

JNIEXPORT jint JNICALL
Java_com_softph_NativeSip_nativeStopRecording(JNIEnv *env, jclass clazz, jint callId) {
    PJ_UNUSED_ARG(env);
    PJ_UNUSED_ARG(clazz);
    call_ctx *ctx = &g_calls[callId];
    if (!ctx->recording) return 0;

    if (ctx->call_slot != PJSUA_INVALID_ID && ctx->rec_slot != PJSUA_INVALID_ID) {
        pjsua_conf_disconnect(ctx->call_slot, ctx->rec_slot);
    }
    if (ctx->rec_id != PJSUA_INVALID_ID) {
        pjsua_recorder_destroy(ctx->rec_id);
    }

    ctx->rec_id = PJSUA_INVALID_ID;
    ctx->rec_slot = PJSUA_INVALID_ID;
    ctx->recording = PJ_FALSE;
    return 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    PJ_UNUSED_ARG(reserved);
    g_vm = vm;
    return JNI_VERSION_1_6;
}

