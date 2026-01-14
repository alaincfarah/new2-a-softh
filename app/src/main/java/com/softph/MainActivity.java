package com.softph;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.text.TextUtils;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.softph.databinding.ActivityMainBinding;

public final class MainActivity extends AppCompatActivity {
    private ActivityMainBinding vb;

    private final ActivityResultLauncher<String> micPermissionLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestPermission(), granted -> {
                if (!granted) {
                    Toast.makeText(this, "Microphone permission is required for calls", Toast.LENGTH_LONG).show();
                }
            });

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        vb = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(vb.getRoot());

        ensureServiceRunning();
        ensureMicPermission();

        vb.btnRegister.setOnClickListener(v -> {
            String domain = str(vb.etDomain);
            String ext = str(vb.etExt);
            String pass = str(vb.etPass);
            String urlTemplate = str(vb.etUrlTemplate);

            if (TextUtils.isEmpty(domain) || TextUtils.isEmpty(ext) || TextUtils.isEmpty(pass)) {
                Toast.makeText(this, "Domain/Extension/Password required", Toast.LENGTH_LONG).show();
                return;
            }

            // Proxy is optional; common FreePBX default is same as domain.
            String proxy = "sip:" + domain + ";transport=udp";

            Intent i = new Intent(this, SipService.class);
            i.setAction(SipService.ACTION_REGISTER);
            i.putExtra("domain", domain);
            i.putExtra("ext", ext);
            i.putExtra("pass", pass);
            i.putExtra("proxy", proxy);
            i.putExtra("incoming_url_template", urlTemplate);
            startService(i);
            Toast.makeText(this, "Register requested", Toast.LENGTH_SHORT).show();
        });

        vb.btnCall.setOnClickListener(v -> {
            String domain = str(vb.etDomain);
            String dst = str(vb.etDial);
            if (TextUtils.isEmpty(domain) || TextUtils.isEmpty(dst)) {
                Toast.makeText(this, "Domain and destination required", Toast.LENGTH_LONG).show();
                return;
            }
            String uri = dst.contains(":") ? dst : ("sip:" + dst + "@" + domain);
            Intent i = new Intent(this, SipService.class);
            i.setAction(SipService.ACTION_MAKE_CALL);
            i.putExtra("dst", uri);
            startService(i);
        });
    }

    private void ensureServiceRunning() {
        Intent i = new Intent(this, SipService.class);
        startForegroundService(i);
    }

    private void ensureMicPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            micPermissionLauncher.launch(Manifest.permission.RECORD_AUDIO);
        }
    }

    private static String str(android.widget.EditText et) {
        return et.getText() == null ? "" : et.getText().toString().trim();
    }
}

