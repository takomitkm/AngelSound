package moe.aimo.angelsound;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashSet;
import java.util.Set;

/* SDLActivity entry point plus a thin SAF bridge.  The native side calls the
 * static-looking instance methods below through JNI; all ContentResolver work
 * happens on the SDL thread, and the folder picker itself runs on the UI
 * thread. */
public class AngelSoundActivity extends SDLActivity {
    private static final String TAG = "AngelSound";
    private static final int REQ_PICK_FOLDER = 0x4153;

    private static volatile String sPickedUri;
    private static volatile Set<String> sGrantsBeforePick;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        /* Treat grants that survive process/activity recreation as the
         * baseline.  Only grants added by a picker opened in this process
         * should be reported as a fresh folder choice. */
        if (sGrantsBeforePick == null) {
            sGrantsBeforePick = persistedTreeUris();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQ_PICK_FOLDER) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                Uri uri = data.getData();
                int flags = data.getFlags()
                        & (Intent.FLAG_GRANT_READ_URI_PERMISSION
                           | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                try {
                    getContentResolver().takePersistableUriPermission(uri, flags);
                } catch (Exception e) {
                    Log.w(TAG, "persistable permission failed", e);
                }
                sPickedUri = uri.toString();
                persistFolder(sPickedUri);
            } else {
                sPickedUri = "";
            }
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    public void pickFolder() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                sPickedUri = null;
                sGrantsBeforePick = persistedTreeUris();
                startActivityForResult(intent, REQ_PICK_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.w(TAG, "no document tree picker", e);
                sPickedUri = "";
            }
        });
    }

    /* Public fallback folder used when the SAF picker cannot deliver a result.
     * The app holds the sdcard_rw group, so a plain path works here. */

    public String getPublicFolder() {
        try {
            File music = Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_MUSIC);
            File dir = new File(music, "AngelSound");
            /* canWrite() is false under scoped storage even when the app can
             * really write there, so only the directory creation decides. */
            if (!dir.isDirectory() && !dir.mkdirs()) {
                return "";
            }
            return dir.getAbsolutePath();
        } catch (Exception e) {
            Log.w(TAG, "public folder lookup failed", e);
            return "";
        }
    }

    public String getPickedUri() {
        String value = sPickedUri;
        sPickedUri = null;
        if (value == null) {
            Set<String> before = sGrantsBeforePick;
            value = newPersistedTree(before);
            if (value != null) {
                /* Native poll found a fresh grant: mirror it so storage_init
                 * in a restarted process reads the chosen folder, then move
                 * the baseline forward so it is only reported once. */
                persistFolder(value);
                sGrantsBeforePick = persistedTreeUris();
            }
        }
        return value;
    }

    private Set<String> persistedTreeUris() {
        Set<String> all = new HashSet<>();
        try {
            for (UriPermission p : getContentResolver().getPersistedUriPermissions()) {
                if (p != null && p.isReadPermission() && p.getUri() != null) {
                    all.add(p.getUri().toString());
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "persisted uri scan failed", e);
        }
        return all;
    }

    /* Pick a persisted tree URI that was not present when the picker opened. */
    private String newPersistedTree(Set<String> before) {
        try {
            Set<String> now = persistedTreeUris();
            for (String uri : now) {
                if (before == null || !before.contains(uri)) {
                    return uri;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "new persisted tree scan failed", e);
        }
        return null;
    }

    /* Mirror the chosen tree URI to the file the native storage layer reads. */
    private void persistFolder(String uri) {
        File settings = new File(getFilesDir(), "settings.txt");
        FileOutputStream out = null;
        try {
            out = new FileOutputStream(settings, false);
            out.write(("recordings_dir=" + uri + "\n").getBytes("UTF-8"));
            out.flush();
        } catch (Exception e) {
            Log.w(TAG, "persist folder failed", e);
        } finally {
            if (out != null) {
                try { out.close(); } catch (Exception ignored) { }
            }
        }
    }

    private static Uri tree(String value) {
        if (value == null || value.isEmpty()) {
            return null;
        }
        try {
            return Uri.parse(value);
        } catch (Exception e) {
            return null;
        }
    }

    private static Uri childrenUri(Uri tree) {
        return DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getTreeDocumentId(tree));
    }

    private Uri findDocument(Uri tree, String name) {
        if (tree == null || name == null) {
            return null;
        }
        String[] projection = {
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(childrenUri(tree), projection,
                    null, null, null);
            if (cursor == null) {
                return null;
            }
            int nameCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            int idCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            int mimeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE);
            while (cursor.moveToNext()) {
                String display = nameCol >= 0 ? cursor.getString(nameCol) : null;
                String mime = mimeCol >= 0 ? cursor.getString(mimeCol) : null;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    continue;
                }
                if (name.equals(display)) {
                    if (idCol < 0) {
                        return null;
                    }
                    String id = cursor.getString(idCol);
                    return DocumentsContract.buildDocumentUriUsingTree(tree, id);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "findDocument failed", e);
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return null;
    }

    public String[] listWavs(String uriString) {
        Uri tree = tree(uriString);
        if (tree == null) {
            return new String[0];
        }
        String[] projection = {
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        Cursor cursor = null;
        java.util.ArrayList<String> names = new java.util.ArrayList<>();
        try {
            cursor = getContentResolver().query(childrenUri(tree), projection,
                    null, null, null);
            if (cursor != null) {
                int nameCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
                int mimeCol = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE);
                while (cursor.moveToNext()) {
                    String display = nameCol >= 0 ? cursor.getString(nameCol) : null;
                    String mime = mimeCol >= 0 ? cursor.getString(mimeCol) : null;
                    if (display == null || DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        continue;
                    }
                    if (display.toLowerCase(java.util.Locale.ROOT).endsWith(".wav")) {
                        names.add(display);
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "listWavs failed", e);
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
        return names.toArray(new String[0]);
    }

    public boolean exists(String uriString, String name) {
        return findDocument(tree(uriString), name) != null;
    }

    public boolean readToCache(String uriString, String name, String destPath) {
        Uri doc = findDocument(tree(uriString), name);
        if (doc == null) {
            return false;
        }
        File dest = new File(destPath);
        File parent = dest.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            return false;
        }
        InputStream in = null;
        OutputStream out = null;
        try {
            in = getContentResolver().openInputStream(doc);
            out = new FileOutputStream(dest, false);
            if (in == null) {
                return false;
            }
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
            }
            out.flush();
            return true;
        } catch (Exception e) {
            Log.w(TAG, "readToCache failed", e);
            return false;
        } finally {
            try {
                if (in != null) {
                    in.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (out != null) {
                    out.close();
                }
            } catch (Exception ignored) {
            }
        }
    }

    public boolean writeFromCache(String uriString, String name, String srcPath) {
        Uri tree = tree(uriString);
        if (tree == null) {
            return false;
        }
        InputStream in = null;
        OutputStream out = null;
        try {
            in = new java.io.FileInputStream(new File(srcPath));
            Uri doc = findDocument(tree, name);
            if (doc == null) {
                doc = DocumentsContract.createDocument(getContentResolver(), tree,
                        "audio/wav", name);
            }
            if (doc == null) {
                return false;
            }
            try {
                out = getContentResolver().openOutputStream(doc, "wt");
            } catch (Exception e) {
                out = getContentResolver().openOutputStream(doc);
            }
            if (out == null) {
                return false;
            }
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
            }
            out.flush();
            return true;
        } catch (Exception e) {
            Log.w(TAG, "writeFromCache failed", e);
            return false;
        } finally {
            try {
                if (in != null) {
                    in.close();
                }
            } catch (Exception ignored) {
            }
            try {
                if (out != null) {
                    out.close();
                }
            } catch (Exception ignored) {
            }
        }
    }

    public boolean deleteDoc(String uriString, String name) {
        Uri doc = findDocument(tree(uriString), name);
        if (doc == null) {
            return false;
        }
        try {
            return DocumentsContract.deleteDocument(getContentResolver(), doc);
        } catch (Exception e) {
            Log.w(TAG, "deleteDoc failed", e);
            return false;
        }
    }
}