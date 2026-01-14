package com.softph;

public final class NativeSip {
    static {
        System.loadLibrary("native-lib");
    }

    public interface Callback {
        void onLog(String msg);
        void onRegistrationState(int statusCode, String reason);
        void onIncomingCall(int callId, String remoteUri, String cname);
        void onCallState(int callId, int state, String stateText);
        void onCallMediaState(int callId, boolean active);
    }

    public static native int nativeInit(Callback cb, String userAgent);
    public static native int nativeDestroy();

    public static native int nativeRegister(String domain, String ext, String password, String proxy);
    public static native int nativeUnregister();

    public static native int nativeMakeCall(String dstUri);
    public static native int nativeAnswer(int callId);
    public static native int nativeHangup(int callId);

    public static native int nativeHoldToggle(int callId);
    public static native int nativeBlindTransfer(int callId, String dstUri);
    public static native int nativeWarmTransfer(int callId, String dstUri);

    public static native int nativeStartRecording(int callId, String wavPath);
    public static native int nativeStopRecording(int callId);
}

