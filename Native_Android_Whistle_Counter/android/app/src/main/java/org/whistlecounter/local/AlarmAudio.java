package org.whistlecounter.local;

import android.content.*;
import android.content.res.AssetFileDescriptor;
import android.media.*;
import java.io.File;

public final class AlarmAudio {
    private AlarmAudio() {}
    public static File folder(Context context) { return new File(context.getFilesDir(), "alarm-tones"); }

    public static MediaPlayer prepare(Context context, AudioAttributes attributes) throws Exception {
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        if (prefs.getBoolean("custom_tone", false)) {
            MediaPlayer selected = new MediaPlayer();
            try {
                selected.setAudioAttributes(attributes);
                selected.setDataSource(ToneFiles.selected(folder(context), prefs.getString("tone_file", "")).getAbsolutePath());
                selected.setLooping(true); selected.prepare();
                prefs.edit().putString("tone_error", "").apply();
                return selected;
            } catch (Exception error) {
                selected.release();
                prefs.edit().putString("tone_error", "Saved sound could not play; using the built-in alarm. Select the sound again.").apply();
            }
        }
        MediaPlayer bundled = new MediaPlayer();
        try {
            bundled.setAudioAttributes(attributes);
            try (AssetFileDescriptor file = context.getResources().openRawResourceFd(R.raw.counter_alarm)) {
                bundled.setDataSource(file.getFileDescriptor(), file.getStartOffset(), file.getLength());
            }
            bundled.setLooping(true); bundled.prepare(); return bundled;
        } catch (Exception error) { bundled.release(); throw error; }
    }
}
