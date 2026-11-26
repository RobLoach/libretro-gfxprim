# libretro-gfxprim

A libretro core that acts as a [gfxprim](https://github.com/gfxprim/gfxprim) backend, so gfxprim applications render inside [RetroArch](https://www.retroarch.com) or any other libretro frontend. Video is sent to the frontend, and mouse, keyboard and joypad input is passed to gfxprim.

![screenshot.png](screenshot.png)

``` c
static int button_on_event(gp_widget_event *ev) {
	if (ev->type != GP_WIDGET_EVENT_WIDGET)
		return 0;
	gp_widget_label_printf(counter_label, "Clicks: %u", ++clicks);
	return 1;
}

void retro_run(void) {
	gp_event *ev;
	while ((ev = gp_backend_ev_poll(core->backend)))
		gp_widgets_event(ev, layout);
	gp_widgets_redraw(layout);
	gp_backend_flip(core->backend);
}
```

## Controls

- Mouse
	- Left, Right and Middle buttons map to `GP_BTN_LEFT`, `GP_BTN_RIGHT` and `GP_BTN_MIDDLE`
	- Mouse uses relative movement, clamped to the screen
	- Pointer uses absolute coordinates, with a press acting as the left button. Select it with the *Mouse Device* core option.
- Keyboard
	- The full keyboard is passed through the libretro keyboard callback, including typed characters
- Joypad (port 1)

	| RetroPad | gfxprim key |
	| -------- | ----------- |
	| D-Pad    | `GP_KEY_LEFT`, `GP_KEY_RIGHT`, `GP_KEY_UP`, `GP_KEY_DOWN` |
	| A        | `GP_KEY_A` |
	| B        | `GP_KEY_B` |
	| X        | `GP_KEY_X` |
	| Y        | `GP_KEY_Y` |
	| Select   | `GP_KEY_RIGHT_SHIFT` |
	| Start    | `GP_KEY_ENTER` |
	| L        | `GP_KEY_PAGE_UP` |
	| R        | `GP_KEY_PAGE_DOWN` |

## Core Options

- `gfxprim_pixelformat` - Pixel Format: `16 Bit` (default), `32 Bit`. Requires a restart.
- `gfxprim_resolution` - Resolution: `320x240`, `400x225` (default), `480x272`, `640x360`, `640x480`, `800x600`, `960x540`, `1280x720`, `1920x1080`. Requires a restart.
- `gfxprim_mouse_device` - Mouse Device: `Mouse` (default, relative movement), `Pointer` (absolute coordinates)

## Compile

1. Ensure dependencies are available:
	- Python

2. Clone the code:
    ```
	git clone --recursive https://github.com/RobLoach/libretro-gfxprim.git
	cd libretro-gfxprim
	```
	For an existing clone, fetch the submodules with `git submodule update --init`.

3. Compile
	```
	make
	```
4. Run
    ```
	retroarch -L gfxprim_libretro.so
	```

GitHub Actions builds `gfxprim_libretro.so` as an artifact for pushes to `master` and pull requests, see [.github/workflows/build.yml](.github/workflows/build.yml).
