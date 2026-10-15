/* Host tool: PCM WAV -> CD-XA ADPCM (.xa, 2336-byte sectors).
 * Uses vendored libpsxav. Optional null-sector interleave for 1x/2x playback. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpsxav/libpsxav.h"

static void die(const char *msg) {
	fprintf(stderr, "wav2xa: %s\n", msg);
	exit(1);
}

static uint32_t rd_u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* Load little-endian PCM16 stereo or mono WAV. Resampling is caller's job. */
static int16_t *load_wav(const char *path, int *out_ch, int *out_rate, int *out_frames) {
	FILE *f = fopen(path, "rb");
	uint8_t hdr[12];
	uint8_t chunk[8];
	uint16_t format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	uint8_t *data = NULL;
	uint32_t data_size = 0;
	int16_t *pcm;
	int frames, i;

	if (!f)
		die("cannot open input");
	if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
		die("not a WAV file");

	while (fread(chunk, 1, 8, f) == 8) {
		uint32_t size = rd_u32(chunk + 4);
		if (!memcmp(chunk, "fmt ", 4)) {
			uint8_t *fmt = malloc(size);
			if (!fmt || fread(fmt, 1, size, f) != size)
				die("bad fmt chunk");
			format = rd_u16(fmt);
			channels = rd_u16(fmt + 2);
			rate = rd_u32(fmt + 4);
			bits = rd_u16(fmt + 14);
			free(fmt);
			if (size & 1)
				fseek(f, 1, SEEK_CUR);
		} else if (!memcmp(chunk, "data", 4)) {
			data = malloc(size);
			if (!data || fread(data, 1, size, f) != size)
				die("bad data chunk");
			data_size = size;
			if (size & 1)
				fseek(f, 1, SEEK_CUR);
		} else {
			fseek(f, (long)size + (size & 1), SEEK_CUR);
		}
	}
	fclose(f);

	if (!data || format != 1 || (channels != 1 && channels != 2) || bits != 16)
		die("need PCM16 mono/stereo WAV");

	frames = (int)(data_size / (channels * 2));
	pcm = malloc((size_t)frames * 2 * sizeof(int16_t));
	if (!pcm)
		die("out of memory");

	if (channels == 2) {
		memcpy(pcm, data, data_size);
	} else {
		for (i = 0; i < frames; i++) {
			int16_t s = (int16_t)rd_u16(data + i * 2);
			pcm[i * 2] = s;
			pcm[i * 2 + 1] = s;
		}
	}
	free(data);

	*out_ch = 2;
	*out_rate = (int)rate;
	*out_frames = frames;
	return pcm;
}

static void write_null_sector(FILE *out, int file_number) {
	uint8_t sector[2336];
	memset(sector, 0, sizeof(sector));
	/* Subheader twice: file, ch=0xFF (invalid), submode=0, coding=0 */
	sector[0] = (uint8_t)file_number;
	sector[1] = 0xFF;
	sector[2] = 0;
	sector[3] = 0;
	memcpy(sector + 4, sector, 4);
	fwrite(sector, 1, 2336, out);
}

int main(int argc, char **argv) {
	const char *in_path = NULL;
	const char *out_path = NULL;
	int interleave = 8; /* stereo 37800Hz @ 2x CD speed */
	int file_number = 1;
	int channel = 0;
	int frequency = 37800;
	int i, ch, rate, frames, sps, sector_count;
	int16_t *pcm;
	psx_audio_xa_settings_t settings;
	psx_audio_encoder_state_t state;
	FILE *out;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-i") && i + 1 < argc)
			interleave = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-F") && i + 1 < argc)
			file_number = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-C") && i + 1 < argc)
			channel = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			frequency = atoi(argv[++i]);
		else if (argv[i][0] == '-')
			die("usage: wav2xa [-f 18900|37800] [-i stride] [-F file] [-C chan] in.wav out.xa");
		else if (!in_path)
			in_path = argv[i];
		else if (!out_path)
			out_path = argv[i];
		else
			die("too many arguments");
	}
	if (!in_path || !out_path)
		die("usage: wav2xa [-f 18900|37800] [-i stride] [-F file] [-C chan] in.wav out.xa");
	if (interleave < 1 || (interleave > 1 && (interleave & (interleave - 1))))
		die("interleave must be 1 or a power of two");
	if (frequency != 18900 && frequency != 37800)
		die("frequency must be 18900 or 37800");

	pcm = load_wav(in_path, &ch, &rate, &frames);
	if (rate != frequency)
		fprintf(stderr, "wav2xa: warning: wav rate %d != %d (encode anyway)\n", rate, frequency);

	memset(&settings, 0, sizeof(settings));
	settings.format = PSX_AUDIO_XA_FORMAT_XA;
	settings.stereo = true;
	settings.frequency = frequency;
	settings.bits_per_sample = 4;
	settings.file_number = file_number;
	settings.channel_number = channel;

	sps = (int)psx_audio_xa_get_samples_per_sector(settings);
	memset(&state, 0, sizeof(state));

	out = fopen(out_path, "wb");
	if (!out)
		die("cannot open output");

	sector_count = 0;
	for (i = 0; i < frames; i += sps) {
		int n = frames - i;
		uint8_t sector[2352];
		int len, pad;

		if (n > sps)
			n = sps;
		len = psx_audio_xa_encode(settings, &state, pcm + i * 2, n, sector_count, sector);
		if (i + sps >= frames)
			psx_audio_xa_encode_finalize(settings, sector, len);
		if (len != 2336)
			die("unexpected sector size");
		fwrite(sector, 1, 2336, out);
		sector_count++;

		for (pad = 1; pad < interleave; pad++)
			write_null_sector(out, file_number);
	}

	/* Trailing nulls so the end-of-stream callback has room to fire. */
	for (i = 0; i < 16; i++)
		write_null_sector(out, file_number);

	fclose(out);
	free(pcm);
	fprintf(stderr, "wav2xa: %d audio sectors, interleave %d, %ld bytes\n",
		sector_count, interleave, (long)sector_count * 2336 * interleave + 16L * 2336);
	return 0;
}
