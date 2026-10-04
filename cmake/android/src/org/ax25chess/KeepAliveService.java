// KeepAliveService.java - keeps the application alive in the background.
//
// A foreground service in the application's own process: its notification
// is what lets Android keep the process (and so the modem's audio threads,
// listening for the correspondent's moves) running while the application
// is not on screen.  Taken from AX25Chat, where it was proven on a phone;
// AX25Chess needs no location, so that type is gone.
// It does nothing itself. Started from AndroidUi.cpp once the
// permissions are settled, stopped when the application quits.
//
// The service type matters since Android 14: "microphone" keeps the
// microphone usable in the background (the modem records), "location" the
// GPS, and each needs its runtime permission granted at the moment the
// service starts; "dataSync" needs nothing but Android 15 limits it to six
// hours a day. The types are chosen from the permissions actually granted.
// A partial wake lock keeps the CPU awake for the demodulator.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

package org.ax25chess;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

public class KeepAliveService extends Service {
    private static final String TAG = "AX25Chess.KeepAlive";
    private static final String CHANNEL_ID = "ax25chess.running";
    private static final int NOTIFICATION_ID = 2;
    private static boolean running;
    private static String lastError = "";
    private static int runningTypes;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;

    // ---- entry points from C++ (through the application context)
    public static synchronized void start(Context context) {
        if (context == null || running) return;
        try {
            Intent intent = new Intent(context, KeepAliveService.class);
            if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(intent);
            else context.startService(intent);
        } catch (Exception e) {
            Log.e(TAG, "start: " + e);
        }
    }

    public static synchronized void stop(Context context) {
        if (context == null) return;
        try {
            context.stopService(new Intent(context, KeepAliveService.class));
        } catch (Exception e) {
            Log.e(TAG, "stop: " + e);
        }
    }

    public static synchronized boolean isRunning() {
        return running;
    }

    // What happened at the last start attempt, for the application's log.
    public static synchronized String status() {
        if (running) return "running, types " + runningTypes;
        return lastError.isEmpty() ? "not running" : "failed: " + lastError;
    }

    // ---- the service itself
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        try {
            Notification notification = buildNotification();
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIFICATION_ID, notification, serviceTypes());
            } else {
                startForeground(NOTIFICATION_ID, notification);
            }
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null && wakeLock == null) {
                wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AX25Chess:modem");
                wakeLock.acquire();
            }
            // Kept from AX25Chat, where Wi-Fi went to sleep with the screen and
            // took a socket with it; harmless here (deprecated
            // since Android 10, still the only lock that does the job - the
            // same one FT891Remote holds for its audio stream).
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm != null && wifiLock == null) {
                wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "AX25Chess:link");
                wifiLock.acquire();
            }
            synchronized (KeepAliveService.class) { running = true; runningTypes = serviceTypes(); lastError = ""; }
            Log.i(TAG, "foreground service running, types " + serviceTypes());
        } catch (Exception e) {
            Log.e(TAG, "startForeground: " + e);
            synchronized (KeepAliveService.class) { running = false; lastError = e.toString(); }
            stopSelf();
        }
        return START_STICKY;
    }

    // The user swiped the application away from the recent tasks: that is
    // a quit.  The service would keep the process alive (that is its job),
    // but a Qt application cannot be entered a second time in the same
    // process, so the next launch would hang on the splash screen.  Stop
    // the service and end the process; the next launch starts clean.
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        Log.i(TAG, "task removed: shutting the process down");
        try { stopForeground(true); } catch (Exception ignored) {}
        stopSelf();
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(
                () -> android.os.Process.killProcess(android.os.Process.myPid()), 300);
    }

    @Override
    public void onDestroy() {
        synchronized (KeepAliveService.class) { running = false; }
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        wakeLock = null;
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        wifiLock = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private boolean granted(String permission) {
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private int serviceTypes() {
        int types = 0;
        if (Build.VERSION.SDK_INT >= 30 && granted("android.permission.RECORD_AUDIO")) {
            types |= ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE;
        }
        if (types == 0 && Build.VERSION.SDK_INT >= 34) {
            // No microphone permission: "special use", as APRSdroid declares it,
            // which Android 15 does not cap at six hours like dataSync.
            types = ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE;
        } else if (types == 0 && Build.VERSION.SDK_INT >= 29) {
            types = ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC;
        }
        return types;
    }

    private Notification buildNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && manager != null) {
            NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Running in the background", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("Shown while the modem keeps listening for your correspondent");
            manager.createNotificationChannel(channel);
        }
        int icon = getResources().getIdentifier("icon", "drawable", getPackageName());
        if (icon == 0) icon = android.R.drawable.ic_dialog_info;
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent open = null;
        if (launch != null) {
            launch.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            open = PendingIntent.getActivity(this, 1, launch, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        }
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        builder.setSmallIcon(icon)
               .setContentTitle("AX25Chess is listening")
               .setContentText("The modem keeps receiving moves and messages with the screen off")
               .setOngoing(true)
               .setOnlyAlertOnce(true);
        if (open != null) builder.setContentIntent(open);
        return builder.build();
    }
}
