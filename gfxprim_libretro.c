#include <stdarg.h>

#include "backends/gp_backend_virtual.h"
#include "gfxprim.h"

#include "libretro.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

struct gp_backend * backend;
int screenWidth, screenHeight;
float x, y;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static void fallback_log(enum retro_log_level level, const char *fmt, ...) {
	(void)level;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
}

void retro_init(void) {
	screenWidth = 400;
	screenHeight = 225;
	x = screenWidth / 2;
	y = screenHeight / 2;
	// TODO: Support GP_PIXEL_xRGB8888 in the settings.
	gp_pixel_type pixelType = GP_PIXEL_RGB565;
	struct gp_pixmap pixmap;
	struct gp_backend virtualBackend;
	pixmap.w = screenWidth;
	pixmap.h = screenHeight;
	virtualBackend.pixmap = &pixmap;
	virtualBackend.fd = -1;
	backend = gp_backend_virt_init(&virtualBackend, pixelType, 0);
}

void retro_deinit(void) {
	gp_backend_exit(backend);
	backend = NULL;
}

unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	log_cb(RETRO_LOG_INFO, "[GFXPrim]: Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info) {
	memset(info, 0, sizeof(*info));
	info->library_name     = "gfxprim";
	info->library_version  = "v0.0.1";
	info->block_extract    = true;
	info->need_fullpath    = false;
	info->valid_extensions = NULL; // Anything is fine, we don't care.
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
	float aspect = (float)screenWidth / (float)screenHeight;

	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = 0.0,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = screenWidth,
		.base_height  = screenHeight,
		.max_width    = screenWidth,
		.max_height   = screenHeight,
		.aspect_ratio = aspect
	};
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;

	bool no_content = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
	audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
	audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
	input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
	input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
	video_cb = cb;
}

void retro_reset(void) {
	// Reset
}

static void update_input(void)
{
	input_poll_cb();
	float speed = 0.1f;
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP)) {
		y-= speed;
	}
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN)) {
		y+= speed;
	}
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) {
		x+= speed;
	}
	if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT)) {
		x-= speed;
	}
}

static void render(void) {
	if (backend == NULL) {
		return;
	}

	gp_pixel red = gp_rgb_to_pixmap_pixel(230, 57, 70, backend->pixmap);
	gp_pixel white = gp_rgb_to_pixmap_pixel(241, 250, 238, backend->pixmap);
	gp_pixel blue = gp_rgb_to_pixmap_pixel(69, 123, 157, backend->pixmap);

	gp_fill(backend->pixmap, white);
	gp_fill_rect(backend->pixmap, 100, 100, 20, 40, red);
	gp_fill_circle(backend->pixmap, x, y, 30, blue);
	gp_line(backend->pixmap, 250, 10, 230, 130, blue);

	switch (backend->pixmap->pixel_type) {
		case GP_PIXEL_xRGB8888:
			video_cb(backend->pixmap->pixels, backend->pixmap->w, backend->pixmap->h, backend->pixmap->w << 2);
			break;
		case GP_PIXEL_RGB565:
			video_cb(backend->pixmap->pixels, backend->pixmap->w, backend->pixmap->h, backend->pixmap->w * sizeof(uint16_t));
			break;
	}
}

static void check_variables(void) {
	// Nothing.
}

static void audio_callback(void) {
	audio_cb(0, 0);
}

void retro_run(void) {
	update_input();
	render();
	audio_callback();

	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		check_variables();
	}
}

bool retro_load_game(const struct retro_game_info* info) {
	if (backend == NULL) {
		return false;
	}

	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	if (backend->pixmap->pixel_type == GP_PIXEL_xRGB8888) {
		fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	}

	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		log_cb(RETRO_LOG_ERROR, "[GFXPrim]: Failed to set pixel format\n");
		return false;
	}

	check_variables();

	(void)info;
	return true;
}

void retro_unload_game(void) {
	// Unload the game.
}

unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
	return retro_load_game(info);
}

size_t retro_serialize_size(void) {
	return 0;
}

bool retro_serialize(void *data_, size_t size) {
	return true;
}

bool retro_unserialize(const void *data_, size_t size) {
	return true;
}

void *retro_get_memory_data(unsigned id) {
	(void)id;
	return NULL;
}

size_t retro_get_memory_size(unsigned id) {
	(void)id;
	return 0;
}

void retro_cheat_reset(void) {
	// Cheat reset
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	(void)index;
	(void)enabled;
	(void)code;
}
