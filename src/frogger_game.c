#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "audio.h"
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
	vec3f_t color;
} model_component_t;

typedef struct player_component_t
{
	int index;
} player_component_t;

typedef struct car_component_t
{
	int index;
} car_component_t;

typedef struct frogger_game_data_t
{
	// entities
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t traffic[3][5];

	// Player spawn(respawn) position
	vec3f_t player_spawn_pos;
	float player_finish_z;

	// Three traffic lanes spawn(respawn) position
	vec3f_t traffic_starts[3];
	float traffic_ends[3];
	vec3f_t traffic_velocity[3];
	vec3f_t car_size_min[3];
	vec3f_t car_size_max[3];

} frogger_game_data_t;

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	audio_t* bgm;
	audio_t* crash;
	audio_t* finish;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int car_type;

	frogger_game_data_t game_data;

	gpu_mesh_info_t square_mesh;
	gpu_shader_info_t square_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game);
static void spawn_traffic(frogger_game_t* game);
static void spawn_camera(frogger_game_t* game);
static void update_player(frogger_game_t* game);
static void update_traffic(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

// get the poition of the 4 corner vertices[top left, bottom left, top right, bottom right] that can define the square model and save it in ret_vec
static void get_corners(vec3f_t ret_vec[2], transform_t transform)
{
	
	vec3f_t player_translation = transform.translation;
	vec3f_t player_scale = transform.scale;

	ret_vec[0] = player_translation;
	ret_vec[1] = player_translation;
	ret_vec[2] = player_translation;
	ret_vec[3] = player_translation;

	ret_vec[0].y -= 1 * player_scale.y;
	ret_vec[0].z -= 1 * player_scale.z;
	ret_vec[1].y -= 1 * player_scale.y;
	ret_vec[1].z += 1 * player_scale.z;
	ret_vec[2].y += 1 * player_scale.y;
	ret_vec[2].z -= 1 * player_scale.z;
	ret_vec[3].y += 1 * player_scale.y;
	ret_vec[3].z += 1 * player_scale.z;
}


frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render)
{
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->car_type = ecs_register_component_type(game->ecs, "car", sizeof(car_component_t), _Alignof(car_component_t));

	// set player spawn position and finish line
	game->game_data.player_spawn_pos.z = 16.0f;
	game->game_data.player_finish_z = -16.0f;

	// set traffic lanes spawn and end points, and directions
	game->game_data.traffic_starts[0].y = -32.0f;
	game->game_data.traffic_starts[0].z = 10.0f;
	game->game_data.traffic_ends[0] = 32.0f;
	game->game_data.traffic_velocity[0] = vec3f_scale(vec3f_right(), 3.5f);
	game->game_data.car_size_min[0] = vec3f_one();
	game->game_data.car_size_max[0] = vec3f_one();
	game->game_data.car_size_min[0].y = 1.0f;
	game->game_data.car_size_max[0].y = 2.0f;

	game->game_data.traffic_starts[1].y = game->game_data.traffic_ends[0];
	game->game_data.traffic_starts[1].z = 3.0f;
	game->game_data.traffic_ends[1] = game->game_data.traffic_starts[0].y;
	game->game_data.traffic_velocity[1] = vec3f_scale(vec3f_right(), -1.0f);
	game->game_data.car_size_min[1] = vec3f_one();
	game->game_data.car_size_max[1] = vec3f_one();
	game->game_data.car_size_min[1].y = 1.5f;
	game->game_data.car_size_max[1].y = 5.0f;

	game->game_data.traffic_starts[2].y = game->game_data.traffic_starts[0].y;
	game->game_data.traffic_starts[2].z = -3.0f;
	game->game_data.traffic_ends[2] = game->game_data.traffic_ends[0];
	game->game_data.traffic_velocity[2] = vec3f_scale(vec3f_right(), 1.0f);
	game->game_data.car_size_min[2] = vec3f_one();
	game->game_data.car_size_max[2] = vec3f_one();
	game->game_data.car_size_min[2].y = 2.5f;
	game->game_data.car_size_max[2].y = 4.5f;

	init_audio_engine();
	game->bgm = read_audio_file(heap, "audios/bgm.mp3");
	game->bgm->loop = 1;
	game->bgm->volume = 2;
	game->crash = read_audio_file(heap, "audios/VOXScrm_Wilhelm scream (ID 0477)_BSB.wav");
	game->finish = read_audio_file(heap, "audios/success-fanfare-trumpets-6185.mp3");

	load_resources(game);
	spawn_player(game);
	spawn_traffic(game);
	spawn_camera(game);

	play_audio(game->bgm);
	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	uninit_audio_engine();
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_player(game);
	update_traffic(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	game->square_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t square_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  1.0f },
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f,  1.0f },
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f,  1.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 0.0f,  0.0f },
	};
	static uint16_t square_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};

	game->square_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = square_verts,
		.vertex_data_size = sizeof(square_verts),
		.index_data = square_indices,
		.index_data_size = sizeof(square_indices),
	};

}

static void unload_resources(frogger_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(frogger_game_t* game)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type);
	game->game_data.player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->game_data.player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation = game->game_data.player_spawn_pos;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->game_data.player_ent, game->model_type, true);
	model_comp->mesh_info = &game->square_mesh;
	model_comp->shader_info = &game->square_shader;
	model_comp->color.y = 1.0f;

}

static void spawn_traffic(frogger_game_t* game)
{
	uint64_t k_car_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->car_type);


	for (int c = 0; c < 5; c++)
	{

		for (int t = 0; t < 3; t++)
		{
			ecs_entity_ref_t car_ent = ecs_entity_add(game->ecs, k_car_ent_mask);
			game->game_data.traffic[t][c] = car_ent;

			transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);
			transform_identity(&transform_comp->transform);
			transform_comp->transform.translation = game->game_data.traffic_starts[t];

			int dir = (int) (game->game_data.traffic_velocity[t].y / fabs(game->game_data.traffic_velocity[t].y));
			float dist_between_lane = 12;
			transform_comp->transform.translation.y += dir * dist_between_lane * c ; 

			for (int i = 0; i < 3; i++)
			{

				/* 
				* The following code does not work and I don't know why, the scale seems to bypass the max function, resulting in a scale less than 1
				* 
				* transform_comp->transform.scale.a[i] = max((float)game->game_data.car_size_min[t].a[i], (float)game->game_data.car_size_max[t].a[i] * (float)rand() / RAND_MAX);
				*/

				float min = game->game_data.car_size_min[t].a[i];
				float max = game->game_data.car_size_max[t].a[i] * (float)rand() / RAND_MAX;
				transform_comp->transform.scale.a[i] = max(min, max);
			}


			model_component_t* model_comp = ecs_entity_get_component(game->ecs, car_ent, game->model_type, true);
			model_comp->mesh_info = &game->square_mesh;
			model_comp->shader_info = &game->square_shader;
			model_comp->color.x = (float)rand() / RAND_MAX;
			model_comp->color.y = (float)rand() / RAND_MAX;
			model_comp->color.z = (float)rand() / RAND_MAX;
		}
	}

}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type);
	game->game_data.camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->game_data.camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, -32.0f, 32.0f, 18.0f, -18.0f, 0.1f, 100.0f);
	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_player(frogger_game_t* game)
{
	ecs_entity_ref_t player = game->game_data.player_ent;
	transform_component_t* player_transform_comp = ecs_entity_get_component(game->ecs, player, game->transform_type, true);

	// respawn player when reaches finish line
	if (player_transform_comp->transform.translation.z < game->game_data.player_finish_z)
	{
		play_audio(game->finish);
		player_transform_comp->transform.translation = game->game_data.player_spawn_pos;
	}

	// respawn player is outside of screen
	if (player_transform_comp->transform.translation.z > game->game_data.player_spawn_pos.z)
	{
		player_transform_comp->transform.translation = game->game_data.player_spawn_pos;
	}
	 
	// get the poition of the 4 corner vertices of the player to compute collision

	vec3f_t player_corners[4];
	get_corners(player_corners, player_transform_comp->transform);

	// collision detection: loop through all cars and check if collide with the player
	int collide = false;
	for (int c = 0; c < 5; c++)
	{
		if (collide) { break; }
		for (int t = 0; t < 3; t++)
		{
			transform_component_t* car_transform_comp = ecs_entity_get_component(game->ecs, game->game_data.traffic[t][c], game->transform_type, true);
			vec3f_t car_corners[4];
			get_corners(car_corners, car_transform_comp->transform);
			for (int i = 0; i < 4; i++)
			{
				if (player_corners[i].y > car_corners[0].y &&
					player_corners[i].z > car_corners[0].z &&
					player_corners[i].y < car_corners[3].y &&
					player_corners[i].z < car_corners[3].z)
				{
					player_transform_comp->transform.translation = game->game_data.player_spawn_pos;
					play_audio(game->crash);
					collide = true;
					break;
				}
			}
		}
	}

	// player movement
	uint32_t key_mask = wm_get_key_mask(game->window);
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.01f;
	transform_t move;
	transform_identity(&move);
	if (key_mask & k_key_up)
	{
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
	}
	if (key_mask & k_key_down)
	{
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
	}
	if (key_mask & k_key_left)
	{
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
	}
	if (key_mask & k_key_right)
	{
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
	}
	transform_multiply(&player_transform_comp->transform, &move);

}

static void update_traffic(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.005f;
	for (int c = 0; c < 5; c++)
	{

		for (int t = 0; t < 3; t++)
		{
			transform_t move;
			transform_identity(&move);

			ecs_entity_ref_t car_ent = game->game_data.traffic[t][c];
			transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);

			move.translation = vec3f_add(move.translation, vec3f_scale(game->game_data.traffic_velocity[t], dt));
			transform_multiply(&transform_comp->transform, &move);

			if (fabs((double)transform_comp->transform.translation.y - game->game_data.traffic_ends[t]) <= 0.2f)
			{
				transform_comp->transform.translation = game->game_data.traffic_starts[t];
			}
		}
		//// Traffic lane 0
		//ecs_entity_ref_t car_ent = game->game_data.traffic_0[c];
		//transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);
		//move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		//transform_multiply(&transform_comp->transform, &move);
		//
		//if (transform_comp->transform.translation.y > game->game_data.traffic_ends[0])
		//{
		//	transform_comp->transform.translation = game->game_data.traffic_0_start;
		//}

		//// Traffic lane 1
		//car_ent = game->game_data.traffic_1[c];
		//transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);
		//move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), - dt - 0.1f));
		//transform_multiply(&transform_comp->transform, &move);
		//if (transform_comp->transform.translation.y < game->game_data.traffic_1_end)
		//{
		//	transform_comp->transform.translation = game->game_data.traffic_1_start;
		//}

		//// Traffic lane 2
		//car_ent = game->game_data.traffic_2[c];
		//transform_comp = ecs_entity_get_component(game->ecs, car_ent, game->transform_type, true);
		//move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt + 0.2f));
		//transform_multiply(&transform_comp->transform, &move);

		//if (transform_comp->transform.translation.y > game->game_data.traffic_2_end)
		//{
		//	transform_comp->transform.translation = game->game_data.traffic_starts[2];
		//}

	}
}

static void draw_models(frogger_game_t* game)
{
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
				vec3f_t color;
			} uniform_data;
			uniform_data.color = model_comp->color;


			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}