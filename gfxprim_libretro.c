#include <stdarg.h>

#include "backends/gp_backend_virtual.h"
#include "gfxprim.h"

#include "libretro.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

struct gfxprim_core {
	gp_backend * backend;
	struct {
		struct {
			bool left;
			bool right;
			int x;
			int y;
		} mouse;
	} input;
	struct {
		uint deltaMillseconds;
	} time;
} core;

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

void retro_flip(gp_backend *self) {
	switch (core.backend->pixmap->pixel_type) {
		case GP_PIXEL_xRGB8888:
			video_cb(core.backend->pixmap->pixels, core.backend->pixmap->w, core.backend->pixmap->h, core.backend->pixmap->w << 2);
			break;
		case GP_PIXEL_RGB565:
			video_cb(core.backend->pixmap->pixels, core.backend->pixmap->w, core.backend->pixmap->h, core.backend->pixmap->w * sizeof(uint16_t));
			break;
	}
}

/**
 * Check the input from the frontend, and queue the input into polled events.
 */
void retro_poll(gp_backend *self){
	struct timeval time;

	input_poll_cb();

	time.tv_sec = core.time.deltaMillseconds * 0.001;
	time.tv_usec = core.time.deltaMillseconds;

	enum gp_event_key_code pushed = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
	if (pushed != core.input.mouse.left) {
		gp_event_queue_push_key(&self->event_queue, GP_BTN_LEFT, pushed, &time);
		core.input.mouse.left = pushed;
	}
}

void retro_exit(gp_backend* backend) {
	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
}

void retro_debug(const struct gp_debug_msg *msg) {
	int level = RETRO_LOG_INFO;
	switch (msg->level) {
		case GP_DEBUG_TODO:
			level = RETRO_LOG_DEBUG;
			break;
		case GP_DEBUG_WARN:
			level = RETRO_LOG_WARN;
			break;
		case GP_DEBUG_BUG:
			level = RETRO_LOG_ERROR;
			break;
		case GP_DEBUG_FATAL:
			level = RETRO_LOG_ERROR;
			break;
	}
	log_cb(level, msg->msg);
}

void retro_init(void) {
	// TODO: Support GP_PIXEL_xRGB8888 in the settings.
	gp_pixel_type pixelType = GP_PIXEL_RGB565;
	struct gp_pixmap pixmap;
	struct gp_backend virtualBackend;
	pixmap.w = 400;
	pixmap.h = 225;
	virtualBackend.pixmap = &pixmap;
	virtualBackend.fd = -1;
	gp_set_debug_handler(retro_debug);
	core.backend = gp_backend_virt_init(&virtualBackend, pixelType, 0);
	core.backend->name = "libretro";
	core.backend->flip = retro_flip;
	core.backend->poll = retro_poll;
	core.backend->exit = retro_exit;
}

void retro_deinit(void) {
	if (core.backend == NULL) {
		return;
	}
	if (core.backend->pixmap != NULL) {
		gp_pixmap_free(core.backend->pixmap);
	}

	if (core.backend->timers != NULL) {
		free(core.backend->timers);
	}

	if (core.backend->clipboard_data != NULL) {
		free(core.backend->clipboard_data);
	}
	core.backend = NULL;
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
	if (core.backend == NULL) {
		return;
	}
	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = 0.0,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = core.backend->pixmap->w,
		.base_height  = core.backend->pixmap->h,
		.max_width    = core.backend->pixmap->w,
		.max_height   = core.backend->pixmap->h,
		.aspect_ratio = (float)core.backend->pixmap->w / (float)core.backend->pixmap->h
	};
}

void retro_frametime(retro_usec_t usec) {
	core.time.deltaMillseconds = usec;
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;

	bool no_content = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;

	struct retro_frame_time_callback callback;
	callback.callback = retro_frametime;
	callback.reference = 0;
	if (cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &callback)) {
		log_cb(RETRO_LOG_ERROR, "[GFXPrim]: Failed to set frame time callback");
	}
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

static void render(gp_pixmap* pixmap) {
	if (pixmap == NULL) {
		return;
	}

	gp_pixel red = gp_rgb_to_pixmap_pixel(230, 57, 70, pixmap);
	gp_pixel white = gp_rgb_to_pixmap_pixel(241, 250, 238, pixmap);
	gp_pixel blue = gp_rgb_to_pixmap_pixel(69, 123, 157, pixmap);

	gp_fill(pixmap, white);

	gp_fill_rect(pixmap, 100, 100, 20, 40, red);
	gp_fill_circle(pixmap, 200, 150, 30, blue);
	gp_line(pixmap, 250, 10, 230, 130, blue);
}

static void check_variables(void) {
	// Nothing.
}

static void audio_callback(void) {
	audio_cb(0, 0);
}

void event_loop(gp_backend* backend) {
	while (gp_backend_events(backend)) {
		gp_event *ev = gp_backend_get_event(backend);

		switch (ev->type) {
			case GP_EV_KEY: {
				if (ev->code != GP_EV_KEY_DOWN)
					break;

				switch (ev->val) {
					case GP_BTN_LEFT:
						gp_backend_exit(backend);
					break;
				}
			}
		}
	}
}

void retro_run(void) {
	if (core.backend == NULL) {
		return;
	}

	gp_backend_poll(core.backend);
	event_loop(core.backend);
	render(core.backend->pixmap);
	gp_backend_flip(core.backend);
	audio_callback();

	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		check_variables();
	}
}

bool retro_load_game(const struct retro_game_info* info) {
	if (core.backend == NULL) {
		return false;
	}

	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	if (core.backend->pixmap->pixel_type == GP_PIXEL_xRGB8888) {
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
