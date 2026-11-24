#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "gfxprim.h"
#include "libretro.h"
#include "libretro-core-options.h"

static struct retro_log_callback logging;
static retro_log_printf_t log_cb;
static retro_perf_get_time_usec_t perf_get_time_usec;

/* Maps libretro joypad buttons to gfxprim keys. */
static const struct {
	unsigned retro_id;
	uint32_t gp_key;
	const char *desc;
} retro_joypad_map[] = {
	{ RETRO_DEVICE_ID_JOYPAD_LEFT,   GP_KEY_LEFT,        "Left" },
	{ RETRO_DEVICE_ID_JOYPAD_RIGHT,  GP_KEY_RIGHT,       "Right" },
	{ RETRO_DEVICE_ID_JOYPAD_UP,     GP_KEY_UP,          "Up" },
	{ RETRO_DEVICE_ID_JOYPAD_DOWN,   GP_KEY_DOWN,        "Down" },
	{ RETRO_DEVICE_ID_JOYPAD_A,      GP_KEY_A,           "A" },
	{ RETRO_DEVICE_ID_JOYPAD_B,      GP_KEY_B,           "B" },
	{ RETRO_DEVICE_ID_JOYPAD_X,      GP_KEY_X,           "X" },
	{ RETRO_DEVICE_ID_JOYPAD_Y,      GP_KEY_Y,           "Y" },
	{ RETRO_DEVICE_ID_JOYPAD_SELECT, GP_KEY_RIGHT_SHIFT, "Right Shift" },
	{ RETRO_DEVICE_ID_JOYPAD_START,  GP_KEY_ENTER,       "Enter" },
	{ RETRO_DEVICE_ID_JOYPAD_L,      GP_KEY_PAGE_UP,     "Page Up" },
	{ RETRO_DEVICE_ID_JOYPAD_R,      GP_KEY_PAGE_DOWN,   "Page Down" },
};

struct gfxprim_core {
	gp_backend *backend;
	gp_ev_queue ev_queue;

	int16_t mouseLeft, mouseRight;
	int16_t mouseX, mouseY;
	int16_t joypad[GP_ARRAY_SIZE(retro_joypad_map)];
	enum gp_pixel_type pixelType;

	/* Keyboard events buffered from the frontend's keyboard callback. */
	struct {
		uint32_t key;
		uint32_t character;
		uint8_t down;
	} keyEvents[GP_EVENT_QUEUE_SIZE];
	unsigned int keyEventsHead, keyEventsTail;
};

static struct gfxprim_core *core;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
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

static void check_variables(void) {
	if (!core)
		return;

	struct retro_variable var = {0};

	var.key = "gfxprim_pixelformat";
	var.value = NULL;
	core->pixelType = GP_PIXEL_RGB565;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
		if (strcmp(var.value, "32 Bit") == 0)
			core->pixelType = GP_PIXEL_xRGB8888;
	}
}

static void retro_flip(gp_backend *self) {
	gp_pixmap *pixmap = self->pixmap;

	switch (pixmap->pixel_type) {
		case GP_PIXEL_xRGB8888:
			video_cb(pixmap->pixels, pixmap->w, pixmap->h, pixmap->w << 2);
			break;
		case GP_PIXEL_RGB565:
		default:
			video_cb(pixmap->pixels, pixmap->w, pixmap->h, pixmap->w * sizeof(uint16_t));
			break;
	}
}

static void retro_poll_mouse(gp_backend *self, uint64_t time) {
	int16_t state = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
	if (state != core->mouseLeft) {
		core->mouseLeft = state;
		gp_ev_queue_push_key(self->event_queue, GP_BTN_LEFT, (uint8_t)state, 0, time);
	}

	state = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
	if (state != core->mouseRight) {
		core->mouseRight = state;
		gp_ev_queue_push_key(self->event_queue, GP_BTN_RIGHT, (uint8_t)state, 0, time);
	}

	// TODO: Support for Pointer API in addition to Mouse.
	int16_t mouseX = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
	int16_t mouseY = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
	if (mouseX != 0 || mouseY != 0) {
		core->mouseX += mouseX;
		core->mouseY += mouseY;
		if (core->mouseX < 0)
			core->mouseX = 0;
		else if (core->mouseX >= (int16_t)self->event_queue->screen_w)
			core->mouseX = self->event_queue->screen_w - 1;
		if (core->mouseY < 0)
			core->mouseY = 0;
		else if (core->mouseY >= (int16_t)self->event_queue->screen_h)
			core->mouseY = self->event_queue->screen_h - 1;
		gp_ev_queue_set_cursor_pos(self->event_queue, core->mouseX, core->mouseY);
	}
}

static const uint16_t retro_keymap[RETROK_LAST] = {
	[RETROK_BACKSPACE] = GP_KEY_BACKSPACE, [RETROK_TAB] = GP_KEY_TAB,
	[RETROK_RETURN] = GP_KEY_ENTER, [RETROK_PAUSE] = GP_KEY_PAUSE,
	[RETROK_ESCAPE] = GP_KEY_ESC, [RETROK_SPACE] = GP_KEY_SPACE,
	[RETROK_QUOTE] = GP_KEY_APOSTROPHE, [RETROK_COMMA] = GP_KEY_COMMA,
	[RETROK_MINUS] = GP_KEY_MINUS, [RETROK_PERIOD] = GP_KEY_DOT,
	[RETROK_SLASH] = GP_KEY_SLASH, [RETROK_SEMICOLON] = GP_KEY_SEMICOLON,
	[RETROK_EQUALS] = GP_KEY_EQUAL, [RETROK_LEFTBRACKET] = GP_KEY_LEFT_BRACE,
	[RETROK_BACKSLASH] = GP_KEY_BACKSLASH, [RETROK_RIGHTBRACKET] = GP_KEY_RIGHT_BRACE,
	[RETROK_BACKQUOTE] = GP_KEY_GRAVE, [RETROK_DELETE] = GP_KEY_DELETE,
	[RETROK_0] = GP_KEY_0, [RETROK_1] = GP_KEY_1, [RETROK_2] = GP_KEY_2,
	[RETROK_3] = GP_KEY_3, [RETROK_4] = GP_KEY_4, [RETROK_5] = GP_KEY_5,
	[RETROK_6] = GP_KEY_6, [RETROK_7] = GP_KEY_7, [RETROK_8] = GP_KEY_8,
	[RETROK_9] = GP_KEY_9,
	[RETROK_a] = GP_KEY_A, [RETROK_b] = GP_KEY_B, [RETROK_c] = GP_KEY_C,
	[RETROK_d] = GP_KEY_D, [RETROK_e] = GP_KEY_E, [RETROK_f] = GP_KEY_F,
	[RETROK_g] = GP_KEY_G, [RETROK_h] = GP_KEY_H, [RETROK_i] = GP_KEY_I,
	[RETROK_j] = GP_KEY_J, [RETROK_k] = GP_KEY_K, [RETROK_l] = GP_KEY_L,
	[RETROK_m] = GP_KEY_M, [RETROK_n] = GP_KEY_N, [RETROK_o] = GP_KEY_O,
	[RETROK_p] = GP_KEY_P, [RETROK_q] = GP_KEY_Q, [RETROK_r] = GP_KEY_R,
	[RETROK_s] = GP_KEY_S, [RETROK_t] = GP_KEY_T, [RETROK_u] = GP_KEY_U,
	[RETROK_v] = GP_KEY_V, [RETROK_w] = GP_KEY_W, [RETROK_x] = GP_KEY_X,
	[RETROK_y] = GP_KEY_Y, [RETROK_z] = GP_KEY_Z,
	[RETROK_KP0] = GP_KEY_KP_0, [RETROK_KP1] = GP_KEY_KP_1, [RETROK_KP2] = GP_KEY_KP_2,
	[RETROK_KP3] = GP_KEY_KP_3, [RETROK_KP4] = GP_KEY_KP_4, [RETROK_KP5] = GP_KEY_KP_5,
	[RETROK_KP6] = GP_KEY_KP_6, [RETROK_KP7] = GP_KEY_KP_7, [RETROK_KP8] = GP_KEY_KP_8,
	[RETROK_KP9] = GP_KEY_KP_9, [RETROK_KP_PERIOD] = GP_KEY_KP_DOT,
	[RETROK_KP_DIVIDE] = GP_KEY_KP_SLASH, [RETROK_KP_MULTIPLY] = GP_KEY_KP_ASTERISK,
	[RETROK_KP_MINUS] = GP_KEY_KP_MINUS, [RETROK_KP_PLUS] = GP_KEY_KP_PLUS,
	[RETROK_KP_ENTER] = GP_KEY_KP_ENTER, [RETROK_KP_EQUALS] = GP_KEY_KP_EQUAL,
	[RETROK_UP] = GP_KEY_UP, [RETROK_DOWN] = GP_KEY_DOWN,
	[RETROK_RIGHT] = GP_KEY_RIGHT, [RETROK_LEFT] = GP_KEY_LEFT,
	[RETROK_INSERT] = GP_KEY_INSERT, [RETROK_HOME] = GP_KEY_HOME,
	[RETROK_END] = GP_KEY_END, [RETROK_PAGEUP] = GP_KEY_PAGE_UP,
	[RETROK_PAGEDOWN] = GP_KEY_PAGE_DOWN,
	[RETROK_F1] = GP_KEY_F1, [RETROK_F2] = GP_KEY_F2, [RETROK_F3] = GP_KEY_F3,
	[RETROK_F4] = GP_KEY_F4, [RETROK_F5] = GP_KEY_F5, [RETROK_F6] = GP_KEY_F6,
	[RETROK_F7] = GP_KEY_F7, [RETROK_F8] = GP_KEY_F8, [RETROK_F9] = GP_KEY_F9,
	[RETROK_F10] = GP_KEY_F10, [RETROK_F11] = GP_KEY_F11, [RETROK_F12] = GP_KEY_F12,
	[RETROK_F13] = GP_KEY_F13, [RETROK_F14] = GP_KEY_F14, [RETROK_F15] = GP_KEY_F15,
	[RETROK_NUMLOCK] = GP_KEY_NUM_LOCK, [RETROK_CAPSLOCK] = GP_KEY_CAPS_LOCK,
	[RETROK_SCROLLOCK] = GP_KEY_SCROLL_LOCK,
	[RETROK_RSHIFT] = GP_KEY_RIGHT_SHIFT, [RETROK_LSHIFT] = GP_KEY_LEFT_SHIFT,
	[RETROK_RCTRL] = GP_KEY_RIGHT_CTRL, [RETROK_LCTRL] = GP_KEY_LEFT_CTRL,
	[RETROK_RALT] = GP_KEY_RIGHT_ALT, [RETROK_LALT] = GP_KEY_LEFT_ALT,
	[RETROK_RMETA] = GP_KEY_RIGHT_META, [RETROK_LMETA] = GP_KEY_LEFT_META,
	[RETROK_RSUPER] = GP_KEY_RIGHT_META, [RETROK_LSUPER] = GP_KEY_LEFT_META,
	[RETROK_COMPOSE] = GP_KEY_COMPOSE, [RETROK_SYSREQ] = GP_KEY_SYSRQ,
};

/* Called by the frontend, possibly from another thread; only buffers events. */
static void RETRO_CALLCONV retro_keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers) {
	(void)key_modifiers;
	if (!core)
		return;

	uint32_t key = keycode < RETROK_LAST ? retro_keymap[keycode] : 0;
	if (!down)
		character = 0;
	if (!key && !character)
		return;

	unsigned int next = (core->keyEventsHead + 1) % GP_ARRAY_SIZE(core->keyEvents);
	if (next == core->keyEventsTail)
		return;

	core->keyEvents[core->keyEventsHead].key = key;
	core->keyEvents[core->keyEventsHead].character = character;
	core->keyEvents[core->keyEventsHead].down = down;
	core->keyEventsHead = next;
}

static void retro_flush_keyboard(gp_backend *self, uint64_t time) {
	while (core->keyEventsTail != core->keyEventsHead) {
		unsigned int i = core->keyEventsTail;
		gp_ev_queue_push_key(self->event_queue, core->keyEvents[i].key,
			core->keyEvents[i].down ? GP_EV_KEY_DOWN : GP_EV_KEY_UP,
			core->keyEvents[i].character, time);
		core->keyEventsTail = (i + 1) % GP_ARRAY_SIZE(core->keyEvents);
	}
}

static void retro_poll_joypad(gp_backend *self, uint64_t time) {
	// TODO: Add actual gamepad support to GFXPrim?
	for (unsigned int i = 0; i < GP_ARRAY_SIZE(retro_joypad_map); i++) {
		int16_t state = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, retro_joypad_map[i].retro_id);
		if (state != core->joypad[i]) {
			core->joypad[i] = state;
			gp_ev_queue_push_key(self->event_queue, retro_joypad_map[i].gp_key, (uint8_t)state, 0, time);
		}
	}
}

static void retro_set_input_descriptors(void) {
	struct retro_input_descriptor desc[GP_ARRAY_SIZE(retro_joypad_map) + 1] = { 0 };
	for (unsigned int i = 0; i < GP_ARRAY_SIZE(retro_joypad_map); i++) {
		desc[i] = (struct retro_input_descriptor) {
			0, RETRO_DEVICE_JOYPAD, 0, retro_joypad_map[i].retro_id, retro_joypad_map[i].desc
		};
	}
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

static void retro_poll(gp_backend *self) {
	input_poll_cb();
	uint64_t time = gp_time_stamp();
	retro_poll_mouse(self, time);
	retro_poll_joypad(self, time);
	retro_flush_keyboard(self, time);
}

static void retro_exit(gp_backend *backend) {
	(void)backend;
	environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0);
}

static void retro_debug(const struct gp_debug_msg *msg) {
	int level = RETRO_LOG_INFO;
	switch (msg->level) {
		case GP_DEBUG_TODO:  level = RETRO_LOG_DEBUG; break;
		case GP_DEBUG_WARN:  level = RETRO_LOG_WARN;  break;
		case GP_DEBUG_BUG:   level = RETRO_LOG_ERROR; break;
		case GP_DEBUG_FATAL: level = RETRO_LOG_ERROR; break;
	}
	log_cb(level, "[GFXPrim]: %s (%s:%i)\n", msg->msg, msg->file, msg->line);
}

void retro_init(void) {
	if (core != NULL)
		return;

	core = calloc(1, sizeof(struct gfxprim_core));
	if (!core)
		return;

	core->pixelType = GP_PIXEL_RGB565;
}

void retro_deinit(void) {
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
	info->block_extract    = false;
	info->need_fullpath    = false;
	info->valid_extensions = NULL;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
	if (!core || !core->backend)
		return;

	info->timing = (struct retro_system_timing) {
		.fps = 60.0,
		.sample_rate = 0.0,
	};

	info->geometry = (struct retro_game_geometry) {
		.base_width   = core->backend->pixmap->w,
		.base_height  = core->backend->pixmap->h,
		.max_width    = core->backend->pixmap->w,
		.max_height   = core->backend->pixmap->h,
		.aspect_ratio = (float)core->backend->pixmap->w / (float)core->backend->pixmap->h,
	};
}

void retro_set_environment(retro_environment_t cb) {
	environ_cb = cb;

	libretro_set_core_options(cb);

	bool no_content = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;

	struct retro_perf_callback perf;
	if (cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf))
		perf_get_time_usec = perf.get_time_usec;
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
	audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
	(void)cb;
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
}

static void render(gp_pixmap *pixmap) {
	gp_pixel black  = gp_rgb_to_pixmap_pixel(10,  10,  10,  pixmap);
	gp_pixel red    = gp_rgb_to_pixmap_pixel(230, 57,  70,  pixmap);
	gp_pixel white  = gp_rgb_to_pixmap_pixel(245, 245, 245, pixmap);
	gp_pixel blue   = gp_rgb_to_pixmap_pixel(69,  123, 157, pixmap);
	gp_pixel orange = gp_rgb_to_pixmap_pixel(255, 161, 0,   pixmap);

	gp_fill(pixmap, white);

	gp_fill_rect(pixmap, 100, 100, 20, 40, red);
	gp_fill_circle(pixmap, 200, 150, 30, blue);
	gp_line(pixmap, 250, 50, 230, 130, blue);
	gp_fill_triangle(pixmap, 60, 200, 130, 180, 90, 150, orange);

	gp_text(pixmap, NULL, pixmap->w / 2, 10, GP_ALIGN_CENTER | GP_VALIGN_BELOW, black, 0, "Hello World!");

	gp_fill_circle(pixmap, core->mouseX, core->mouseY, 10, core->mouseLeft == 1 ? red : orange);
}

static void event_loop(gp_backend *backend) {
	while (gp_backend_ev_queued(backend)) {
		gp_event *ev = gp_backend_ev_get(backend);
		switch (ev->type) {
			case GP_EV_KEY:
				if (ev->code != GP_EV_KEY_DOWN)
					break;
				switch (ev->val) {
					case GP_BTN_LEFT:
					break;
				}
			break;
		}
	}
}

void retro_run(void) {
	if (!core || !core->backend)
		return;

	gp_backend_poll(core->backend);
	event_loop(core->backend);
	render(core->backend->pixmap);
	gp_backend_flip(core->backend);

	audio_cb(0, 0);

	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		check_variables();
}

bool retro_load_game(const struct retro_game_info *info) {
	(void)info;

	if (!core)
		return false;

	check_variables();
	gp_set_debug_handler(retro_debug);

	gp_backend *backend = calloc(1, sizeof(gp_backend));
	if (!backend)
		return false;

	backend->pixmap = gp_pixmap_alloc(400, 225, core->pixelType);
	if (!backend->pixmap) {
		free(backend);
		return false;
	}

	backend->event_queue = &core->ev_queue;
	gp_ev_queue_init(backend->event_queue, 400, 225, 0, NULL, NULL, 0);

	backend->name = "libretro";
	backend->flip = retro_flip;
	backend->poll = retro_poll;
	backend->exit = retro_exit;

	core->backend = backend;

	struct retro_keyboard_callback keyboard = { retro_keyboard_event };
	if (!environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard))
		log_cb(RETRO_LOG_WARN, "[GFXPrim]: Failed to set keyboard callback\n");

	retro_set_input_descriptors();

	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	if (core->backend->pixmap->pixel_type == GP_PIXEL_xRGB8888)
		fmt = RETRO_PIXEL_FORMAT_XRGB8888;

	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		log_cb(RETRO_LOG_ERROR, "[GFXPrim]: Failed to set pixel format %i\n", fmt);
		gp_pixmap_free(core->backend->pixmap);
		free(core->backend);
		core->backend = NULL;
		return false;
	}

	return true;
}

void retro_unload_game(void) {
	if (!core || !core->backend)
		return;

	gp_pixmap_free(core->backend->pixmap);
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

size_t retro_serialize_size(void) { return 0; }

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

void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {
	(void)index;
	(void)enabled;
	(void)code;
}

/*
 * gp_poll — FD-based polling is not used in the libretro backend.
 * The frontend drives the main loop, so all poll functions are no-ops.
 */
void   gp_poll_clear(gp_poll *self) { (void)self; }
int    gp_poll_add(gp_poll *self, gp_fd *fd) { (void)self; (void)fd; return 0; }
int    gp_poll_rem(gp_poll *self, gp_fd *fd) { (void)self; (void)fd; return 0; }
gp_fd *gp_poll_rem_by_fd(gp_poll *self, int fd) { (void)self; (void)fd; return NULL; }
int    gp_poll_wait(gp_poll *self, int timeout_ms) { (void)self; (void)timeout_ms; return 0; }

/*
 * gp_time_stamp — uses the libretro perf interface.
 * get_time_usec returns microseconds; divide by 1000 for milliseconds.
 */
uint64_t gp_time_stamp(void)
{
	if (perf_get_time_usec)
		return (uint64_t)perf_get_time_usec() / 1000;

	return 0;
}
