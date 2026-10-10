// UsbPtt.java - keying a transmitter from a USB device on Android.
//
// Called from AndroidPtt.cpp through JNI, on the modem's transmit thread.
// Two kinds of device are supported, both without a driver:
//
//   - a CM108/CM119 USB sound card with a GPIO wired to PTT (Digirig and
//     most "USB sound card" interfaces): a 4-byte HID output report on the
//     card's HID interface, the same report Dire Wolf's cm108.c writes;
//   - a USB serial adapter whose RTS or DTR line keys the radio: the
//     control request of the adapter's chip (CDC-ACM, FTDI, Silicon Labs
//     CP210x, WCH CH34x, Prolific PL2303).
//
// Android asks the user for permission per device; the first attempt asks
// and reports failure, the answer is received here and the device opened on
// the spot, so the next transmission works. The application context comes
// from AndroidPtt.cpp (setContext) through Qt's public native interface.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

package org.ax25chess;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.util.Log;

import java.util.HashMap;

public class UsbPtt {
    private static final String TAG = "AX25Chess.UsbPtt";
    private static final String ACTION_PERMISSION = "org.ax25chess.USB_PERMISSION";

    private static final int METHOD_SERIAL = 0;
    private static final int METHOD_CM108 = 1;

    private static final int CHIP_CDC = 1, CHIP_FTDI = 2, CHIP_CP210X = 3, CHIP_CH34X = 4, CHIP_PL2303 = 5;

    private static Context context;
    private static UsbManager manager;
    private static UsbDevice device;
    private static UsbDeviceConnection connection;
    private static UsbInterface iface;
    private static int method;
    private static int gpio;
    private static int line;
    private static int line2;
    private static int chip;
    private static boolean receiverRegistered;

    // ---- entry points from C++ ------------------------------------------------

    // Called once at start with the application context (AndroidPtt.cpp).
    public static synchronized void setContext(Context c) {
        context = c;
    }

    public static synchronized boolean open(int chan, int wantedMethod, int wantedGpio, int wantedLine, int wantedLine2) {
        if (!init()) return false;
        method = wantedMethod;
        gpio = wantedGpio;
        line = wantedLine;
        line2 = wantedLine2;
        UsbDevice candidate = find(method);
        if (candidate == null) {
            Log.w(TAG, "No suitable USB device for PTT method " + method);
            return false;
        }
        if (!manager.hasPermission(candidate)) {
            Log.i(TAG, "Asking for permission on " + candidate.getDeviceName());
            // Android 14 refuses a mutable pending intent that is implicit; naming
            // our own package makes it explicit enough.
            int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
            Intent intent = new Intent(ACTION_PERMISSION).setPackage(context.getPackageName());
            PendingIntent pi = PendingIntent.getBroadcast(context, 0, intent, flags);
            manager.requestPermission(candidate, pi);
            return false;
        }
        return connect(candidate);
    }

    public static synchronized void set(int chan, boolean on) {
        if (connection == null) return;
        try {
            if (method == METHOD_CM108) setGpio(on);
            else setModemLines(on);
        } catch (Exception e) {
            Log.e(TAG, "PTT failed: " + e);
        }
    }

    public static synchronized void close() {
        if (connection != null) {
            try { set(0, false); } catch (Exception ignored) {}
            if (iface != null) connection.releaseInterface(iface);
            connection.close();
        }
        connection = null;
        iface = null;
        device = null;
    }

    // What is plugged in, one line per device, for a settings page.
    public static synchronized String devices() {
        if (!init()) return "";
        StringBuilder sb = new StringBuilder();
        for (UsbDevice d : manager.getDeviceList().values()) {
            sb.append(String.format("%04x:%04x %s%s\n", d.getVendorId(), d.getProductId(),
                    d.getProductName() == null ? "" : d.getProductName(),
                    manager.hasPermission(d) ? "" : " (no permission yet)"));
        }
        return sb.toString();
    }

    // ---- internals -------------------------------------------------------------

    private static boolean init() {
        if (context == null) {
            // setContext() is called from AndroidPtt.cpp at start-up; Qt's own
            // context holder is not public, so there is no fallback.
            Log.e(TAG, "No application context: setContext() was not called");
            return false;
        }
        if (manager == null) manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (!receiverRegistered) {
            IntentFilter filter = new IntentFilter(ACTION_PERMISSION);
            filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
            if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED);
            else context.registerReceiver(receiver, filter);
            receiverRegistered = true;
        }
        return manager != null;
    }

    private static final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent intent) {
            String action = intent.getAction();
            UsbDevice d = Build.VERSION.SDK_INT >= 33
                    ? intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class)
                    : intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (ACTION_PERMISSION.equals(action)) {
                boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                Log.i(TAG, "Permission " + (granted ? "granted" : "denied") + " for " + (d == null ? "?" : d.getDeviceName()));
                if (granted && d != null) synchronized (UsbPtt.class) { connect(d); }
            } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                if (d != null && device != null && d.getDeviceId() == device.getDeviceId()) {
                    Log.i(TAG, "PTT device unplugged");
                    close();
                }
            }
        }
    };

    private static UsbDevice find(int wanted) {
        for (UsbDevice d : manager.getDeviceList().values()) {
            if (wanted == METHOD_CM108 && hidInterface(d) != null && looksLikeSoundCard(d)) return d;
            if (wanted == METHOD_SERIAL && chipOf(d) != 0) return d;
        }
        return null;
    }

    private static boolean looksLikeSoundCard(UsbDevice d) {
        if (d.getVendorId() == 0x0d8c) return true;         // C-Media
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            if (d.getInterface(i).getInterfaceClass() == UsbConstants.USB_CLASS_AUDIO) return true;
        }
        return false;
    }

    private static UsbInterface hidInterface(UsbDevice d) {
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface u = d.getInterface(i);
            if (u.getInterfaceClass() == UsbConstants.USB_CLASS_HID) return u;
        }
        return null;
    }

    private static int chipOf(UsbDevice d) {
        int vid = d.getVendorId(), pid = d.getProductId();
        if (vid == 0x0403) return CHIP_FTDI;                                   // FTDI FT232, FT2232...
        if (vid == 0x10c4 && (pid == 0xea60 || pid == 0xea70 || pid == 0xea71)) return CHIP_CP210X;
        if (vid == 0x1a86 && (pid == 0x7523 || pid == 0x5523 || pid == 0x55d4)) return CHIP_CH34X;
        if (vid == 0x067b) return CHIP_PL2303;
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface u = d.getInterface(i);
            if (u.getInterfaceClass() == UsbConstants.USB_CLASS_COMM && u.getInterfaceSubclass() == 2) return CHIP_CDC;
        }
        return 0;
    }

    private static boolean connect(UsbDevice d) {
        close();
        UsbDeviceConnection c = manager.openDevice(d);
        if (c == null) {
            Log.e(TAG, "openDevice failed for " + d.getDeviceName());
            return false;
        }
        UsbInterface u;
        if (method == METHOD_CM108) {
            u = hidInterface(d);
            chip = 0;
        } else {
            chip = chipOf(d);
            u = null;
            for (int i = 0; i < d.getInterfaceCount(); i++) {
                UsbInterface x = d.getInterface(i);
                if (chip == CHIP_CDC) {
                    if (x.getInterfaceClass() == UsbConstants.USB_CLASS_COMM) { u = x; break; }
                } else if (u == null) {
                    u = x;   // vendor chips: the first interface is the port
                }
            }
        }
        if (u == null) {
            c.close();
            return false;
        }
        if (!c.claimInterface(u, true)) {
            Log.e(TAG, "claimInterface failed");
            c.close();
            return false;
        }
        device = d;
        connection = c;
        iface = u;
        Log.i(TAG, "PTT device ready: " + d.getDeviceName() + " method " + method + " chip " + chip);
        try { set(0, false); } catch (Exception ignored) {}
        return true;
    }

    // CM108: HID output report {0, data, mask, 0}, mask bit n-1 selects GPIO n
    // as an output, data bit sets its level. Same bytes as Dire Wolf.
    private static void setGpio(boolean on) {
        int mask = 1 << (gpio - 1);
        int data = on ? mask : 0;
        byte[] report = new byte[] { 0, (byte) data, (byte) mask, 0 };
        int r = connection.controlTransfer(0x21, 0x09, 0x0200, iface.getId(), report, report.length, 500);
        if (r < 0) Log.e(TAG, "CM108 SET_REPORT failed: " + r);
    }

    // Serial adapters: RTS and/or DTR through the chip's own request.
    private static void setModemLines(boolean on) {
        boolean rts = (line == 1 && on) || (line2 == 1 && on);
        boolean dtr = (line == 2 && on) || (line2 == 2 && on);
        int r = -1;
        switch (chip) {
            case CHIP_CDC:
            case CHIP_PL2303: {
                int value = (dtr ? 1 : 0) | (rts ? 2 : 0);
                r = connection.controlTransfer(0x21, 0x22, value, iface.getId(), null, 0, 500);
                break;
            }
            case CHIP_FTDI: {
                // SIO_SET_MODEM_CTRL: high byte enables the bit, low byte sets it; index = port + 1.
                int value = (dtr ? 0x0101 : 0x0100) | (rts ? 0x0202 : 0x0200);
                r = connection.controlTransfer(0x40, 0x01, value, 1, null, 0, 500);
                break;
            }
            case CHIP_CP210X: {
                // SET_MHS: bits 0/1 DTR/RTS state, bits 8/9 their masks.
                int value = (dtr ? 0x0001 : 0) | (rts ? 0x0002 : 0) | 0x0300;
                r = connection.controlTransfer(0x41, 0x07, value, iface.getId(), null, 0, 500);
                break;
            }
            case CHIP_CH34X: {
                int value = ~((dtr ? 0x20 : 0) | (rts ? 0x40 : 0)) & 0xffff;
                r = connection.controlTransfer(0x40, 0xa4, value, 0, null, 0, 500);
                break;
            }
            default:
                Log.e(TAG, "No control request known for this adapter");
                return;
        }
        if (r < 0) Log.e(TAG, "Modem line request failed: " + r);
    }
}
