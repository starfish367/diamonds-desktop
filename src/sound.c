#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100

struct wav_header {
	char riff_tag[4];       /* "RIFF" */
	uint32_t riff_length;
	char wave_tag[4];       /* "WAVE" */
	char fmt_tag[4];        /* "fmt " */
	uint32_t fmt_length;
	uint16_t audio_format;  /* 1 = PCM */
	uint16_t num_channels;  /* 1 = mono */
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char data_tag[4];       /* "data" */
	uint32_t data_length;
} __attribute__((packed));

struct sound_buffer {
	uint8_t *data;
	size_t size;
};

static struct sound_buffer s_click = {0};
static struct sound_buffer s_snap = {0};
static struct sound_buffer s_error = {0};

static struct sound_buffer generate_pcm(
		float duration_sec,
		float (*waveform_fn)(float t, float duration)) {
	size_t num_samples = (size_t)(duration_sec * SAMPLE_RATE);
	size_t data_size = num_samples * sizeof(int16_t);
	size_t total_size = sizeof(struct wav_header) + data_size;

	uint8_t *buf = malloc(total_size);
	if (!buf) {
		struct sound_buffer empty = {0};
		return empty;
	}

	struct wav_header *hdr = (struct wav_header *)buf;
	memcpy(hdr->riff_tag, "RIFF", 4);
	hdr->riff_length = (uint32_t)(total_size - 8);
	memcpy(hdr->wave_tag, "WAVE", 4);
	memcpy(hdr->fmt_tag, "fmt ", 4);
	hdr->fmt_length = 16;
	hdr->audio_format = 1;
	hdr->num_channels = 1;
	hdr->sample_rate = SAMPLE_RATE;
	hdr->bits_per_sample = 16;
	hdr->byte_rate = SAMPLE_RATE * 1 * (16 / 8);
	hdr->block_align = 1 * (16 / 8);
	memcpy(hdr->data_tag, "data", 4);
	hdr->data_length = (uint32_t)data_size;

	int16_t *samples = (int16_t *)(buf + sizeof(struct wav_header));
	for (size_t i = 0; i < num_samples; i++) {
		float t = (float)i / (float)SAMPLE_RATE;
		float val = waveform_fn(t, duration_sec);
		if (val > 1.0f) val = 1.0f;
		if (val < -1.0f) val = -1.0f;
		samples[i] = (int16_t)(val * 32767.0f);
	}

	struct sound_buffer result = { .data = buf, .size = total_size };
	return result;
}

/* 1. Click: fast snappy 12ms pulse */
static float click_synth(float t, float dur) {
	(void)dur;
	float env = expf(-t * 220.0f);
	return sinf(2.0f * (float)M_PI * 1400.0f * t) * env * 0.45f;
}

/* 2. Snap: resonant low-end thump sweeping 240Hz -> 80Hz */
static float snap_synth(float t, float dur) {
	float env = expf(-t * 40.0f);
	float freq = 240.0f - (t / dur) * 160.0f;
	return sinf(2.0f * (float)M_PI * freq * t) * env * 0.65f;
}

/* 3. Error Chime: dual harmonic bell chord (880Hz + 1320Hz) */
static float error_synth(float t, float dur) {
	(void)dur;
	float env = expf(-t * 12.0f);
	float s1 = sinf(2.0f * (float)M_PI * 880.0f * t);
	float s2 = sinf(2.0f * (float)M_PI * 1320.0f * t);
	return (s1 * 0.6f + s2 * 0.4f) * env * 0.5f;
}

void diamonds_sound_init(void) {
	s_click = generate_pcm(0.015f, click_synth);
	s_snap = generate_pcm(0.070f, snap_synth);
	s_error = generate_pcm(0.220f, error_synth);
}

void diamonds_sound_play(diamonds_sound_type_t type) {
	struct sound_buffer *buf = NULL;
	switch (type) {
	case DIAMONDS_SOUND_CLICK: buf = &s_click; break;
	case DIAMONDS_SOUND_SNAP:  buf = &s_snap;  break;
	case DIAMONDS_SOUND_ERROR: buf = &s_error; break;
	}

	if (!buf || !buf->data || buf->size == 0) {
		return;
	}

	/* Clean up any zombie processes */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		return;
	}

	pid_t pid = fork();
	if (pid == 0) {
		/* Child process: feed aplay */
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);

		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}

		execlp("aplay", "aplay", "-q", "-t", "wav", (char *)NULL);
		_exit(1);
	} else if (pid > 0) {
		/* Parent process: write sound buffer into pipe non-blocking */
		close(pipefd[0]);
		ssize_t written = write(pipefd[1], buf->data, buf->size);
		(void)written;
		close(pipefd[1]);
	} else {
		close(pipefd[0]);
		close(pipefd[1]);
	}
}

void diamonds_sound_cleanup(void) {
	free(s_click.data);
	free(s_snap.data);
	free(s_error.data);
	s_click.data = NULL;
	s_snap.data = NULL;
	s_error.data = NULL;
}
