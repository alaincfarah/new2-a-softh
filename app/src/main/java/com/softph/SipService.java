package com.softph;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import java.io.File;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;

public final class SipService extends Service implements NativeSip.Callback {
    private static final String TAG = "SipService";
    private static final String CHANNEL_ID = "softph_sip";
    private static final int NOTIF_ID = 1;

    public static final String ACTION_REGISTER = "com.softph.action.REGISTER";
    public static final String ACTION_MAKE_CALL = "com.softph.action.MAKE_CALL";

    public static final String EXTRA_CALL_ID = "call_id";
    public static final String EXTRA_REMOTE_URI = "remote_uri";
    public static final String EXTRA_CNAME = "cname";

    private volatile String incomingUrlTemplate = "https://example.local/incoming?cname={cname}";

    @Override
    public void onCreate() {
        super.onCreate();
        ensureChannel();
        startForeground(NOTIF_ID, buildNotification("Starting SIP engine…"));
        int rc = NativeSip.nativeInit(this, "Softph/1.0 (Android)");
        Log.i(TAG, "nativeInit rc=" + rc);
        updateForeground("SIP ready (not registered)");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            if (intent.hasExtra("incoming_url_template")) {
                incomingUrlTemplate = intent.getStringExtra("incoming_url_template");
            }
            String action = intent.getAction();
            if (ACTION_REGISTER.equals(action)) {
                String domain = intent.getStringExtra("domain");
                String ext = intent.getStringExtra("ext");
                String pass = intent.getStringExtra("pass");
                String proxy = intent.getStringExtra("proxy");
                String urlTemplate = intent.getStringExtra("incoming_url_template");
                if (urlTemplate != null) incomingUrlTemplate = urlTemplate;
                if (domain != null && ext != null && pass != null) {
                    updateForeground("Registering " + ext + "@" + domain + "…");
                    int rc = NativeSip.nativeRegister(domain, ext, pass, proxy);
                    Log.i(TAG, "nativeRegister rc=" + rc);
                }
            } else if (ACTION_MAKE_CALL.equals(action)) {
                String dst = intent.getStringExtra("dst");
                if (dst != null && !dst.isEmpty()) {
                    int rc = NativeSip.nativeMakeCall(dst);
                    Log.i(TAG, "nativeMakeCall rc=" + rc);
                }
            }
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        NativeSip.nativeDestroy();
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    public int register(String domain, String ext, String password, String proxy, String urlTemplate) {
        incomingUrlTemplate = urlTemplate;
        updateForeground("Registering " + ext + "@" + domain + "…");
        return NativeSip.nativeRegister(domain, ext, password, proxy);
    }

    public int makeCall(String dstUri) {
        return NativeSip.nativeMakeCall(dstUri);
    }

    public int answer(int callId) {
        return NativeSip.nativeAnswer(callId);
    }

    public int hangup(int callId) {
        return NativeSip.nativeHangup(callId);
    }

    public int holdToggle(int callId) {
        return NativeSip.nativeHoldToggle(callId);
    }

    public int blindTransfer(int callId, String dstUri) {
        return NativeSip.nativeBlindTransfer(callId, dstUri);
    }

    public int warmTransfer(int callId, String dstUri) {
        return NativeSip.nativeWarmTransfer(callId, dstUri);
    }

    public int startRecording(int callId) {
        File dir = new File(getExternalFilesDir(null), "recordings");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        String name = "call_" + callId + "_" + System.currentTimeMillis() + ".wav";
        File out = new File(dir, name);
        return NativeSip.nativeStartRecording(callId, out.getAbsolutePath());
    }

    public int stopRecording(int callId) {
        return NativeSip.nativeStopRecording(callId);
    }

    @Override
    public void onLog(String msg) {
        Log.i(TAG, msg);
    }

    @Override
    public void onRegistrationState(int statusCode, String reason) {
        updateForeground("Registration: " + statusCode + " " + reason);
    }

    @Override
    public void onIncomingCall(int callId, String remoteUri, String cname) {
        Log.i(TAG, "Incoming call: callId=" + callId + " remote=" + remoteUri + " cname=" + cname);
        updateForeground("Incoming call from " + (cname == null ? "" : cname));

        // Critical feature: open URL based on CNAME.
        String url = buildIncomingUrl(cname);
        if (url != null) {
            try {
                Intent view = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                view.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(view);
            } catch (Throwable t) {
                Log.e(TAG, "Failed to open URL: " + url, t);
            }
        }

        // Bring up the in-call UI.
        Intent callUi = new Intent(this, CallActivity.class);
        callUi.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        callUi.putExtra(EXTRA_CALL_ID, callId);
        callUi.putExtra(EXTRA_REMOTE_URI, remoteUri);
        callUi.putExtra(EXTRA_CNAME, cname);
        startActivity(callUi);
    }

    @Override
    public void onCallState(int callId, int state, String stateText) {
        Log.i(TAG, "Call state: " + callId + " state=" + state + " " + stateText);
        updateForeground("Call " + callId + ": " + stateText);
    }

    @Override
    public void onCallMediaState(int callId, boolean active) {
        Log.i(TAG, "Call media: " + callId + " active=" + active);
    }

    private String buildIncomingUrl(String cname) {
        if (cname == null) cname = "";
        String trimmed = cname.trim();

        // If CNAME itself looks like a URL, use it.
        if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
            return trimmed;
        }

        // Otherwise use the template with URL-encoded cname.
        String enc = URLEncoder.encode(trimmed, StandardCharsets.UTF_8);
        if (incomingUrlTemplate == null || incomingUrlTemplate.isEmpty()) return null;
        return incomingUrlTemplate.replace("{cname}", enc);
    }

    private void ensureChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel chan = new NotificationChannel(
                    CHANNEL_ID,
                    "Softphone SIP",
                    NotificationManager.IMPORTANCE_LOW
            );
            NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            nm.createNotificationChannel(chan);
        }
    }

    private Notification buildNotification(String text) {
        Intent i = new Intent(this, MainActivity.class);
        PendingIntent pi = PendingIntent.getActivity(
                this, 0, i,
                Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0
        );

        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.sym_call_incoming)
                .setContentTitle("Softphone")
                .setContentText(text)
                .setContentIntent(pi)
                .setOngoing(true)
                .build();
    }

    private void updateForeground(String text) {
        Notification n = buildNotification(text);
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        nm.notify(NOTIF_ID, n);
    }
}

