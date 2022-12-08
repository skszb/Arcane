#pragma once
typedef struct heap_t heap_t;

typedef struct ma_engine ma_engine;
typedef struct ma_sound ma_sound;

typedef struct audio_t
{
	heap_t* heap;
	ma_sound* audio_data;
	int loop;
	float volume;
} audio_t;




int init_audio_engine();

void uninit_audio_engine();

// Supports .wav .mp3 .flac
audio_t* read_audio_file(heap_t* heap, char* file_path);

int play_audio(audio_t* audio_clip);

int pause_audio(audio_t* audio_clip);

int stop_audio(audio_t* audio_clip);










