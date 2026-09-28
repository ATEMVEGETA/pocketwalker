package org.atemvegeta.pocketwalker;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
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
    public static final int FILE_SAVE = 2;
    public static final int IR_MODE_PC = 0;
    public static final int IR_MODE_AUTO_PEER = 1;
    public static final int IR_MODE_OFF = 2;

    private static final int PERMISSION_REQUEST = 1001;
    private static final int ROM_OPEN_REQUEST = 1002;
    private static final int SAVE_OPEN_REQUEST = 1003;
    private static final int SAVE_CREATE_REQUEST = 1004;
    private static final String PREFS_NAME = "pocketwalker_storage";
    private static final String ROM_URI_KEY = "rom_document_uri";
    private static final String SAVE_URI_KEY = "save_document_uri";
    private static final String IR_MODE_KEY = "ir_connection_mode";
    private static final String IR_PC_HOST_KEY = "ir_pc_host";
    private static final String PEER_ID_KEY = "peer_device_id";

    private static volatile PocketWalkerActivity currentActivity;
    private static volatile Context applicationContext;
    private static volatile boolean serviceRequested;
    private static volatile Uri pendingRomUri;
    private boolean closingNotified;

    private static native void nativeOnFileSelectionResult(int fileType, boolean changed);
    private static native void nativeOnAppClosing();

    @Override
    public void onCreate(Bundle state) {
        currentActivity = this;
        applicationContext = getApplicationContext();
        super.onCreate(state);
        requestWalkingPermissions();
    }

    @Override
    protected void onDestroy() {
        if (isFinishing() && !closingNotified) {
            closingNotified = true;
            nativeOnAppClosing();
        }
        if (currentActivity == this)
            currentActivity = null;
        super.onDestroy();
    }

    public static void chooseRomFile() {
        openExistingDocument(ROM_OPEN_REQUEST, "application/octet-stream");
    }

    public static void chooseSaveFile() {
        openExistingDocument(SAVE_OPEN_REQUEST, "application/octet-stream");
    }

    public static void createNewSave() {
        PocketWalkerActivity activity = currentActivity;
        Context context = applicationContext;
        if (activity == null || context == null) {
            nativeOnFileSelectionResult(FILE_SAVE, false);
            return;
        }

        Uri romUri = pendingRomUri != null ? pendingRomUri : selectedUri(context, ROM_URI_KEY);
        Uri parentUri = parentDocumentUri(romUri);
        if (parentUri != null) {
            try {
                Uri saveUri = DocumentsContract.createDocument(
                    context.getContentResolver(), parentUri,
                    "application/octet-stream", "rom.pwsav");
                if (saveUri != null) {
                    commitSaveSelection(context, saveUri);
                    nativeOnFileSelectionResult(FILE_SAVE, true);
                    return;
                }
            } catch (Exception ignored) {
            }
        }

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.setType("application/octet-stream");
        intent.putExtra(Intent.EXTRA_TITLE, "rom.pwsav");
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && parentUri != null)
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, parentUri);
        activity.startActivityForResult(intent, SAVE_CREATE_REQUEST);
    }

    private static void openExistingDocument(int requestCode, String mimeType) {
        PocketWalkerActivity activity = currentActivity;
        if (activity == null) {
            nativeOnFileSelectionResult(
                requestCode == ROM_OPEN_REQUEST ? FILE_ROM : FILE_SAVE, false);
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.setType(mimeType);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        activity.startActivityForResult(intent, requestCode);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != ROM_OPEN_REQUEST && requestCode != SAVE_OPEN_REQUEST &&
            requestCode != SAVE_CREATE_REQUEST)
            return;

        int fileType = requestCode == ROM_OPEN_REQUEST ? FILE_ROM : FILE_SAVE;
        boolean changed = false;
        if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            int flags = data.getFlags() &
                (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            try {
                getContentResolver().takePersistableUriPermission(uri, flags);
                if (fileType == FILE_ROM) {
                    pendingRomUri = uri;
                } else {
                    commitSaveSelection(this, uri);
                }
                changed = true;
            } catch (SecurityException ignored) {
            }
        }
        if (!changed && fileType == FILE_SAVE)
            pendingRomUri = null;
        nativeOnFileSelectionResult(fileType, changed);
    }

    public static void cancelPendingRomChange() {
        pendingRomUri = null;
    }

    public static boolean hasRomFile() {
        Context context = applicationContext;
        return context != null && hasReadableDocument(context, ROM_URI_KEY);
    }

    public static boolean hasPendingRomFile() {
        return pendingRomUri != null;
    }

    public static boolean hasSaveFile() {
        Context context = applicationContext;
        return context != null && hasReadableDocument(context, SAVE_URI_KEY);
    }

    public static String selectedRomLabel() {
        return selectedDocumentLabel(ROM_URI_KEY, "No ROM selected");
    }

    public static String selectedSaveLabel() {
        return selectedDocumentLabel(SAVE_URI_KEY, "No save selected");
    }

    public static String selectedRomPath() {
        return selectedDocumentPath(ROM_URI_KEY, "No ROM selected");
    }

    public static String selectedSavePath() {
        return selectedDocumentPath(SAVE_URI_KEY, "No save selected");
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
        Uri romUri = selectedUri(context, ROM_URI_KEY);
        if (romUri == null)
            return false;

        File directory = new File(internalDirectory);
        if (!directory.exists() && !directory.mkdirs())
            return false;
        if (!copyToInternalAtomically(resolver, romUri, new File(directory, "rom.bin")))
            return false;

        Uri saveUri = selectedUri(context, SAVE_URI_KEY);
        File internalSave = new File(directory, "rom.pwsav");
        if (saveUri != null) {
            if (!copyToInternalAtomically(resolver, saveUri, internalSave))
                return false;
        } else {
            deleteIfPresent(internalSave);
            deleteIfPresent(new File(directory, "rom.sav"));
            deleteIfPresent(new File(directory, "rom.sav.state"));
            deleteIfPresent(new File(directory, "rom.sav.rtc"));
        }
        return true;
    }

    public static boolean syncToSaveFile(String internalDirectory) {
        Context context = applicationContext;
        if (context == null)
            return false;

        Uri saveUri = selectedUri(context, SAVE_URI_KEY);
        File internalSave = new File(internalDirectory, "rom.pwsav");
        if (saveUri == null || !internalSave.isFile())
            return false;

        try (InputStream input = new FileInputStream(internalSave);
             OutputStream output = context.getContentResolver().openOutputStream(saveUri, "wt")) {
            if (output == null)
                return false;
            copy(input, output);
            output.flush();
            return true;
        } catch (Exception ignored) {
            return false;
        }
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

    private static Uri selectedUri(Context context, String key) {
        String value = preferences(context).getString(key, null);
        return value == null || value.isEmpty() ? null : Uri.parse(value);
    }

    private static void commitSaveSelection(Context context, Uri saveUri) {
        SharedPreferences.Editor editor = preferences(context).edit();
        if (pendingRomUri != null)
            editor.putString(ROM_URI_KEY, pendingRomUri.toString());
        editor.putString(SAVE_URI_KEY, saveUri.toString()).apply();
        pendingRomUri = null;
    }

    private static boolean hasReadableDocument(Context context, String key) {
        Uri uri = selectedUri(context, key);
        if (uri == null)
            return false;

        try (ParcelFileDescriptor descriptor =
                 context.getContentResolver().openFileDescriptor(uri, "r")) {
            if (descriptor != null)
                return true;
        } catch (Exception ignored) {
        }

        preferences(context).edit().remove(key).commit();
        return false;
    }

    private static String selectedDocumentLabel(String key, String fallback) {
        Context context = applicationContext;
        if (context == null)
            return fallback;
        Uri uri = selectedUri(context, key);
        if (uri == null)
            return fallback;

        try (Cursor cursor = context.getContentResolver().query(
                 uri, new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME },
                 null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String label = cursor.getString(0);
                if (label != null && !label.isEmpty())
                    return label;
            }
        } catch (Exception ignored) {
        }
        return uri.getLastPathSegment();
    }

    private static String selectedDocumentPath(String key, String fallback) {
        Context context = applicationContext;
        if (context == null)
            return fallback;
        Uri uri = selectedUri(context, key);
        if (uri == null)
            return fallback;
        try {
            String documentId = DocumentsContract.getDocumentId(uri);
            int separator = documentId.indexOf(':');
            if (separator >= 0) {
                String volume = documentId.substring(0, separator);
                String relative = documentId.substring(separator + 1);
                String root = "primary".equalsIgnoreCase(volume) ? "Internal storage" : volume;
                return root + "/" + relative;
            }
            return documentId;
        } catch (Exception ignored) {
            return uri.toString();
        }
    }

    private static Uri parentDocumentUri(Uri documentUri) {
        if (documentUri == null || !DocumentsContract.isDocumentUri(applicationContext, documentUri))
            return null;
        try {
            String documentId = DocumentsContract.getDocumentId(documentUri);
            int separator = documentId.lastIndexOf('/');
            if (separator < 0)
                return null;
            String parentId = documentId.substring(0, separator);
            return DocumentsContract.buildDocumentUri(documentUri.getAuthority(), parentId);
        } catch (Exception ignored) {
            return null;
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

    private static void deleteIfPresent(File file) {
        if (file.exists())
            file.delete();
    }
}
