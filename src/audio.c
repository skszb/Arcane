#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.h"
#include "heap.h"

#include <stdio.h>

static ma_engine audio_engine;
static int audio_engine_initialized = 0;

#define AUDIO_CAPACITY 20

static struct
{
	audio_t* audio_clips[AUDIO_CAPACITY];
	int cur;
} audio_list;


int init_audio_engine()
{
	if (audio_engine_initialized != 0)
	{
		return 0;
	}
	audio_list.cur = 0;
	ma_result result = ma_engine_init(NULL, &audio_engine);
	if (result != MA_SUCCESS)
	{
		printf("Failed to initialize audio engine.\n");
		return -1;
	}
	audio_engine_initialized = 1;
	return 0;
}

void uninit_audio_engine()
{
	ma_engine_uninit(&audio_engine);
	for (int i = 0; i < audio_list.cur; i++)
	{
		audio_t* audio_clip = audio_list.audio_clips[i];
		heap_free(audio_clip->heap, audio_clip);
	}
	audio_list.cur = 0;
	audio_engine_initialized = 0;
}


audio_t* read_audio_file(heap_t* heap, char* file_path)
{
	if (audio_list.cur >= AUDIO_CAPACITY)
	{
		printf("Not enough space.\n");
		return NULL;
	}
	audio_t* audio_clip = heap_alloc(heap, sizeof(audio_t)+ sizeof(ma_sound), 8);
	ma_sound* audio_data = (ma_sound*)(audio_clip + 1);
	ma_result result = ma_sound_init_from_file(&audio_engine, file_path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, NULL, audio_data);

	if (result != MA_SUCCESS)
	{
		printf("Failed to read audio file at %s.\n", file_path);
		return NULL;
	}

	audio_clip->heap = heap;
	audio_clip->audio_data = audio_data;
	audio_clip->loop = 0;
	audio_clip->volume = 1.0f;

	audio_list.audio_clips[audio_list.cur++] = audio_clip;
	return audio_clip;
}

int play_audio(audio_t* audio_clip)
{
	if (audio_clip == NULL)
	{
		printf("Audio clip is null.\n");
		return -1;
	}

	ma_sound_set_volume(audio_clip->audio_data, audio_clip->volume);
	ma_result result = ma_sound_start(audio_clip->audio_data);
	if (result != MA_SUCCESS && result != MA_NOT_IMPLEMENTED)
	{
		printf("Failed to play audio clip.\n");
		return -1; 
	}
	return 0;
}

int pause_audio(audio_t* audio_clip) {
	ma_result result = ma_sound_stop(audio_clip->audio_data);
	if (result != MA_SUCCESS)
	{
		printf("Failed to pause audio clip.\n");
		return -1;  
	}
	return 0;
}

int stop_audio(audio_t* audio_clip)
{
	ma_result result = ma_sound_stop(audio_clip->audio_data);
	if (result != MA_SUCCESS)
	{
		printf("Failed to stop audio clip.\n");
		return -1;
	}

	result = ma_sound_seek_to_pcm_frame(audio_clip->audio_data, 0);
	if (result != MA_SUCCESS)
	{
		printf("Failed to stop audio clip.\n");
		return -1;
	}
	return 0;
}