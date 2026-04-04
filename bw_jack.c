#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <alsa/asoundlib.h>
#include <sndfile.h>

#define PERIOD_SIZE 4096
#define DEVICE "plughw:0,0"

// logs

static void log_msg(const char *level, const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(stderr, "[%02d:%02d:%02d] [%s] ", t->tm_hour, t->tm_min, t->tm_sec, level);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#define LOG_INFO(...) log_msg("INFO", __VA_ARGS__)
#define LOG_OK(...) log_msg("OK", __VA_ARGS__)
#define LOG_WARN(...) log_msg("WARN", __VA_ARGS__)
#define LOG_ERR(...) log_msg("ERR", __VA_ARGS__)

// alsa

static snd_pcm_t *g_pcm = NULL;

static void handle_sig(int sig)
{
    (void)sig;
    if (g_pcm)
    {
        snd_pcm_drop(g_pcm); // immediate stop, don't drain
        snd_pcm_close(g_pcm);
        g_pcm = NULL;
    }
    LOG_WARN("Interrupted");
    _exit(0);
}

static snd_pcm_t *alsa_open(int rate, int channels)
{
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *params;

    if (snd_pcm_open(&pcm, DEVICE, SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        LOG_ERR("Failed to open ALSA device: %s", DEVICE);
        return NULL;
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, channels);

    unsigned int urate = rate;
    snd_pcm_hw_params_set_rate_near(pcm, params, &urate, 0);

    snd_pcm_uframes_t period = PERIOD_SIZE;
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0);

    if (snd_pcm_hw_params(pcm, params) < 0)
    {
        LOG_ERR("Failed to set ALSA hw params");
        snd_pcm_close(pcm);
        return NULL;
    }

    g_pcm = pcm;
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    signal(SIGPIPE, handle_sig);

    return pcm;
}

// playback

static void play_file(const char *path, int loop)
{
    SF_INFO info = {0};
    SNDFILE *sf = sf_open(path, SFM_READ, &info);
    if (!sf)
    {
        LOG_ERR("Failed to open audio file: %s", path);
        exit(1);
    }

    LOG_INFO("Playing file | rate=%d channels=%d frames=%lld",
             info.samplerate, info.channels, (long long)info.frames);

    snd_pcm_t *pcm = alsa_open(info.samplerate, info.channels);
    if (!pcm)
    {
        sf_close(sf);
        exit(1);
    }

    short *buf = malloc(PERIOD_SIZE * info.channels * sizeof(short));
    if (!buf)
    {
        LOG_ERR("malloc failed");
        exit(1);
    }

    int loop_count = 0;
    do
    {
        if (loop)
            LOG_INFO("Loop #%d", ++loop_count);

        sf_seek(sf, 0, SEEK_SET);
        sf_count_t n;
        while ((n = sf_readf_short(sf, buf, PERIOD_SIZE)) > 0)
        {
            snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf, n);
            if (written == -EPIPE)
            {
                snd_pcm_prepare(pcm); // xrun recovery
            }
            else if (written < 0)
            {
                LOG_WARN("ALSA write error: %s", snd_strerror(written));
                break;
            }
        }
        LOG_OK("Playback finished");
    } while (loop);

    free(buf);
    sf_close(sf);
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
}

static void stream_stdin(int rate, int channels)
{
    LOG_INFO("Streaming stdin to ALSA | rate=%d channels=%d", rate, channels);

    snd_pcm_t *pcm = alsa_open(rate, channels);
    if (!pcm)
        exit(1);

    short buf[PERIOD_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
    {
        snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf, n / (sizeof(short) * channels));
        if (written == -EPIPE)
        {
            snd_pcm_prepare(pcm);
        }
        else if (written < 0)
        {
            LOG_WARN("ALSA write error: %s", snd_strerror(written));
            break;
        }
    }

    LOG_OK("Stream ended");
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
}

// arg parsing

typedef struct {
    const char *audio;
    int         rate;
    int         channels;
    int         loop;
    int         raw;
} Args;

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s -audio <file|-> [-rate N] [-channels N] [-loop] [-raw]\n"
            prog);
    exit(1);
}

static Args parse_args(int argc, char **argv) {
    Args a = { .rate = 48000, .channels = 2 };

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-audio")    && i+1 < argc) a.audio    = argv[++i];
        else if (!strcmp(argv[i], "-rate")     && i+1 < argc) a.rate     = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-channels") && i+1 < argc) a.channels = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-loop"))                   a.loop     = 1;
        else if (!strcmp(argv[i], "-raw"))                    a.raw      = 1;
    }

    if (!a.audio) usage(argv[0]);
    return a;
}

// entry point

int main(int argc, char **argv)
{
    Args a = parse_args(argc, argv);

    if (!strcmp(a.audio, "-"))
    {
        stream_stdin(a.rate, a.channels);
        return 0;
    }

    if (a.raw)
    {
        // raw PCM: just stream the file through ALSA directly
        LOG_INFO("Loading raw PCM file: %s", a.audio);
        FILE *f = fopen(a.audio, "rb");
        if (!f)
        {
            LOG_ERR("Cannot open file: %s", a.audio);
            return 1;
        }

        snd_pcm_t *pcm = alsa_open(a.rate, a.channels);
        if (!pcm)
        {
            fclose(f);
            return 1;
        }

        short buf[PERIOD_SIZE];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        {
            snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf, n / (sizeof(short) * a.channels));
            if (written == -EPIPE)
                snd_pcm_prepare(pcm);
        }

        LOG_OK("Playback finished");
        fclose(f);
        snd_pcm_drain(pcm);
        snd_pcm_close(pcm);
        return 0;
    }

    LOG_INFO("Loading audio file: %s", a.audio);
    play_file(a.audio, a.loop);
    return 0;
}