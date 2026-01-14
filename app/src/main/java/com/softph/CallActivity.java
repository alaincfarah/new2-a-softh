package com.softph;

import android.os.Bundle;
import android.text.TextUtils;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.softph.databinding.ActivityCallBinding;

public final class CallActivity extends AppCompatActivity {
    private ActivityCallBinding vb;
    private int callId = -1;

    private boolean recording = false;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        vb = ActivityCallBinding.inflate(getLayoutInflater());
        setContentView(vb.getRoot());

        callId = getIntent().getIntExtra(SipService.EXTRA_CALL_ID, -1);
        String remote = getIntent().getStringExtra(SipService.EXTRA_REMOTE_URI);
        String cname = getIntent().getStringExtra(SipService.EXTRA_CNAME);

        vb.tvCallInfo.setText("Call " + callId + "\n" + (cname == null ? "" : cname) + "\n" + (remote == null ? "" : remote));

        vb.btnAnswer.setOnClickListener(v -> {
            if (callId < 0) return;
            int rc = NativeSip.nativeAnswer(callId);
            toast("Answer rc=" + rc);
        });

        vb.btnHangup.setOnClickListener(v -> {
            if (callId < 0) return;
            int rc = NativeSip.nativeHangup(callId);
            toast("Hangup rc=" + rc);
            finish();
        });

        vb.btnHold.setOnClickListener(v -> {
            if (callId < 0) return;
            int rc = NativeSip.nativeHoldToggle(callId);
            toast("Hold toggle rc=" + rc);
        });

        vb.btnBlindTransfer.setOnClickListener(v -> {
            if (callId < 0) return;
            String dst = vb.etTransferTo.getText() == null ? "" : vb.etTransferTo.getText().toString().trim();
            if (TextUtils.isEmpty(dst)) {
                toast("Enter transfer target");
                return;
            }
            int rc = NativeSip.nativeBlindTransfer(callId, dst);
            toast("Blind transfer rc=" + rc);
        });

        vb.btnWarmTransfer.setOnClickListener(v -> {
            if (callId < 0) return;
            String dst = vb.etTransferTo.getText() == null ? "" : vb.etTransferTo.getText().toString().trim();
            if (TextUtils.isEmpty(dst)) {
                toast("Enter transfer target");
                return;
            }
            int rc = NativeSip.nativeWarmTransfer(callId, dst);
            toast("Warm transfer rc=" + rc);
        });

        vb.btnRecord.setOnClickListener(v -> {
            if (callId < 0) return;
            int rc;
            if (!recording) {
                rc = NativeSip.nativeStartRecording(callId, getExternalFilesDir(null) + "/recordings/call_" + callId + "_" + System.currentTimeMillis() + ".wav");
                recording = (rc == 0);
            } else {
                rc = NativeSip.nativeStopRecording(callId);
                recording = false;
            }
            toast("Record rc=" + rc);
        });
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }
}

