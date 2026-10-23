package org.whistlecounter.local;

import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.UUID;

/** Copies selected audio into private storage so URI grants are not needed later. */
public final class ToneFiles {
    public static final int MAX_BYTES = 8 * 1024 * 1024;
    private ToneFiles() {}

    public static File stage(InputStream input, File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) throw new IOException("Cannot create the alarm tone folder.");
        File staged = new File(folder, UUID.randomUUID() + ".tmp");
        try (OutputStream output = new FileOutputStream(staged)) {
            byte[] buffer = new byte[8192]; int length, total = 0;
            while ((length = input.read(buffer)) != -1) {
                total += length;
                if (total > MAX_BYTES) throw new IOException("Choose an alarm sound smaller than 8 MB.");
                output.write(buffer, 0, length);
            }
            if (total == 0) throw new IOException("The selected audio file is empty.");
        } catch (IOException error) { staged.delete(); throw error; }
        return staged;
    }

    public static File commit(File staged) throws IOException {
        File saved = new File(staged.getParentFile(), UUID.randomUUID() + ".audio");
        Files.move(staged.toPath(), saved.toPath(), StandardCopyOption.ATOMIC_MOVE);
        return saved;
    }

    public static File selected(File folder, String name) throws IOException {
        if (!name.matches("[a-f0-9-]{36}\\.audio")) throw new IOException("Invalid saved alarm tone.");
        File file = new File(folder, name);
        if (!file.isFile() || file.length() == 0) throw new IOException("Saved alarm tone is unavailable.");
        return file;
    }

    public static void cleanExcept(File folder, String name) {
        File[] files = folder.listFiles();
        if (files != null) for (File file : files) if (!file.getName().equals(name)) file.delete();
    }
}
