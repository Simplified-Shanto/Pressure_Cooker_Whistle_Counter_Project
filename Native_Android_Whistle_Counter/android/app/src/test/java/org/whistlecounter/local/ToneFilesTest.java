package org.whistlecounter.local;

import org.junit.*;
import org.junit.rules.TemporaryFolder;
import java.io.*;
import java.nio.file.Files;
import static org.junit.Assert.*;

public class ToneFilesTest {
    @Rule public TemporaryFolder temp = new TemporaryFolder();

    @Test public void selectedAudioRemainsAvailableWithoutOriginalUri() throws Exception {
        File folder = temp.newFolder("tones");
        byte[] original = new byte[]{82, 73, 70, 70, 10, 20, 30};
        File staged = ToneFiles.stage(new ByteArrayInputStream(original), folder);
        File saved = ToneFiles.commit(staged);
        assertFalse(staged.exists());
        assertArrayEquals(original, Files.readAllBytes(ToneFiles.selected(folder, saved.getName()).toPath()));
    }
    @Test public void failedImportDoesNotReplaceExistingTone() throws Exception {
        File folder = temp.newFolder("tones");
        byte[] original = new byte[]{1, 2, 3};
        File saved = ToneFiles.commit(ToneFiles.stage(new ByteArrayInputStream(original), folder));
        try { ToneFiles.stage(new ByteArrayInputStream(new byte[0]), folder); fail(); }
        catch (IOException expected) { /* empty audio is rejected */ }
        assertArrayEquals(original, Files.readAllBytes(saved.toPath()));
        assertEquals(1, folder.listFiles().length);
    }
    @Test public void oversizeImportIsRejectedAndTemporaryFileRemoved() throws Exception {
        File folder = temp.newFolder("tones");
        try { ToneFiles.stage(new ByteArrayInputStream(new byte[ToneFiles.MAX_BYTES + 1]), folder); fail(); }
        catch (IOException expected) { assertTrue(expected.getMessage().contains("8 MB")); }
        assertEquals(0, folder.listFiles().length);
    }
    @Test public void invalidStoredPathCannotEscapePrivateToneFolder() throws Exception {
        File folder = temp.newFolder("tones");
        try { ToneFiles.selected(folder, "../pairing.xml"); fail(); }
        catch (IOException expected) { assertTrue(expected.getMessage().contains("Invalid")); }
    }
}
