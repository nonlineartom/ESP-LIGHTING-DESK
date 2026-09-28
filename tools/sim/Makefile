# Host renderer: builds LVGL + the MeshDeck UI against stubbed app_core and
# renders PNG screenshots of every screen (see render.c).
LVGL   := ../../components/lvgl
CC     ?= gcc
CFLAGS := -O1 -g -std=gnu11 -w -DLV_CONF_INCLUDE_SIMPLE -I. -I$(LVGL) -I../../main -I../../main/ui -DMESHDECK_HOST_SIM
LV_SRCS := $(shell find $(LVGL)/src -name '*.c')
LV_OBJS := $(patsubst $(LVGL)/src/%.c,obj/lvgl/%.o,$(LV_SRCS))
UI_SRCS := $(wildcard ../../main/ui/*.c) ../../main/lights.c ../../main/products.c ../../main/scenes.c ../../main/telink.c
SIM_SRCS := render.c stubs.c

all: render

obj/lvgl/%.o: $(LVGL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

liblvgl.a: $(LV_OBJS)
	@ar rcs $@ $^

render: liblvgl.a $(UI_SRCS) $(SIM_SRCS)
	$(CC) $(CFLAGS) -Wall -o $@ $(SIM_SRCS) $(UI_SRCS) liblvgl.a -lm

clean:
	rm -rf obj liblvgl.a render out
.PHONY: all clean
