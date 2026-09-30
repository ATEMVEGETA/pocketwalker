package org.atemvegeta.pocketwalker;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

public class PocketWalkerService extends Service implements SensorEventListener {
    private static final String CHANNEL_ID = "pocketwalker_active";
    private static final int NOTIFICATION_ID = 1516;

    private SensorManager sensorManager;
    private Sensor motionSensor;
    private Sensor wakeStepSensor;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.MulticastLock multicastLock;
    private long lastMotionPulseNanos;
    private boolean sensorIncludesGravity;
    private boolean gravityInitialized;
    private float gravityX;
    private float gravityY;
    private float gravityZ;
    private static final float MOTION_PEAK_THRESHOLD = 2.2f;
    private static final long MOTION_PULSE_COOLDOWN_NANOS = 350_000_000L;
    private static native boolean nativeSetMotionEnabled(boolean enabled);
    private static native boolean nativeOnAcceleration(float x, float y, float z);
    private static native boolean nativeOnMotionPulse();
    private static native void nativeOnAppClosing();

    public static void start(Context context) {
        Intent intent = new Intent(context, PocketWalkerService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            context.startForegroundService(intent);
        else
            context.startService(intent);
    }

    public static void stop(Context context) {
        context.stopService(new Intent(context, PocketWalkerService.class));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, createNotification());

        PowerManager power = (PowerManager)getSystemService(POWER_SERVICE);
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "PocketWalker:ContinuousEmulation");
        wakeLock.acquire();

        WifiManager wifi = (WifiManager)getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("PocketWalker:PeerDiscovery");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        }

        sensorManager = (SensorManager)getSystemService(SENSOR_SERVICE);
        registerWalkingSensor();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        registerWalkingSensor();
        return START_NOT_STICKY;
    }

    private void registerWalkingSensor() {
        if (sensorManager == null)
            return;

        if (motionSensor == null) {
            motionSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION, true);
            if (motionSensor == null)
                motionSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION);
            if (motionSensor == null) {
                motionSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER, true);
                if (motionSensor == null)
                    motionSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            }
            if (motionSensor == null)
                return;
            sensorIncludesGravity = motionSensor.getType() == Sensor.TYPE_ACCELEROMETER;
            boolean registered = sensorManager.registerListener(
                this, motionSensor, SensorManager.SENSOR_DELAY_GAME, 0);
            if (!registered) {
                motionSensor = null;
                return;
            }
        }

        if (wakeStepSensor == null) {
            wakeStepSensor = sensorManager.getDefaultSensor(Sensor.TYPE_STEP_DETECTOR, true);
            if (wakeStepSensor != null) {
                boolean registered = sensorManager.registerListener(
                    this, wakeStepSensor, SensorManager.SENSOR_DELAY_NORMAL, 0);
                if (!registered)
                    wakeStepSensor = null;
            }
        }

        nativeSetMotionEnabled(true);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_STEP_DETECTOR) {
            nativeOnMotionPulse();
            return;
        }

        if (event.sensor.getType() != Sensor.TYPE_LINEAR_ACCELERATION &&
            event.sensor.getType() != Sensor.TYPE_ACCELEROMETER)
            return;

        float x = event.values[0];
        float y = event.values[1];
        float z = event.values[2];

        if (sensorIncludesGravity) {
            if (!gravityInitialized) {
                gravityX = x;
                gravityY = y;
                gravityZ = z;
                gravityInitialized = true;
            }
            final float alpha = 0.8f;
            gravityX = alpha * gravityX + (1.0f - alpha) * x;
            gravityY = alpha * gravityY + (1.0f - alpha) * y;
            gravityZ = alpha * gravityZ + (1.0f - alpha) * z;
            x -= gravityX;
            y -= gravityY;
            z -= gravityZ;
        }

        float magnitude = (float)Math.sqrt(x * x + y * y + z * z);
        nativeOnAcceleration(x, y, z);

        long now = event.timestamp;
        if (magnitude >= MOTION_PEAK_THRESHOLD &&
            now - lastMotionPulseNanos >= MOTION_PULSE_COOLDOWN_NANOS) {
            lastMotionPulseNanos = now;
            nativeOnMotionPulse();
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    @Override
    public void onDestroy() {
        nativeSetMotionEnabled(false);
        if (sensorManager != null)
            sensorManager.unregisterListener(this);
        wakeStepSensor = null;
        motionSensor = null;
        if (wakeLock != null && wakeLock.isHeld())
            wakeLock.release();
        if (multicastLock != null && multicastLock.isHeld())
            multicastLock.release();
        super.onDestroy();
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        nativeSetMotionEnabled(false);
        nativeOnAppClosing();
        super.onTaskRemoved(rootIntent);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O)
            return;

        NotificationChannel channel = new NotificationChannel(
            CHANNEL_ID, "Active Pokewalker", NotificationManager.IMPORTANCE_LOW);
        channel.setDescription("Keeps the Pokewalker clock and walking sensor active.");
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(channel);
    }

    private Notification createNotification() {
        Intent launch = new Intent(this, PocketWalkerActivity.class);
        PendingIntent pending = PendingIntent.getActivity(
            this, 0, launch, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);

        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
            ? new Notification.Builder(this, CHANNEL_ID)
            : new Notification.Builder(this);

        return builder
            .setContentTitle("PocketWalker is active")
            .setContentText("Clock and accelerometer are running")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pending)
            .setOngoing(true)
            .build();
    }
}
