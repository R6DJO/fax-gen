/*
 * tiff2wav - Convert TIFF image to audio WAV for amateur radio facsimile (FAX)
 *
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Alexander (R6DJO)
 *
 * Copyright (c) 2026 Alexander (R6DJO)
 * Licensed under the MIT License - see the LICENSE file in the repository root.
 *
 * Uses FM modulation to encode a TIFF image as an audio signal suitable
 * for transmission/reception via HF radio. Outputs a 16-bit signed,
 * little-endian, mono WAV file at 8000 Hz sample rate.
 *
 * Usage: tiff2wav <input.tiff> [output.wav]
 *
 * Default parameters (matching WMO marine weather fax standard):
 *   carrier = 1900 Hz, deviation = ±400 Hz, LPM = 120, IOC = 576,
 *   phasing = 60 lines (WMO), APT start=300 Hz / 5s alternating FM,
 *   stop=450 Hz / 5s alternating FM + 10s black silence.
 */

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <tiffio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ----- configurable parameters (defaults match hamfax) ----- */

typedef struct {
    int       sample_rate;        /* 8000 Hz */
    double    carrier_freq;       /* 1900 Hz */
    double    deviation;          /* 400 Hz */
    int       lpm;                /* lines per minute = 120 */
    int       phasing_lines;      /* 60 phasing lines (WMO standard) */
    double    start_freq;         /* APT start tone, 300 Hz */
    int       start_dur_s;        /* 5 seconds */
    double    stop_freq;          /* APT stop tone, 450 Hz */
    int       stop_dur_s;         /* 5 seconds */
    double    threshold;          /* binary threshold for grayscale (0-255) */
    int       invert_image;       /* invert image before modulation */
} fax_params_t;

static fax_params_t p = {
    .sample_rate     = 8000,
    .carrier_freq    = 1900.0,
    .deviation       = 400.0,
    .lpm             = 120,
    .phasing_lines   = 60,
    .start_freq      = 300.0,
    .start_dur_s     = 5,
    .stop_freq       = 450.0,
    .stop_dur_s      = 5,
    .threshold       = 128,
    .invert_image    = 0
};

/* ----- TIFF image loading via libtiff ----- */

/* The RGBA staging buffer needs 4 bytes per pixel; cap the input so the
 * staging buffer stays at 256 MB and the grayscale buffer at 64 MB. */
#define MAX_IMAGE_PIXELS (64u * 1024u * 1024u)

typedef struct {
    uint8_t  *data;     /* row-major grayscale, width*height bytes */
    int       width;
    int       height;
} fax_image_t;

static int load_tiff(const char *path, fax_image_t *img)
{
    TIFF *tif = TIFFOpen(path, "r");
    if (!tif) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return -1;
    }

    uint32_t w = 0, h = 0;
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) || w == 0 || h == 0) {
        fprintf(stderr, "Error: missing or zero image dimensions in '%s'\n", path);
        TIFFClose(tif);
        return -1;
    }

    uint16_t spp = 1, bps = 1, photo = PHOTOMETRIC_MINISBLACK, planar = 1;
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photo);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);

    uint64_t npix = (uint64_t)w * (uint64_t)h;
    if (npix > MAX_IMAGE_PIXELS) {
        fprintf(stderr, "Error: image too large (%" PRIu64 " pixels, limit %u)\n",
                npix, MAX_IMAGE_PIXELS);
        TIFFClose(tif);
        return -1;
    }

    /*
     * Decode through libtiff's RGBA image interface rather than raw scanlines.
     * That interface applies the photometric interpretation and the palette,
     * and handles 1/2/4/8/16 bits per sample, contig and separated planar
     * configurations, alpha, YCbCr and tiled images. Doing the same by hand
     * from scanlines is what previously broke 1-bit bilevel FAX input.
     */
    uint32_t *rgba = calloc((size_t)npix, sizeof(uint32_t));
    if (!rgba) {
        fprintf(stderr, "Error: out of memory for %ux%u RGBA buffer\n", w, h);
        TIFFClose(tif);
        return -1;
    }

    if (!TIFFReadRGBAImageOriented(tif, w, h, rgba, ORIENTATION_TOPLEFT, 1)) {
        fprintf(stderr, "Error: failed to decode TIFF image '%s'\n", path);
        free(rgba);
        TIFFClose(tif);
        return -1;
    }
    TIFFClose(tif);

    img->width  = (int)w;
    img->height = (int)h;
    img->data   = calloc((size_t)npix, 1);
    if (!img->data) {
        fprintf(stderr, "Error: out of memory for %ux%u grayscale buffer\n", w, h);
        free(rgba);
        return -1;
    }

    for (uint64_t i = 0; i < npix; i++) {
        uint32_t px = rgba[i];
        int val;

        if (TIFFGetA(px) == 0) {
            val = 255;   /* fully transparent -> treat as paper (white) */
        } else {
            /* Rec. ITU-R BT.601 luma, rounded to 0..255 */
            val = (int)((0.299 * TIFFGetR(px) +
                         0.587 * TIFFGetG(px) +
                         0.114 * TIFFGetB(px)) + 0.5);
        }
        if (val < 0) val = 0;
        if (val > 255) val = 255;

        img->data[i] = (uint8_t)val;
    }

    free(rgba);

    printf("Loaded: %s  (%dx%d, %u bit/sample, %u sample(s)/px, "
           "photometric %u, planar %u)\n",
           path, img->width, img->height, bps, spp, photo, planar);
    return 0;
}

/* ----- WAV file writer (minimal, no external dependency) -----
 *
 * Writes a RIFF/WAV file with PCM 16-bit signed data. The RIFF/WAVE PCM
 * format is little-endian, so the sample bytes are stored low byte first.
 */

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static int write_wav(const char *path, const short *samples, long nsamples)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", path);
        return -1;
    }

    /* WAV header (44 bytes, RIFF format) */
    uint8_t header[44];
    uint32_t data_size = (uint32_t)(nsamples * 2);  /* 16-bit = 2 bytes/sample */

    /* RIFF header */
    memcpy(header + 0,  "RIFF", 4);
    put_le32(header + 4, 36 + data_size);   /* file size - 8 */
    memcpy(header + 8,  "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(header + 12, "fmt ", 4);
    put_le32(header + 16, 16);              /* sub-chunk size (PCM) */
    put_le16(header + 20, 1);               /* audio format: PCM */
    put_le16(header + 22, 1);               /* num channels: mono */
    put_le32(header + 24, p.sample_rate);
    put_le32(header + 28, (uint32_t)(p.sample_rate * 2)); /* byte rate */
    put_le16(header + 32, 2);               /* block align */
    put_le16(header + 34, 16);              /* bits per sample */

    /* data sub-chunk header */
    memcpy(header + 36, "data", 4);
    put_le32(header + 40, data_size);

    fwrite(header, 1, 44, fp);

    /* Write audio samples in little-endian order (required by RIFF/PCM) */
    for (long i = 0; i < nsamples; i++) {
        uint8_t buf[2];
        put_le16(buf, (uint16_t)samples[i]);
        fwrite(buf, 1, 2, fp);
    }

    fclose(fp);

    printf("Wrote: %s  (%ld samples, %.2f seconds)\n",
           path, nsamples, (double)nsamples / p.sample_rate);
    return 0;
}

/* ----- Main processing pipeline ----- */

/* Sample offset where FAX line k begins. Rounding the boundary - instead of
 * truncating every line to an integer length - keeps consecutive lines exactly
 * contiguous, so a fractional samples-per-line (e.g. -l 110) leaves no zero
 * gaps at the end of a line. */
static long line_offset(double samples_per_line, long k)
{
    return (long)((double)k * samples_per_line + 0.5);
}

static int process(const char *input_path, const char *output_path)
{
    /* Load TIFF image */
    fax_image_t img = {0};
    if (load_tiff(input_path, &img) < 0) return -1;

    printf("Image: %d columns x %d rows\n", img.width, img.height);

    int sample_rate = p.sample_rate;
    double spl = 60.0 * (double)sample_rate / (double)p.lpm; /* samples per image row */

    int phasing_lines = p.phasing_lines;
    double carrier_freq = p.carrier_freq;
    double deviation = p.deviation;

    /* Per-line FM phase increments: black and white levels */
    uint32_t black_inc   = (uint32_t)((carrier_freq - deviation) * 4294967296.0 / sample_rate);
    uint32_t white_inc   = (uint32_t)((carrier_freq + deviation) * 4294967296.0 / sample_rate);

    /* Calculate total number of samples needed:
     *   start tone + phasing lines + image rows + stop tone + 10s black silence
     */
    long start_samples = (long)(p.start_dur_s * sample_rate);
    long stop_samples  = (long)(p.stop_dur_s * sample_rate);
    long silence_samples = (long)(10.0 * sample_rate); /* WMO: 10 sec black after stop */
    long phasing_samples = line_offset(spl, phasing_lines);
    long image_samples   = line_offset(spl, img.height);

    long total_samples = start_samples + phasing_samples +
                         image_samples + stop_samples + silence_samples;

    printf("Parameters: carrier=%.0f Hz, deviation=%.0f Hz, LPM=%d, "
           "samples/line=%.2f, phasing=%d lines\n",
           carrier_freq, deviation, p.lpm, spl,
           phasing_lines);
    printf("Total audio length: %.2f sec (%ld samples)\n",
           (double)total_samples / sample_rate, total_samples);

    /* Allocate output buffer */
    short *output = calloc(1, (size_t)total_samples * sizeof(short));
    if (!output) {
        fprintf(stderr, "Error: out of memory for %ld samples\n", total_samples);
        free(img.data);
        return -1;
    }

    /* Phase accumulator for sine wave generation */
    uint32_t phase = 0;

    short *ptr = output;

    /* ====== PHASE 1: APT Start tone (FM-modulated, alternating black/white) ======
     * Standard: carrier modulated by alternate Black and White at start_freq Hz.
     * Alternates between carrier-deviation and carrier+deviation every half-period.
     */
    {
        double freq = p.start_freq;
        uint32_t half_period_samples = (uint32_t)(sample_rate / (2.0 * freq));
        if (half_period_samples < 1) half_period_samples = 1;

        printf("Phase 1/4: APT start tone %.0f Hz alternating FM (%.2f sec)\n",
               freq, (double)start_samples / sample_rate);

        for (long i = 0; i < start_samples; i++) {
            /* Alternate black/white at the specified frequency */
            uint32_t inc = ((i / half_period_samples) & 1u) ? white_inc : black_inc;

            double angle = ((double)(phase & 0xFFFFFFFFu) / 4294967296.0) * 2.0 * M_PI;
            ptr[i] = (short)(32767.0 * sin(angle));
            phase += inc;
        }
        ptr += start_samples;
    }

    /* ====== PHASE 2: Phasing lines (sync pattern) ====== */
    {
        printf("Phase 2/4: Phasing (%d lines, %.2f samples/line)\n",
               phasing_lines, spl);

        /* WMO phasing pattern: first 25 ms white, the rest black */
        long white_samples = (long)(0.025 * sample_rate);
        if (white_samples < 1) white_samples = 1;

        for (int pline = 0; pline < phasing_lines; pline++) {
            long s0 = line_offset(spl, pline);
            long s1 = line_offset(spl, pline + 1);

            for (long i = s0; i < s1; i++) {
                uint32_t inc = (i - s0 < white_samples) ? white_inc : black_inc;

                double angle = ((double)(phase & 0xFFFFFFFFu) / 4294967296.0) * 2.0 * M_PI;
                ptr[i] = (short)(32767.0 * sin(angle));

                phase += inc;
            }
        }
        ptr += phasing_samples;
    }

    /* ====== PHASE 3: Image data ====== */
    printf("Phase 3/4: Image data (%d rows, %d cols)\n",
           img.height, img.width);

    for (int row = 0; row < img.height; row++) {
        long s0 = line_offset(spl, row);
        long s1 = line_offset(spl, row + 1);
        long n  = s1 - s0;

        for (long i = 0; i < n; i++) {
            int col = (int)((double)i / (double)n * img.width);
            if (col >= img.width) col = img.width - 1;

            uint8_t pixel_val = img.data[row * img.width + col];

            /* Apply threshold for binary image */
            double input_val;
            if (pixel_val < p.threshold) {
                input_val = 0.0;   /* black -> low frequency */
            } else {
                input_val = 1.0;   /* white -> high frequency */
            }

            /* Invert if requested */
            if (p.invert_image) {
                input_val = 1.0 - input_val;
            }

            uint32_t inc = input_val < 0.5 ? black_inc : white_inc;

            double angle = ((double)(phase & 0xFFFFFFFFu) / 4294967296.0) * 2.0 * M_PI;
            ptr[s0 + i] = (short)(32767.0 * sin(angle));

            phase += inc;
        }
    }
    ptr += image_samples;

    /* ====== PHASE 4: APT Stop tone + silence ======
     * Standard: carrier modulated by alternate Black and White at stop_freq Hz for 5s,
     * followed by 10 seconds of black level (carrier - deviation).
     */
    {
        double freq = p.stop_freq;
        uint32_t half_period_samples = (uint32_t)(sample_rate / (2.0 * freq));
        if (half_period_samples < 1) half_period_samples = 1;

        long total_phase4 = stop_samples + silence_samples;

        printf("Phase 4/4: APT stop tone %.0f Hz alternating FM (%.2f sec) + 10s silence\n",
               freq, (double)stop_samples / sample_rate);

        for (long i = 0; i < total_phase4; i++) {
            if (i < stop_samples) {
                /* Alternating black/white FM modulation */
                uint32_t inc = ((i / half_period_samples) & 1u) ? white_inc : black_inc;
                double angle = ((double)(phase & 0xFFFFFFFFu) / 4294967296.0) * 2.0 * M_PI;
                ptr[i] = (short)(32767.0 * sin(angle));
                phase += inc;
            } else {
                /* 10 seconds of black level (carrier - deviation) */
                double angle = ((double)(phase & 0xFFFFFFFFu) / 4294967296.0) * 2.0 * M_PI;
                ptr[i] = (short)(32767.0 * sin(angle));
                phase += black_inc;
            }
        }
    }

    /* Write WAV */
    int rc = write_wav(output_path, output, total_samples);

    free(output);
    free(img.data);

    return (rc == 0) ? 0 : -1;
}

/* ----- Usage/help ----- */

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [options] <input.tiff> [output.wav]\n\n"
        "Convert a TIFF image to an audio WAV file suitable for\n"
        "amateur radio facsimile (FAX) transmission.\n\n"
        "Options:\n"
        "  -c, --carrier FREQ     Carrier frequency in Hz (default: %g)\n"
        "  -d, --deviation DEV    FM deviation in Hz  (default: %g)\n"
        "  -l, --lpm LPM          Lines per minute    (default: %d)\n"
        "  -p, --phasing N        Number of phasing lines (default: %d)\n"
        "  -s, --start FREQ       APT start tone freq (default: %g)\n"
        "  -S, --stop FREQ        APT stop tone freq  (default: %g)\n"
        "  -t, --threshold N      Binary threshold 0-255 (default: %d)\n"
        "  -i, --invert           Invert image polarity\n"
        "  -h, --help             Show this help\n",
        progname,
        p.carrier_freq, p.deviation,
        p.lpm, p.phasing_lines,
        p.start_freq, p.stop_freq,
        (int)p.threshold);
}

/* ----- main ----- */

int main(int argc, char *argv[])
{
    const char *input_path = NULL;
    const char *output_path = "fax_output.wav";

    static struct option long_opts[] = {
        {"carrier",   required_argument, 0, 'c'},
        {"deviation", required_argument, 0, 'd'},
        {"lpm",       required_argument, 0, 'l'},
        {"phasing",   required_argument, 0, 'p'},
        {"start",     required_argument, 0, 's'},
        {"stop",      required_argument, 0, 'S'},
        {"threshold", required_argument, 0, 't'},
        {"invert",    no_argument,       0, 'i'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:d:l:p:s:S:t:ih", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c': p.carrier_freq = atof(optarg); break;
        case 'd': p.deviation  = atof(optarg);  break;
        case 'l': p.lpm        = atoi(optarg);  break;
        case 'p': p.phasing_lines = atoi(optarg); break;
        case 's': p.start_freq = atof(optarg); break;
        case 'S': p.stop_freq  = atof(optarg); break;
        case 't': p.threshold  = (double)atoi(optarg); break;
        case 'i': p.invert_image = 1;            break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: no input TIFF file specified\n\n");
        usage(argv[0]);
        return 1;
    }

    input_path = argv[optind];
    if (optind + 1 < argc) {
        output_path = argv[optind + 1];
    }

    printf("tiff2wav - TIFF to WAV converter for amateur radio FAX\n");
    printf("=============================================\n\n");

    int rc = process(input_path, output_path);
    if (rc != 0) {
        fprintf(stderr, "Processing failed.\n");
        return 1;
    }

    printf("\nDone.\n");
    return 0;
}
