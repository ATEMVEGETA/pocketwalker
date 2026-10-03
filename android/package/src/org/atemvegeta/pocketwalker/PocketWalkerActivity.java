package org.atemvegeta.pocketwalker;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.UUID;

import org.qtproject.qt.android.bindings.QtActivity;

public class PocketWalkerActivity extends QtActivity {
    public static final int FILE_ROM = 1;
    public static final int IR_MODE_PC = 0;
    public static final int IR_MODE_AUTO_PEER = 1;
    public static final int IR_MODE_OFF = 2;

    private static final int PERMISSION_REQUEST = 1001;
    private static final int ROM_FOLDER_REQUEST = 1002;
    private static final String PREFS_NAME = "pocketwalker_storage";
    private static final String ROM_FOLDER_URI_KEY = "rom_folder_uri";
    private static final String IR_MODE_KEY = "ir_connection_mode";
    private static final String IR_PC_HOST_KEY = "ir_pc_host";
    private static final String PEER_ID_KEY = "peer_device_id";

    private static volatile PocketWalkerActivity currentActivity;
    private static volatile Context applicationContext;
    private static volatile boolean serviceRequested;
    private boolean closingNotified;
    private boolean exitPromptVisible;

    private static native void nativeOnFileSelectionResult(int fileType, boolean changed);
    private static native void nativeOnAppClosing();
    private static native void nativeOnAppBackgrounded();

    @Override
    public void onCreate(Bundle state) {
        currentActivity = this;
        applicationContext = getApplicationContext();
        super.onCreate(state);
        requestWalkingPermissions();
    }

    @Override
    public void onBackPressed() {
        showExitConfirmation();
    }

    @Override
    protected void onPause() {
        if (!closingNotified)
            nativeOnAppBackgrounded();
        super.onPause();
    }

    public static void showExitConfirmation() {
        PocketWalkerActivity activity = currentActivity;
        if (activity == null)
            return;
        activity.runOnUiThread(activity::showExitConfirmationInternal);
    }

    private void showExitConfirmationInternal() {
        if (closingNotified || exitPromptVisible)
            return;

        exitPromptVisible = true;
        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("Close PocketWalker?")
            .setMessage("Your current progress will be saved before the app closes.")
            .setNegativeButton("Cancel", (ignored, which) -> exitPromptVisible = false)
            .setPositiveButton("Close", (ignored, which) -> {
                exitPromptVisible = false;
                closingNotified = true;
                nativeOnAppClosing();
            })
            .create();
        dialog.setOnCancelListener(ignored -> exitPromptVisible = false);
        dialog.show();
    }

    @Override
    protected void onDestroy() {
        if (isFinishing() && !isChangingConfigurations() && !closingNotified) {
            closingNotified = true;
            // The service owns task-removal shutdown. Qt is retained here so
            // QtActivityBase does not wait on a main loop that Android is in
            // the middle of detaching.
            super.onRetainNonConfigurationInstance();
        }
        if (currentActivity == this)
            currentActivity = null;
        super.onDestroy();
    }

    public static void chooseRomFile() {
        PocketWalkerActivity activity = currentActivity;
        if (activity == null) {
            nativeOnFileSelectionResult(FILE_ROM, false);
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        Uri current = selectedFolderUri(activity);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && current != null)
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, current);
        activity.startActivityForResult(intent, ROM_FOLDER_REQUEST);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != ROM_FOLDER_REQUEST)
            return;

        boolean changed = false;
        if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
            Uri treeUri = data.getData();
            int flags = data.getFlags() &
                (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            try {
                getContentResolver().takePersistableUriPermission(treeUri, flags);
                if (findChildDocument(getContentResolver(), treeUri, "rom.bin") != null) {
                    preferences(this).edit()
                        .putString(ROM_FOLDER_URI_KEY, treeUri.toString())
                        .commit();
                    changed = true;
                } else {
                    new AlertDialog.Builder(this)
                        .setTitle("rom.bin not found")
                        .setMessage("The selected folder does not contain rom.bin.")
                        .setPositiveButton("OK", null)
                        .show();
                }
            } catch (SecurityException ignored) {
            }
        }
        nativeOnFileSelectionResult(FILE_ROM, changed);
    }

    public static void finishAfterNativeClose() {
        PocketWalkerActivity activity = currentActivity;
        Context context = applicationContext;
        // QtActivity.onDestroy waits for the native Qt main loop to return.
        // Give that thread time to finish before Android destroys the Activity.
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            serviceRequested = false;
            if (context != null)
                PocketWalkerService.stop(context);
            if (activity != null && !activity.isFinishing())
                activity.finishAndRemoveTask();
            else
                System.exit(0);
        }, 150);
    }

    public static boolean hasRomFile() {
        Context context = applicationContext;
        if (context == null)
            return false;
        Uri treeUri = selectedFolderUri(context);
        Uri romUri = findChildDocument(context.getContentResolver(), treeUri, "rom.bin");
        if (romUri != null)
            return true;
        clearFolderSelection(context);
        return false;
    }

    public static String selectedRomPath() {
        Context context = applicationContext;
        if (context == null)
            return "No PocketWalker folder selected";
        String folder = selectedFolderPath(context, "No PocketWalker folder selected");
        if ("No PocketWalker folder selected".equals(folder))
            return folder;
        return folder + "\nrom.bin + rom.pwsav";
    }

    public static int getIrConnectionMode() {
        Context context = applicationContext;
        if (context == null)
            return IR_MODE_OFF;
        return preferences(context).getInt(IR_MODE_KEY, IR_MODE_OFF);
    }

    public static int setIrConnectionMode(int mode) {
        int normalized = mode == IR_MODE_AUTO_PEER || mode == IR_MODE_OFF
            ? mode : IR_MODE_PC;
        Context context = applicationContext;
        if (context == null)
            return normalized;
        boolean saved = preferences(context).edit().putInt(IR_MODE_KEY, normalized).commit();
        return saved ? normalized : getIrConnectionMode();
    }

    public static String getIrPcHost() {
        Context context = applicationContext;
        if (context == null)
            return "";
        return preferences(context).getString(IR_PC_HOST_KEY, "");
    }

    public static String setIrPcHost(String host) {
        Context context = applicationContext;
        String normalized = host == null ? "" : host.trim();
        if (context == null)
            return normalized;
        boolean saved = preferences(context).edit().putString(IR_PC_HOST_KEY, normalized).commit();
        return saved ? normalized : getIrPcHost();
    }

    public static String getPeerDeviceId() {
        Context context = applicationContext;
        if (context == null)
            return "";
        SharedPreferences prefs = preferences(context);
        String id = prefs.getString(PEER_ID_KEY, null);
        if (id == null || id.isEmpty()) {
            id = UUID.randomUUID().toString();
            prefs.edit().putString(PEER_ID_KEY, id).commit();
        }
        return id;
    }

    public static boolean syncFromFiles(String internalDirectory) {
        Context context = applicationContext;
        if (context == null) {
            return false;
        }

        ContentResolver resolver = context.getContentResolver();
        Uri treeUri = selectedFolderUri(context);
        Uri romUri = findChildDocument(resolver, treeUri, "rom.bin");
        if (romUri == null)
            return false;

        File directory = new File(internalDirectory);
        if (!directory.exists() && !directory.mkdirs())
            return false;
        if (!copyToInternalAtomically(resolver, romUri, new File(directory, "rom.bin")))
            return false;

        Uri saveUri = findChildDocument(resolver, treeUri, "rom.pwsav");
        File internalSave = new File(directory, "rom.pwsav");
        if (saveUri == null) {
            deleteInternalSaveFiles(directory);
            return true;
        }
        if (!copyToInternalAtomically(resolver, saveUri, internalSave))
            return false;
        if (!hasPwsavHeader(internalSave)) {
            long invalidLength = internalSave.length();
            deleteIfPresent(internalSave);
            // Recover placeholders left by an interrupted/failed save creation.
            // Non-empty malformed saves are never silently replaced.
            return invalidLength <= 8;
        }
        return true;
    }

    public static boolean syncToSaveFile(String internalDirectory) {
        Context context = applicationContext;
        if (context == null)
            return false;

        File internalSave = new File(internalDirectory, "rom.pwsav");
        if (!hasPwsavHeader(internalSave))
            return false;

        ContentResolver resolver = context.getContentResolver();
        Uri treeUri = selectedFolderUri(context);
        if (treeUri == null)
            return false;
        Uri saveUri = findChildDocument(resolver, treeUri, "rom.pwsav");
        if (saveUri == null)
            saveUri = createChildDocument(resolver, treeUri, "rom.pwsav");
        if (saveUri == null)
            return false;

        try (InputStream input = new FileInputStream(internalSave);
             OutputStream output = resolver.openOutputStream(saveUri, "wt")) {
            if (output == null)
                return false;
            copy(input, output);
            output.flush();
        } catch (Exception ignored) {
            return false;
        }

        if (!hasPwsavDocument(resolver, saveUri, internalSave.length()))
            return false;

        return true;
    }

    public static void startPocketWalkerService() {
        Context context = applicationContext;
        if (context == null)
            return;
        serviceRequested = true;
        PocketWalkerService.start(context);
    }

    public static void stopPocketWalkerService() {
        Context context = applicationContext;
        serviceRequested = false;
        if (context != null)
            PocketWalkerService.stop(context);
    }

    private void requestWalkingPermissions() {
        ArrayList<String> permissions = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
            checkSelfPermission(Manifest.permission.ACTIVITY_RECOGNITION) != PackageManager.PERMISSION_GRANTED)
            permissions.add(Manifest.permission.ACTIVITY_RECOGNITION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            permissions.add(Manifest.permission.POST_NOTIFICATIONS);
        if (!permissions.isEmpty())
            requestPermissions(permissions.toArray(new String[0]), PERMISSION_REQUEST);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == PERMISSION_REQUEST && serviceRequested)
            PocketWalkerService.start(this);
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    private static Uri selectedFolderUri(Context context) {
        String value = preferences(context).getString(ROM_FOLDER_URI_KEY, null);
        return value == null || value.isEmpty() ? null : Uri.parse(value);
    }

    private static void clearFolderSelection(Context context) {
        preferences(context).edit().remove(ROM_FOLDER_URI_KEY).commit();
    }

    private static Uri findChildDocument(
        ContentResolver resolver, Uri treeUri, String displayName) {
        if (treeUri == null)
            return null;
        try {
            String treeId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeId);
            try (Cursor cursor = resolver.query(
                     childrenUri,
                     new String[] {
                         DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                         DocumentsContract.Document.COLUMN_DISPLAY_NAME
                     }, null, null, null)) {
                if (cursor == null)
                    return null;
                while (cursor.moveToNext()) {
                    if (displayName.equalsIgnoreCase(cursor.getString(1))) {
                        return DocumentsContract.buildDocumentUriUsingTree(
                            treeUri, cursor.getString(0));
                    }
                }
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private static Uri createChildDocument(
        ContentResolver resolver, Uri treeUri, String displayName) {
        if (treeUri == null)
            return null;
        try {
            String treeId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri directoryUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeId);
            return DocumentsContract.createDocument(
                resolver, directoryUri, "application/octet-stream", displayName);
        } catch (Exception ignored) {
            return null;
        }
    }

    private static String selectedFolderPath(Context context, String fallback) {
        Uri treeUri = selectedFolderUri(context);
        if (treeUri == null)
            return fallback;
        try {
            String documentId = DocumentsContract.getTreeDocumentId(treeUri);
            int separator = documentId.indexOf(':');
            if (separator >= 0) {
                String volume = documentId.substring(0, separator);
                String relative = documentId.substring(separator + 1);
                String root = "primary".equalsIgnoreCase(volume) ? "Internal storage" : volume;
                return root + "/" + relative;
            }
            return documentId;
        } catch (Exception ignored) {
            return treeUri.toString();
        }
    }

    private static boolean copyToInternalAtomically(
        ContentResolver resolver, Uri source, File destination) {
        File temporary = new File(destination.getParentFile(), destination.getName() + ".tmp");
        try (InputStream input = resolver.openInputStream(source);
             FileOutputStream output = new FileOutputStream(temporary, false)) {
            if (input == null)
                return false;
            copy(input, output);
            output.flush();
            output.getFD().sync();
        } catch (Exception ignored) {
            deleteIfPresent(temporary);
            return false;
        }

        if (destination.exists() && !destination.delete()) {
            deleteIfPresent(temporary);
            return false;
        }
        if (!temporary.renameTo(destination)) {
            deleteIfPresent(temporary);
            return false;
        }
        return true;
    }

    private static void copy(InputStream input, OutputStream output) throws Exception {
        byte[] buffer = new byte[64 * 1024];
        int read;
        while ((read = input.read(buffer)) >= 0) {
            if (read > 0)
                output.write(buffer, 0, read);
        }
    }

    private static boolean hasPwsavHeader(File file) {
        final byte[] expected = new byte[] {'P', 'W', 'S', 'A', 'V', '0', '0', '1'};
        if (!file.isFile() || file.length() < expected.length)
            return false;

        try (InputStream input = new FileInputStream(file)) {
            for (byte value : expected) {
                if (input.read() != (value & 0xFF))
                    return false;
            }
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    private static boolean hasPwsavDocument(
        ContentResolver resolver, Uri uri, long expectedLength) {
        final byte[] expected = new byte[] {'P', 'W', 'S', 'A', 'V', '0', '0', '1'};
        long total = 0;
        try (InputStream input = resolver.openInputStream(uri)) {
            if (input == null)
                return false;
            for (byte value : expected) {
                if (input.read() != (value & 0xFF))
                    return false;
                total++;
            }
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0)
                    total += read;
            }
        } catch (Exception ignored) {
            return false;
        }

        return total > expected.length && (expectedLength < 0 || total == expectedLength);
    }

    private static void deleteInternalSaveFiles(File directory) {
        deleteIfPresent(new File(directory, "rom.pwsav"));
        deleteIfPresent(new File(directory, "rom.sav"));
        deleteIfPresent(new File(directory, "rom.sav.state"));
        deleteIfPresent(new File(directory, "rom.sav.rtc"));
    }

    private static void deleteIfPresent(File file) {
        if (file.exists())
            file.delete();
    }
}
