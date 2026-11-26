package moe.aimo.angelsound;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.Log;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.ActivityResultRegistry;
import androidx.activity.result.contract.ActivityResultContract;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.core.app.ActivityOptionsCompat;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.LifecycleRegistry;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/* Activity owns the Android lifecycle.  Native is a passive engine: it only
 * receives folder/permission/lifecycle callbacks and the SAF file operations
 * it needs for an explicit read/write/delete. */
public class AngelSoundActivity extends SDLActivity {
    private static final String TAG = "AngelSound";

    private final ActivityResultRegistry mRegistry = new ActivityResultRegistry() {
        @Override
        public <I, O> void onLaunch(int requestCode, ActivityResultContract<I, O> contract,
                                    I input, ActivityOptionsCompat options) {
            Intent intent = contract.createIntent(AngelSoundActivity.this, input);
            startActivityForResult(intent, requestCode);
        }
    };
    private ActivityResultLauncher<Uri> mFolderLauncher;

    private static final class Owner implements androidx.lifecycle.LifecycleOwner {
        private final LifecycleRegistry lifecycle = new LifecycleRegistry(this);
        Owner() { lifecycle.setCurrentState(Lifecycle.State.CREATED); }
        @Override public Lifecycle getLifecycle() { return lifecycle; }
    }
    private final Owner mOwner = new Owner();
    private boolean mPickerOpen;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mFolderLauncher = mRegistry.register("saf_folder", mOwner,
                new ActivityResultContracts.OpenDocumentTree(), this::onFolderChosen);
        if (savedInstanceState != null) {
            mRegistry.onRestoreInstanceState(savedInstanceState);
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        mRegistry.onSaveInstanceState(outState);
    }

    private void onFolderChosen(Uri uri) {
        mPickerOpen = false;
        nativeFolderPickerClosed();
        if (uri == null) {
            return;
        }
        int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Exception e) {
            Log.w(TAG, "persistable permission failed", e);
        }
        String value = uri.toString();
        persistFolder(value);
        nativeSetFolder(value);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        boolean dispatched = mRegistry.dispatchResult(requestCode, resultCode, data);
        /* Some ROMs return CANCELED but still deliver a valid tree URI.  Trust
         * the payload: this is the only reliable signal on those devices. */
        if (data != null && data.getData() != null) {
            onFolderChosen(data.getData());
        }
        if (!dispatched) {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    public void pickFolder() {
        runOnUiThread(() -> {
            try {
                mPickerOpen = true;
                nativeFolderPickerOpened();
                mFolderLauncher.launch(null);
            } catch (Exception e) {
                Log.w(TAG, "folder launcher failed", e);
                mPickerOpen = false;
                nativeFolderPickerClosed();
            }
        });
    }

    @Override
    protected void onDestroy() {
        nativeStop();
        super.onDestroy();
    }

    private void persistFolder(String uri) {
        try (java.io.FileOutputStream out =
                     openFileOutput("settings.txt", android.content.Context.MODE_PRIVATE)) {
            out.write(("recordings_dir=" + uri + "\n").getBytes("UTF-8"));
            out.flush();
        } catch (Exception e) {
            Log.w(TAG, "persist folder failed", e);
        }
    }

    private static native void nativeSetFolder(String uri);
    private static native void nativeFolderPickerOpened();
    private static native void nativeFolderPickerClosed();
    private static native void nativeStop();

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
                    /* Only the app's own takes are listed, matching the
                     * desktop storage_list() which globs ASREC_*.wav. */
                    if (display.startsWith("ASREC_") && display.endsWith(".wav")) {
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