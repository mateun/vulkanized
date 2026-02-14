/* gen_sounds.c — generates simple retro sound effect WAVs for the shmup.
 * Compile: cl gen_sounds.c /Fe:gen_sounds.exe
 * Run:     gen_sounds.exe  (creates WAVs in current directory)
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define PI 3.14159265358979323846

/* Write a mono 16-bit WAV file */
static void write_wav(const char *path, const int16_t *samples, int num_samples)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("ERROR: can't write %s\n", path); return; }

    int data_size = num_samples * 2; /* 16-bit = 2 bytes per sample */
    int file_size = 44 + data_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    int chunk_size = file_size - 8;
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* fmt sub-chunk */
    fwrite("fmt ", 1, 4, f);
    int fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    int16_t audio_format = 1; /* PCM */
    fwrite(&audio_format, 2, 1, f);
    int16_t channels = 1;
    fwrite(&channels, 2, 1, f);
    int sample_rate = SAMPLE_RATE;
    fwrite(&sample_rate, 4, 1, f);
    int byte_rate = SAMPLE_RATE * 2;
    fwrite(&byte_rate, 4, 1, f);
    int16_t block_align = 2;
    fwrite(&block_align, 2, 1, f);
    int16_t bits = 16;
    fwrite(&bits, 2, 1, f);

    /* data sub-chunk */
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(samples, 2, (size_t)num_samples, f);

    fclose(f);
    printf("Wrote %s (%d samples, %.3f sec)\n", path, num_samples,
           (double)num_samples / SAMPLE_RATE);
}

/* ---- Shoot sound: short rising chirp ---- */
static void gen_shoot(void)
{
    int len = (int)(0.1 * SAMPLE_RATE);  /* 100ms */
    int16_t buf[SAMPLE_RATE]; /* enough for 1 sec */

    for (int i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;
        double env = 1.0 - t / 0.1;           /* linear fade out */
        double freq = 800.0 + 2000.0 * t / 0.1; /* 800 → 2800 Hz chirp */
        double sample = sin(2.0 * PI * freq * t) * env * 0.7;
        buf[i] = (int16_t)(sample * 32767);
    }
    write_wav("shoot.wav", buf, len);
}

/* ---- Explosion sound: noise burst with low-pass decay ---- */
static void gen_explosion(void)
{
    int len = (int)(0.4 * SAMPLE_RATE);  /* 400ms */
    int16_t buf[SAMPLE_RATE];

    double prev = 0.0;
    for (int i = 0; i < len; i++) {
        double t = (double)i / SAMPLE_RATE;
        double env = 1.0 - t / 0.4;           /* linear fade */
        env = env * env;                        /* quadratic for punch */
        /* White noise */
        double noise = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
        /* Simple low-pass: smoothing increases over time (darker rumble) */
        double alpha = 0.3 - 0.25 * (t / 0.4);
        if (alpha < 0.05) alpha = 0.05;
        double sample = alpha * noise + (1.0 - alpha) * prev;
        prev = sample;
        buf[i] = (int16_t)(sample * env * 32767);
    }
    write_wav("explosion.wav", buf, len);
}

int main(void)
{
    gen_shoot();
    gen_explosion();
    printf("Done! Copy shoot.wav and explosion.wav to assets/\n");
    return 0;
}
