#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "frogger_game.h"
#include "debug.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>


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
} model_component_t;

typedef struct player_component_t
{
	int index;
} player_component_t;

typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct collider_component_t
{
	float y_cord;
	float z_cord;
	float width;
	float height;
} collider_component_t;

typedef struct traffic_component_t
{
	int row;
	int index;
	float width;
	float speed;
} traffic_component_t;

typedef struct frogger_game_t
{
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int traffic_type;
	int name_type;
	int collider_type;
	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t camera_ent;
	ecs_entity_ref_t** traffic_ent;

	gpu_mesh_info_t cube_mesh;
	gpu_mesh_info_t prism_mesh;
	gpu_shader_info_t cube_shader;
	gpu_shader_info_t prism_shader;
	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void spawn_traffic(frogger_game_t* game, int row, int index, bool start);
static void spawn_camera(frogger_game_t* game);
static void update_players(frogger_game_t* game);
static void update_traffic(frogger_game_t* game);
static void update_collisions(frogger_game_t* game);
static void draw_models(frogger_game_t* game);

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
	game->traffic_type = ecs_register_component_type(game->ecs, "traffic", sizeof(traffic_component_t), _Alignof(traffic_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	game->collider_type = ecs_register_component_type(game->ecs, "collider", sizeof(collider_component_t), _Alignof(collider_component_t));

	load_resources(game);
	spawn_player(game, 0);
	int row_count[] = {0, 9, 12};
	game->traffic_ent = heap_alloc(game->heap, sizeof(ecs_entity_ref_t*) * 3, 8);
	for (int i = 0; i < 3; i++) {
		game->traffic_ent[i] = heap_alloc(game->heap, sizeof(ecs_entity_ref_t) * (18 - row_count[i]), 8);
		for (int j = i; j < (18 - row_count[i]); j++) {
			spawn_traffic(game, i, j, true);
		}
	}
	spawn_camera(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game)
{
	for (int i = 0; i < 3; i++) {
		heap_free(game->heap, game->traffic_ent[i]);
	}
	heap_free(game->heap, game->traffic_ent);
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game)
{
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_players(game);
	update_traffic(game);
	update_collisions(game);
	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game)
{
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);

	game->cube_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};
	static vec3f_t cube_verts[] =
	{
		{ -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{  1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
		{ -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f,  0.0f },
	};
	static uint16_t cube_indices[] =
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
	game->cube_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};

	game->prism_shader = game->cube_shader;
	static vec3f_t prism_verts[] =
	{	//vertex position		  //vertex color
		{ -1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{  1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f,  0.0f },
		{  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f,  0.0f },
		{  1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f,  0.0f },
		{ -1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f,  0.0f },
	};
	game->prism_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = prism_verts,
		.vertex_data_size = sizeof(prism_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void unload_resources(frogger_game_t* game)
{
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type) |
		(1ULL << game->collider_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.z = 10.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;

	collider_component_t* collider_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->collider_type, true);
	collider_comp->y_cord = transform_comp->transform.translation.y;
	collider_comp->z_cord = transform_comp->transform.translation.z;
	collider_comp->width = 2;
	collider_comp->height = 2;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->cube_mesh;
	model_comp->shader_info = &game->cube_shader;
}

static void spawn_traffic(frogger_game_t* game, int row, int index, bool start)
{
	uint64_t k_traffic_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->traffic_type) |
		(1ULL << game->name_type) |
		(1ULL << game->collider_type);

	game->traffic_ent[row][index] = ecs_entity_add(game->ecs, k_traffic_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->traffic_ent[row][index], game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.scale.y = (row + 1) * 1.5f;
	float widths[] = { 6.0f, 12.0f, 18.0f };
	transform_comp->transform.translation.y = start ?  (widths[row] * index) - 27 : (row % 2 == 0 ? -28.0f : 28.0f);
	transform_comp->transform.translation.z = -5.0f * row;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->traffic_ent[row][index], game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "traffic");

	traffic_component_t* traffic_comp = ecs_entity_get_component(game->ecs, game->traffic_ent[row][index], game->traffic_type, true);
	traffic_comp->row = row;
	traffic_comp->index = index;
	traffic_comp->width = (row + 1) * 3.0f;
	traffic_comp->speed = row % 2 == 0 ? (row + 1) * 3.0f : (row + 1) * -3.0f;

	collider_component_t* collider_comp = ecs_entity_get_component(game->ecs, game->traffic_ent[row][row], game->collider_type, true);
	collider_comp->y_cord = transform_comp->transform.translation.x;
	collider_comp->z_cord = transform_comp->transform.translation.y;
	collider_comp->width = (row + 1) * 3.0f;
	collider_comp->height = 2.0f;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->traffic_ent[row][index], game->model_type, true);
	model_comp->mesh_info = &game->prism_mesh;
	model_comp->shader_info = &game->prism_shader;
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, 56.0f, 30.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		if (transform_comp->transform.translation.z < -14.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
			spawn_player(game, 0);
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt*5));
		}
		if (key_mask & k_key_down && transform_comp->transform.translation.z < 10.0f)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt*5));
		}
		if (key_mask & k_key_left && transform_comp->transform.translation.y > -15.0f)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt*5));
		}
		if (key_mask & k_key_right && transform_comp->transform.translation.y < 15.0f)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt*5));
		}
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void update_traffic(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->traffic_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		traffic_component_t* traffic_comp = ecs_query_get_component(game->ecs, &query, game->traffic_type);

		if (transform_comp->transform.translation.y > 28.0f || transform_comp->transform.translation.y < -28.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
			spawn_traffic(game, traffic_comp->row, traffic_comp->index, false);
		}

		transform_t move;
		transform_identity(&move);
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt * traffic_comp->speed));
		transform_multiply(&transform_comp->transform, &move);
	}
}

static void update_collisions(frogger_game_t* game)
{
	uint64_t k_query_mask = (1ULL << game->player_type);

	player_component_t* player_comp = NULL;
	collider_component_t* player_collider_comp = NULL;
	ecs_query_t player_query;

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);
		player_collider_comp = ecs_query_get_component(game->ecs, &query, game->collider_type);
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_collider_comp->y_cord = transform_comp->transform.translation.y;
		player_collider_comp->z_cord = transform_comp->transform.translation.z;
		player_query = query;
	}

	k_query_mask = (1ULL << game->transform_type) | (1ULL << game->collider_type) | (1ULL << game->traffic_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		collider_component_t* collider_comp = ecs_query_get_component(game->ecs, &query, game->collider_type);
		collider_comp->y_cord = transform_comp->transform.translation.y;
		collider_comp->z_cord = transform_comp->transform.translation.z;

		float y1 = player_collider_comp->y_cord;
		float z1 = player_collider_comp->z_cord;
		float y2 = y1 + player_collider_comp->width;
		float z2 = z1 - player_collider_comp->height;
		float y3 = collider_comp->y_cord;
		float z3 = collider_comp->z_cord;
		float y4 = y3 + collider_comp->width;
		float z4 = z3 - collider_comp->height;
		bool dead = false;

		if (y1 > y3 && y1 < y4 &&  z1 < z3 && z1 > z4) {
			dead = true;
		}
		else if (y1 > y3 && y1 < y4 && z2 < z3 && z2 > z4) {
			dead = true;
		}
		else if (y2 > y3 && y2 < y4 && z1 < z3 && z1 > z4) {
			dead = true;
		}
		else if (y2 > y3 && y2 < y4 && z2 < z3 && z2 > z4) {
			dead = true;
		}
		
		if (dead) {
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &player_query), false);
			spawn_player(game, 0);
		}
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
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}
