#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_PATH APP_DATA_PATH("score.txt")
#define LEFT_DEBOUNCE_MS 100
#define RIGHT_REPEAT_MS 150
#define RESET_HOLD_MS 700

typedef struct {
    FuriMutex* mutex;
    uint32_t score;
    bool left_was_down;
    uint32_t last_left_ms;
    uint32_t last_right_ms;
    bool back_down;
    uint32_t back_start_ms;
    bool exit_requested;
    bool dirty;
} CounterState;

// Placeholder only: original 128-byte XBM graphic is not recoverable.
static const uint8_t my_icon_bits[128] = {0};

static void draw_callback(Canvas* canvas, void* ctx) {
    CounterState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    uint32_t score = state->score;
    furi_mutex_release(state->mutex);
    char value[24];
    snprintf(value, sizeof(value), "%lu", (unsigned long)score);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "The Application");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 2, 39, value);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 61, "Hold BACK: reset");
    canvas_draw_xbm(canvas, 86, 6, 32, 32, my_icon_bits);
}

static void input_callback(InputEvent* event, void* ctx) {
    CounterState* state = ctx;
    if(event->key != InputKeyBack) return;
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    if(event->type == InputTypePress) {
        state->back_down = true;
        state->back_start_ms = furi_get_tick();
    } else if(event->type == InputTypeRelease) {
        if(state->back_down && (furi_get_tick() - state->back_start_ms) < RESET_HOLD_MS) {
            state->exit_requested = true;
        }
        state->back_down = false;
    } else if(event->type == InputTypeLong) {
        state->score = 0;
        state->dirty = true;
        state->back_down = false;
    }
    furi_mutex_release(state->mutex);
}

static void save_score(Storage* storage, uint32_t score) {
    storage_common_mkdir(storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buffer[24];
        int len = snprintf(buffer, sizeof(buffer), "%lu\n", (unsigned long)score);
        storage_file_write(file, buffer, len);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static uint32_t load_score(Storage* storage) {
    uint32_t result = 0;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[24] = {0};
        uint16_t len = storage_file_read(file, buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';
        result = (uint32_t)strtoul(buffer, NULL, 10);
    }
    storage_file_close(file);
    storage_file_free(file);
    return result;
}

int32_t the_application_main(void* p) {
    UNUSED(p);
    CounterState* state = malloc(sizeof(CounterState));
    memset(state, 0, sizeof(CounterState));
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    state->score = load_score(storage);

    furi_hal_gpio_init(&gpio_ext_pa7, GpioModeInput, GpioPullUp, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_ext_pb2, GpioModeInput, GpioPullUp, GpioSpeedLow);

    ViewPort* viewport = view_port_alloc();
    view_port_draw_callback_set(viewport, draw_callback, state);
    view_port_input_callback_set(viewport, input_callback, state);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, viewport, GuiLayerFullscreen);

    while(true) {
        uint32_t now = furi_get_tick();
        bool left = !furi_hal_gpio_read(&gpio_ext_pa7);
        bool right = !furi_hal_gpio_read(&gpio_ext_pb2);
        bool exit_requested;
        bool changed = false;
        uint32_t score_to_save = 0;

        furi_mutex_acquire(state->mutex, FuriWaitForever);
        if(left && !state->left_was_down &&
           (!state->last_left_ms || now - state->last_left_ms >= LEFT_DEBOUNCE_MS)) {
            state->score++;
            state->last_left_ms = now;
            state->dirty = true;
        }
        state->left_was_down = left;
        if(right && (!state->last_right_ms || now - state->last_right_ms >= RIGHT_REPEAT_MS)) {
            state->score++;
            state->last_right_ms = now;
            state->dirty = true;
        }
        if(!right) state->last_right_ms = 0;
        if(state->back_down && now - state->back_start_ms >= RESET_HOLD_MS) {
            state->score = 0;
            state->dirty = true;
            state->back_down = false;
        }
        changed = state->dirty;
        if(changed) {
            score_to_save = state->score;
            state->dirty = false;
        }
        exit_requested = state->exit_requested;
        furi_mutex_release(state->mutex);

        if(changed) {
            save_score(storage, score_to_save);
            view_port_update(viewport);
        }
        if(exit_requested) break;
        furi_delay_ms(20);
    }

    gui_remove_view_port(gui, viewport);
    view_port_free(viewport);
    furi_record_close(RECORD_GUI);
    furi_hal_gpio_init(&gpio_ext_pa7, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(&gpio_ext_pb2, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(state->mutex);
    free(state);
    return 0;
}
