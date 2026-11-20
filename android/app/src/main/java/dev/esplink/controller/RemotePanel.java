package dev.esplink.controller;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.Dialog;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.view.ViewGroup;
import org.json.JSONObject;

final class RemotePanel extends Dialog {
    private final Activity activity;
    private final WiFiLink wifi;
    private WebView web;

    @SuppressLint("SetJavaScriptEnabled") // Only local bundled assets; requests go through the fixed native routes.
    RemotePanel(Activity activity, WiFiLink wifi) {
        super(activity, android.R.style.Theme_Material_NoActionBar);
        this.activity = activity;
        this.wifi = wifi;
        web = new WebView(activity);
        web.setBackgroundColor(Color.rgb(9, 14, 19));
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(false);
        web.getSettings().setBlockNetworkLoads(true);
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) { return true; }
        });
        web.addJavascriptInterface(this, "RemoteNative");
        setContentView(web);
        web.loadUrl("file:///android_asset/remote.html");
    }

    @JavascriptInterface
    public void send(int id, String op, String body) {
        activity.runOnUiThread(() -> {
            if (web == null || !isShowing() || id < 1 || body == null) return;
            if ("back".equals(op)) { dismiss(); return; }
            if ("layout".equals(op) || "save".equals(op)) {
                SharedPreferences prefs = activity.getSharedPreferences("remote-panel", Activity.MODE_PRIVATE);
                try {
                    JSONObject data = new JSONObject();
                    if ("layout".equals(op)) data.put("layout", prefs.getString("layout", ""));
                    else if (body.length() <= 160) prefs.edit().putString("layout", body).apply();
                    else { reply(id, 400, data); return; }
                    reply(id, 200, data);
                } catch (org.json.JSONException error) { reply(id, 400, new JSONObject()); }
                return;
            }
            if (op != null) wifi.remote(op, body, (code, data) -> reply(id, code, data));
        });
    }

    private void reply(int id, int code, JSONObject data) {
        if (web != null) {
            // JSON-quote the whole data string; board replies are parsed as data, never JavaScript.
            web.evaluateJavascript("window.espReply(" + id + ",{code:" + code + ",data:JSON.parse("
                    + JSONObject.quote(data.toString()) + ")});", null);
        }
    }

    @Override protected void onStart() {
        super.onStart();
        if (getWindow() != null) getWindow().setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
    }

    @Override protected void onStop() {
        if (web != null) {
            wifi.remote("stop", "", (code, data) -> {});
            web.removeJavascriptInterface("RemoteNative");
            if (web.getParent() instanceof ViewGroup) ((ViewGroup) web.getParent()).removeView(web);
            web.destroy();
            web = null;
        }
        super.onStop();
    }
}
