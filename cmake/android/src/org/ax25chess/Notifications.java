// Notifications.java - sounds and system notifications on Android.
//
// Called from AndroidNotify.cpp through JNI. The alert sound is the phone's
// own default notification sound (a plain tone if it has none), so "Test
// sound" and the alerts work on a fresh installation and sound like every
// other application's; a system notification is posted
// when a message arrives while the application is not in front, on a
// channel of its own so the user can tune it in the system settings.
// Android 13 and later require the POST_NOTIFICATIONS permission, asked
// for at start-up (requestPermission below).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

package org.ax25chess;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.media.ToneGenerator;
import android.net.Uri;
import android.os.Build;
import android.util.Log;

public class Notifications {
    private static final String TAG = "AX25Chess.Notifications";
    private static final String CHANNEL_ID = "ax25chess.traffic";
    private static final int PERMISSION_REQUEST = 4225;

    private static Context context;
    private static int nextId = 1;
    private static boolean channelReady;

    public static synchronized void setContext(Context c) {
        context = c;
    }

    // The phone's own default notification sound, the one every other
    // application uses, on the notification stream; a plain tone when the
    // device has none to offer.
    public static synchronized void beep() {
        try {
            if (context != null) {
                Uri uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION);
                Ringtone ringtone = uri == null ? null : RingtoneManager.getRingtone(context, uri);
                if (ringtone != null) {
                    ringtone.setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_NOTIFICATION)
                            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                            .build());
                    ringtone.play();
                    return;
                }
            }
            ToneGenerator tone = new ToneGenerator(AudioManager.STREAM_NOTIFICATION, 90);
            tone.startTone(ToneGenerator.TONE_PROP_BEEP2, 250);
            new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(tone::release, 600);
        } catch (Exception e) {
            Log.e(TAG, "notification sound failed: " + e);
        }
    }

    // True when notifications may be posted (always before Android 13).
    public static synchronized boolean allowed() {
        if (context == null) return false;
        if (Build.VERSION.SDK_INT < 33) return true;
        return context.checkSelfPermission("android.permission.POST_NOTIFICATIONS") == PackageManager.PERMISSION_GRANTED;
    }

    // Ask for POST_NOTIFICATIONS (Android 13+). The answer arrives through
    // the activity; nothing waits for it, the next notification simply
    // gets posted if it was granted.
    public static synchronized void requestPermission() {
        if (context == null || Build.VERSION.SDK_INT < 33 || allowed()) return;
        if (context instanceof Activity) {
            ((Activity) context).requestPermissions(new String[] {"android.permission.POST_NOTIFICATIONS"}, PERMISSION_REQUEST);
        }
    }

    public static synchronized void show(String title, String text) {
        if (context == null || !allowed()) return;
        try {
            NotificationManager manager = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
            if (manager == null) return;
            if (Build.VERSION.SDK_INT >= 26 && !channelReady) {
                NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Moves and messages", NotificationManager.IMPORTANCE_DEFAULT);
                channel.setDescription("Your correspondent's moves, messages and draw offers received on the air");
                manager.createNotificationChannel(channel);
                channelReady = true;
            }
            int icon = context.getResources().getIdentifier("icon", "drawable", context.getPackageName());
            if (icon == 0) icon = android.R.drawable.ic_dialog_info;
            Intent launch = context.getPackageManager().getLaunchIntentForPackage(context.getPackageName());
            PendingIntent open = null;
            if (launch != null) {
                launch.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
                open = PendingIntent.getActivity(context, 0, launch, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            }
            Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                    ? new Notification.Builder(context, CHANNEL_ID)
                    : new Notification.Builder(context);
            builder.setSmallIcon(icon)
                   .setContentTitle(title)
                   .setContentText(text)
                   .setAutoCancel(true)
                   .setOnlyAlertOnce(false);
            if (open != null) builder.setContentIntent(open);
            manager.notify(nextId++, builder.build());
        } catch (Exception e) {
            Log.e(TAG, "notification failed: " + e);
        }
    }
}
