#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "backends/gp_backend_virtual.h"
#include "gfxprim.h"

#include "libretro.h"
#include "libretro-core-options.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

struct gfxprim_core {
	gp_backend * backend;

	uint64_t timeDeltaMilliseconds;
	int16_t mouseLeft, mouseRight;
	int16_t mouseX, mouseY;
	int16_t keyLeft, keyRight, keyUp, keyDown;
	int16_t keyA, keyB, keySelect, keyStart;
	enum gp_pixel_type pixelType;
};

struct gfxprim_core* core;

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

void check_variables(void) {
	if (!core) {
		return;
	}

	// Update core option from the libretro variables.
	struct retro_variable var = {0};

	// Pixel Format
	var.key = "gfxprim_pixelformat";
	var.value = NULL;
	core->pixelType = GP_PIXEL_RGB565;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (strcmp(var.value, "32 Bit") == 0) {
			core->pixelType = GP_PIXEL_xRGB8888;
		}
	}
}

void retro_flip(gp_backend *self) {
	switch (core->backend->pixmap->pixel_type) {
		case GP_PIXEL_xRGB8888:
			video_cb(core->backend->pixmap->pixels, core->backend->pixmap->w, core->backend->pixmap->h, core->backend->pixmap->w << 2);
			break;
		case GP_PIXEL_RGB565:
			video_cb(core->backend->pixmap->pixels, core->backend->pixmap->w, core->backend->pixmap->h, core->backend->pixmap->w * sizeof(uint16_t));
			break;
		default:
			log_cb(RETRO_LOG_WARN, "[GFXPrim]: Unknown pixel format: %i\n", core->backend->pixmap->pixel_type);
			break;
	}
}

/**
 * Input: Poll all the mouse data into GPXPrim Events.
 */
void retro_poll_mouse(gp_backend *self, struct timeval* time) {
	int16_t state = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
	if (state != core->mouseLeft) {
		core->mouseLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_BTN_LEFT, (uint8_t)state, time);
	}

	state = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
	if (state != core->mouseRight) {
		core->mouseRight = state;
		gp_event_queue_push_key(&self->event_queue, GP_BTN_RIGHT, (uint8_t)state, time);
	}

	// TODO: Support for Pointer API in addition to Mouse.
	state = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	int16_t mouseY = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	if (state != 0 || mouseY != 0) {
		core->mouseX += state;
		core->mouseY += mouseY;
		if (core->mouseX < 0) {
			core->mouseX = 0;
		}
		else if (core->mouseX >= self->event_queue.screen_w) {
			core->mouseX = self->event_queue.screen_w - 1;
		}
		if (core->mouseY < 0) {
			core->mouseY = 0;
		}
		else if (core->mouseY >= self->event_queue.screen_h) {
			core->mouseY = self->event_queue.screen_h - 1;
		}
		gp_event_queue_set_cursor_pos(&self->event_queue, core->mouseX, core->mouseY);
	}
}

/**
 * Input: Poll all the keyboard states into GFXPrim events.
 */
void retro_poll_keyboard(gp_backend *self, struct timeval* time) {
	// TODO: Add actual gamepad support to GFXPrim?
	// TODO: Add the actual keyboard input, rather than just gamepad input.
	// TODO: Clean this up to be in a loop.
	int16_t state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
	if (state != core->keyLeft) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_LEFT, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
	if (state != core->keyRight) {
		core->keyRight = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_RIGHT, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
	if (state != core->keyUp) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_UP, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
	if (state != core->keyDown) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_DOWN, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
	if (state != core->keyA) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_A, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
	if (state != core->keyB) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_B, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
	if (state != core->keySelect) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_RIGHT_SHIFT, (uint8_t)state, time);
	}
	state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
	if (state != core->keyStart) {
		core->keyLeft = state;
		gp_event_queue_push_key(&self->event_queue, GP_KEY_ENTER, (uint8_t)state, time);
	}
}

/**
 * Check the input from the frontend, and queue the input into polled events.
 */
void retro_poll(gp_backend *self){
	struct timeval time;

	input_poll_cb();

	time.tv_sec = core->timeDeltaMilliseconds * 0.001;
	time.tv_usec = core->timeDeltaMilliseconds;

	retro_poll_mouse(self, &time);
	retro_poll_keyboard(self, &time);
}

/**
 * GFXPrim Callback: Exit
 */
void retro_exit(gp_backend* backend) {
	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
}

/**
 * GFXPrim Callback: Debug Handler
 */
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
	log_cb(level, "[GFXPrim]: %s (%s:%i)\n", msg->msg, msg->file, msg->line);
}

void retro_init(void) {
	if (core == NULL) {
		core = malloc(sizeof(struct gfxprim_core));
		core->pixelType = GP_PIXEL_RGB565;
	}
}

void retro_deinit(void) {
	if (!core || core->backend == NULL) {
		return;
	}

	free(core);
	core = NULL;
}

unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	log_cb(RETRO_LOG_INFO, "[GFXPrim]: Plugging device %u into port %u\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info) {
	memset(info, 0, sizeof(*info));
	info->library_name     = "gfxprim";
	info->library_version  = "v0.0.1";
	info->block_extract    = true;
	info->need_fullpath    = false;
	info->valid_extensions = NULL;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
	if (core->backend == NULL) {
		return;
	}
	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = 0.0,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = core->backend->pixmap->w,
		.base_height  = core->backend->pixmap->h,
		.max_width    = core->backend->pixmap->w,
		.max_height   = core->backend->pixmap->h,
		.aspect_ratio = (float)core->backend->pixmap->w / (float)core->backend->pixmap->h
	};
}

void retro_frametime(retro_usec_t usec) {
	core->timeDeltaMilliseconds = usec;
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;

	// Configure the core options.
	libretro_set_core_options(cb);

	// No Game
	bool no_content = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

	// Logging
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;

	// Frame time callback
	/*
	struct retro_frame_time_callback callback;
	callback.callback = retro_frametime;
	callback.reference = 0;
	if (!cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &callback)) {
		log_cb(RETRO_LOG_WARN, "[GFXPrim]: Failed to set frame time callback\n");
	}
	*/
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
	gp_pixel black = gp_rgb_to_pixmap_pixel(10, 10, 10, pixmap);
	gp_pixel red = gp_rgb_to_pixmap_pixel(230, 57, 70, pixmap);
	gp_pixel white = gp_rgb_to_pixmap_pixel(245, 245, 245, pixmap);
	gp_pixel blue = gp_rgb_to_pixmap_pixel(69, 123, 157, pixmap);
	gp_pixel orange = gp_rgb_to_pixmap_pixel(255, 161, 0, pixmap);

	gp_fill(pixmap, white);

	gp_fill_rect(pixmap, 100, 100, 20, 40, red);
	gp_fill_circle(pixmap, 200, 150, 30, blue);
	gp_line(pixmap, 250, 50, 230, 130, blue);
	gp_fill_triangle(pixmap, 60, 200, 130, 180, 90, 150, orange);

	gp_text(pixmap, NULL, pixmap->w / 2, 10, GP_ALIGN_CENTER | GP_VALIGN_BELOW, black, 0, "Hello World!");

	gp_fill_circle(pixmap, core->mouseX, core->mouseY, 10, core->mouseLeft == 1 ? red : orange);
}

static void audio_callback(void) {
	// TODO: Add audio support
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
						// gp_backend_exit(backend);
					break;
				}
			}
		}
	}
}

void retro_run(void) {
	if (core == NULL || core->backend == NULL) {
		return;
	}

	// Input
	gp_backend_poll(core->backend);

	// Update the state of the core through an event loop
	event_loop(core->backend);

	// Render the state to the pixel map
	render(core->backend->pixmap);

	// Flip the pixel map to the screen
	gp_backend_flip(core->backend);

	// Process core audio
	audio_callback();

	// Check if any variables were updated
	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
		check_variables();
	}
}

bool retro_load_game(const struct retro_game_info* info) {
	if (core == NULL) {
		return false;
	}

	check_variables();

	// GFXPrim Backend
	struct gp_pixmap pixmap;
	pixmap.w = 400;
	pixmap.h = 225;
	struct gp_backend virtualBackend;
	virtualBackend.pixmap = &pixmap;
	virtualBackend.fd = -1;
	gp_set_debug_handler(retro_debug);
	core->backend = gp_backend_virt_init(&virtualBackend, core->pixelType, 0);
	if (core->backend == NULL) {
		return false;
	}

	core->backend->name = "libretro";
	core->backend->flip = retro_flip;
	core->backend->poll = retro_poll;
	core->backend->exit = retro_exit;

	// Set the pixel format.
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	if (core->backend->pixmap->pixel_type == GP_PIXEL_xRGB8888) {
		fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	}
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		log_cb(RETRO_LOG_ERROR, "[GFXPrim]: Failed to set pixel format %i\n", fmt);
		return false;
	}

	(void)info;
	return true;
}

void retro_unload_game(void) {
	if (!core || !core->backend) {
		return;
	}
	if (core->backend->pixmap != NULL) {
		gp_pixmap_free(core->backend->pixmap);
	}

	if (core->backend->timers != NULL) {
		free(core->backend->timers);
	}

	if (core->backend->clipboard_data != NULL) {
		free(core->backend->clipboard_data);
	}

	free(core->backend);
	core->backend = NULL;
}

unsigned retro_get_region(void) {
	return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
	(void)type;
	(void)num;
	return retro_load_game(info);
}

size_t retro_serialize_size(void) {
	return 0;
}

bool retro_serialize(void *data, size_t size) {
	(void)data;
	(void)size;
	return true;
}

bool retro_unserialize(const void *data, size_t size) {
	(void)data;
	(void)size;
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
