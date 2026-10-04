// DeviceServices.java - what the device has switched on, and its settings screens.
//
// Called from AndroidUi.cpp through JNI: whether location is enabled on the
// device (the permission may be granted while the service is off), and the
// system's location settings screen, offered when it is not.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

package org.ax25chess;

import android.content.Context;
import android.content.Intent;
import android.location.LocationManager;
import android.net.Uri;
import android.os.Build;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;

public class DeviceServices {
    private static final String TAG = "AX25Chess.DeviceServices";
    private static Context context;

    public static synchronized void setContext(Context c) {
        context = c;
    }

    public static synchronized boolean locationEnabled() {
        if (context == null) return true;
        try {
            LocationManager lm = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) return true;
            if (Build.VERSION.SDK_INT >= 28) return lm.isLocationEnabled();
            return lm.isProviderEnabled(LocationManager.GPS_PROVIDER) || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
        } catch (Exception e) {
            Log.e(TAG, "locationEnabled: " + e);
            return true;
        }
    }

    // Doze: with the screen off and the phone still, Android suspends the
    // network for every application that is not on its exemption list, a
    // foreground service and wake locks notwithstanding.  The user has to
    // grant the exemption through the system's own dialog.
    public static synchronized boolean ignoringBatteryOptimizations() {
        if (context == null || Build.VERSION.SDK_INT < 23) return true;
        try {
            PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            return pm == null || pm.isIgnoringBatteryOptimizations(context.getPackageName());
        } catch (Exception e) {
            Log.e(TAG, "ignoringBatteryOptimizations: " + e);
            return true;
        }
    }

    public static synchronized void requestIgnoreBatteryOptimizations() {
        if (context == null || Build.VERSION.SDK_INT < 23) return;
        try {
            Intent intent = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            intent.setData(Uri.parse("package:" + context.getPackageName()));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
        } catch (Exception e) {
            // Some builds of Android hide the direct request; the list is the fallback.
            Log.e(TAG, "requestIgnoreBatteryOptimizations: " + e);
            try {
                Intent intent = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(intent);
            } catch (Exception e2) {
                Log.e(TAG, "battery optimisation settings: " + e2);
            }
        }
    }

    public static synchronized void openLocationSettings() {
        if (context == null) return;
        try {
            Intent intent = new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);
        } catch (Exception e) {
            Log.e(TAG, "openLocationSettings: " + e);
        }
    }
}
