# libretro-gfxprim

Perhaps a [gfxprim](https://github.com/gfxprim/gfxprim) libretro backend core?

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

## Compile

1. Ensure dependencies are available:
	- Python

2. Clone the code:
    ```
	git clone --recursive https://github.com/RobLoach/libretro-gfxprim.git
	cd libretro-gfxprim
	```

3. Compile
	```
	make
	```
4. Run
    ```
	retroarch -L gfxprim_libretro.so
	```
