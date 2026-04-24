/*
 * sounds.h — Audio data stub for ViviSense firmware
 *
 * This file contains empty placeholders so the firmware compiles immediately.
 * To add real audio, copy the files from webserver/audio_files/ into this
 * project, run  python webserver/wav_to_header.py  and replace this file
 * with the generated output.
 *
 * Required WAV format: 16-bit PCM, mono, 16 kHz recommended.
 */

#pragma once
#include <pgmspace.h>

// ── stop ─────────────────────────────────────────────────────────────────────
const unsigned int  stop_rate = 16000;
const unsigned int  stop_len  = 0;
const unsigned char stop_data[] PROGMEM = {0};

// ── go_forward ────────────────────────────────────────────────────────────────
const unsigned int  go_forward_rate = 16000;
const unsigned int  go_forward_len  = 0;
const unsigned char go_forward_data[] PROGMEM = {0};

// ── turn_left ─────────────────────────────────────────────────────────────────
const unsigned int  turn_left_rate = 16000;
const unsigned int  turn_left_len  = 0;
const unsigned char turn_left_data[] PROGMEM = {0};

// ── turn_right ────────────────────────────────────────────────────────────────
const unsigned int  turn_right_rate = 16000;
const unsigned int  turn_right_len  = 0;
const unsigned char turn_right_data[] PROGMEM = {0};

// ── speed_up ──────────────────────────────────────────────────────────────────
const unsigned int  speed_up_rate = 16000;
const unsigned int  speed_up_len  = 0;
const unsigned char speed_up_data[] PROGMEM = {0};

// ── slow_down ─────────────────────────────────────────────────────────────────
const unsigned int  slow_down_rate = 16000;
const unsigned int  slow_down_len  = 0;
const unsigned char slow_down_data[] PROGMEM = {0};

// ── back_up ───────────────────────────────────────────────────────────────────
const unsigned int  back_up_rate = 16000;
const unsigned int  back_up_len  = 0;
const unsigned char back_up_data[] PROGMEM = {0};
