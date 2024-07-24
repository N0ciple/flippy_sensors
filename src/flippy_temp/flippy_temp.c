/**
 * @file flippy_temp_graph.c
 * @brief SHT30 temperature and humidity sensor application.
 *
 * This application reads and displays the temperature and humidity from a SHT30 sensor.
 * It also displays a graph showing the temperature trend over time using a circular buffer.
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include "sht30.h"
#include "circular_buffer.h"
#include "custom_u8g2_fonts.h"
#include "flippy_temp_icons.h"

#define TEMP_BUFFER_SIZE       (64 - 8) // 64 pixels height - 8 pixels for text
#define BUFFER_TIMER_UPDATE_MS 1000 //15 * 60000 // 15 minutes
#define HEADER_TIMER_UPDATE_MS 1000

typedef struct {
    CircularBuffer temp_buffer;
    FuriTimer* buffer_timer;
    FuriTimer* header_timer;
    FuriMessageQueue* event_queue;
    ViewPort* view_port;
    Gui* gui;
    bool is_running;
    bool info_screen_visible;
    Measurement current_measurement;
    File* log_file;
    Storage* storage;
    FuriThread* log_thread;
    FuriSemaphore* log_semaphore;
    char log_file_name[64]; // Buffer to store the log file name
} flippyTempContext;

/******************** Timer Callback ****************************/

static void buffer_timer_callback(void* ctx) {
    furi_assert(ctx);
    flippyTempContext* context = ctx;

    if(sht30_read(&context->current_measurement) && !context->info_screen_visible) {
        circular_buffer_put(&context->temp_buffer, &context->current_measurement);
        view_port_update(context->view_port);

        // Signal the logging thread to log the current measurement
        furi_semaphore_release(context->log_semaphore);

        NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
        notification_message(notifications, &sequence_blink_blue_100);
        furi_record_close(RECORD_NOTIFICATION);
    }
}

static void header_timer_callback(void* ctx) {
    furi_assert(ctx);
    flippyTempContext* context = ctx;

    if(sht30_read(&context->current_measurement)) {
        view_port_update(context->view_port);
    }
}

/******************** Logging Thread ****************************/

static int32_t log_thread_callback(void* ctx) {
    furi_assert(ctx);
    flippyTempContext* context = ctx;

    while(context->is_running) {
        // Wait for a signal to log data
        furi_semaphore_acquire(context->log_semaphore, FuriWaitForever);

        // Log the current measurement
        FURI_LOG_D("THREAD", "Logging current measurement to file");
        if(storage_file_open(
               context->log_file, context->log_file_name, FSAM_WRITE, FSOM_OPEN_APPEND)) {
            char log_entry[64];
            snprintf(
                log_entry,
                sizeof(log_entry),
                "%04d-%02d-%02d %02d:%02d:%02d,%.1f,%.0f\n",
                context->current_measurement.timestamp.year,
                context->current_measurement.timestamp.month,
                context->current_measurement.timestamp.day,
                context->current_measurement.timestamp.hour,
                context->current_measurement.timestamp.minute,
                context->current_measurement.timestamp.second,
                (double)context->current_measurement.temperature,
                (double)context->current_measurement.humidity);
            bool output = storage_file_write(context->log_file, log_entry, strlen(log_entry));
            storage_file_close(context->log_file);

            if(output) {
                FURI_LOG_D("FILE_SAVE", "Data saved to log file");
            } else {
                FURI_LOG_D("FILE_SAVE", "Failed to save data to log file");
            }
        }
    }

    return 0;
}

/******************** GUI Draw Callback ****************************/

/*------------------- Helper Functions -------------------*/

static void draw_icons(Canvas* canvas) {
    canvas_draw_icon(canvas, 0, 0, &I_thermo_16x9);
    canvas_draw_icon(canvas, 42, 0, &I_drop_16x9);
}

static void draw_current_measurement(Canvas* canvas, flippyTempContext* context) {
    char buffer[64];
    canvas_set_font(canvas, FontSecondary);
    snprintf(buffer, sizeof(buffer), "%.1f", (double)context->current_measurement.temperature);
    canvas_draw_str(canvas, 12, 12, buffer);
    snprintf(buffer, sizeof(buffer), "%.0f%%", (double)context->current_measurement.humidity);
    canvas_draw_str(canvas, 54, 12, buffer);
}

static void draw_current_time(Canvas* canvas) {
    char buffer[64];
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(buffer, sizeof(buffer), "%02d:%02d", now.hour, now.minute);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 128 - (6 * 5), 8, buffer);
}

static void draw_y_axis_legends(Canvas* canvas, float min_temp, float max_temp) {
    char buffer[64];
    canvas_set_font_direction(canvas, 1);
    canvas_set_custom_u8g2_font(canvas, u8g2_font_4x6_tf);
    snprintf(buffer, sizeof(buffer), "%.1f", (double)min_temp);
    canvas_draw_str(canvas, 0, 64 - 9 - 6, buffer);
    snprintf(buffer, sizeof(buffer), "%.1f", (double)max_temp);
    canvas_draw_str(canvas, 0, 21, buffer);
    canvas_set_font_direction(canvas, 0);
}

static void draw_x_axis_legends(Canvas* canvas, flippyTempContext* context, size_t buffer_size) {
    char buffer[64];
    DateTime timestamp_first = context->temp_buffer.buffer[context->temp_buffer.tail].timestamp;
    DateTime timestamp_middle =
        context->temp_buffer
            .buffer[(context->temp_buffer.tail + buffer_size / 2) % context->temp_buffer.max_size]
            .timestamp;
    DateTime timestamp_last =
        context->temp_buffer
            .buffer[(context->temp_buffer.tail + buffer_size - 1) % context->temp_buffer.max_size]
            .timestamp;

    snprintf(buffer, sizeof(buffer), "%02d:%02d", timestamp_first.hour, timestamp_first.minute);
    canvas_draw_str(canvas, 20, 64, buffer);

    snprintf(buffer, sizeof(buffer), "%02d:%02d", timestamp_middle.hour, timestamp_middle.minute);
    canvas_draw_str(canvas, 58, 64, buffer);

    snprintf(buffer, sizeof(buffer), "%02d:%02d", timestamp_last.hour, timestamp_last.minute);
    canvas_draw_str(canvas, 104, 64, buffer);
}

static void draw_temperature_graph(
    Canvas* canvas,
    flippyTempContext* context,
    int graph_width,
    int graph_height,
    int left_padding,
    int bottom_padding) {
    size_t buffer_size = circular_buffer_size(&context->temp_buffer);
    if(buffer_size > 0) {
        float min_temp = context->temp_buffer.buffer[context->temp_buffer.tail].temperature;
        float max_temp = context->temp_buffer.buffer[context->temp_buffer.tail].temperature;
        for(size_t i = 0; i < buffer_size; ++i) {
            float temp =
                context->temp_buffer
                    .buffer[(context->temp_buffer.tail + i) % context->temp_buffer.max_size]
                    .temperature;
            if(temp < min_temp) min_temp = temp;
            if(temp > max_temp) max_temp = temp;
        }

        float temp_range = max_temp - min_temp;
        if(temp_range == 0) temp_range = 1;

        float x_step = (float)graph_width / (buffer_size - 1);

        for(size_t i = 1; i < buffer_size; ++i) {
            float temp_prev =
                context->temp_buffer
                    .buffer[(context->temp_buffer.tail + i - 1) % context->temp_buffer.max_size]
                    .temperature;
            float temp_curr =
                context->temp_buffer
                    .buffer[(context->temp_buffer.tail + i) % context->temp_buffer.max_size]
                    .temperature;
            int x1 = (int)((i - 1) * x_step) + left_padding;
            int y1 =
                64 - bottom_padding - (int)((temp_prev - min_temp) / temp_range * (graph_height));
            int x2 = (int)(i * x_step) + left_padding;
            int y2 =
                64 - bottom_padding - (int)((temp_curr - min_temp) / temp_range * (graph_height));
            canvas_draw_line(canvas, x1, y1, x2, y2);
        }

        draw_y_axis_legends(canvas, min_temp, max_temp);
        draw_x_axis_legends(canvas, context, buffer_size);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 64 - 6 * 5, 64 - 6, "No data yet");
    }
}

static void draw_info_screen(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 12, "Flippy Temp");
    canvas_set_custom_u8g2_font(canvas, u8g2_font_4x6_tf);
    canvas_draw_str(canvas, 0, 22, "Data stored at:");
    canvas_draw_str(canvas, 0, 28, "SD:/apps_data/flippy_temp/*.csv");
    canvas_draw_str(canvas, 0, 34, "Long press back to exit");
    canvas_draw_str(canvas, 0, 40, "github.com/n0ciple/flippy_sensors");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 10, 60, "Press OK to continue");
}

/*------------------- Main Draw Callback ---------------------------*/

static void draw_callback(Canvas* canvas, void* ctx) {
    flippyTempContext* context = ctx;
    int bottom_padding = 6 + 1;
    int left_padding = 6 + 2;
    int graph_height = 43 - bottom_padding;
    int graph_width = 128 - left_padding * 2;

    canvas_clear(canvas);
    if(context->info_screen_visible) {
        draw_info_screen(canvas);
        return;
    }
    draw_icons(canvas);
    draw_current_measurement(canvas, context);
    draw_current_time(canvas);
    draw_temperature_graph(
        canvas, context, graph_width, graph_height, left_padding, bottom_padding);
}

/******************** Input Callback ****************************/

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    flippyTempContext* context = ctx;
    furi_message_queue_put(context->event_queue, input_event, FuriWaitForever);
}

/******************** Main Application ****************************/

static flippyTempContext* flippy_temp_context_alloc() {
    flippyTempContext* context = malloc(sizeof(flippyTempContext));
    context->view_port = view_port_alloc();
    view_port_draw_callback_set(context->view_port, draw_callback, context);
    view_port_input_callback_set(context->view_port, input_callback, context);

    context->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    context->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(context->gui, context->view_port, GuiLayerFullscreen);

    circular_buffer_init(&context->temp_buffer, TEMP_BUFFER_SIZE);

    context->buffer_timer =
        furi_timer_alloc(buffer_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(context->buffer_timer, BUFFER_TIMER_UPDATE_MS);

    context->header_timer =
        furi_timer_alloc(header_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(context->header_timer, HEADER_TIMER_UPDATE_MS);

    context->is_running = true;
    context->info_screen_visible = true;

    sht30_init();

    // Generate a unique file name with a timestamp
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        context->log_file_name,
        sizeof(context->log_file_name),
        APP_DATA_PATH("data_log_%04d%02d%02d_%02d%02d%02d.csv"),
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

    // Initialize storage and log file
    context->storage = furi_record_open(RECORD_STORAGE);
    context->log_file = storage_file_alloc(context->storage);
    if(!storage_file_open(
           context->log_file, context->log_file_name, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E("FILE_INIT", "Failed to open log file");
    } else {
        // Write CSV header
        char log_header[] = "Timestamp,Temperature (C),Humidity (%)\n";
        storage_file_write(context->log_file, log_header, strlen(log_header));
        storage_file_close(context->log_file);
    }

    // Initialize log semaphore and thread
    context->log_semaphore = furi_semaphore_alloc(1, 0); // Initial count 0
    context->log_thread = furi_thread_alloc();
    furi_thread_set_name(context->log_thread, "LogThread");
    furi_thread_set_stack_size(context->log_thread, 1024);
    furi_thread_set_context(context->log_thread, context);
    furi_thread_set_callback(context->log_thread, log_thread_callback);
    furi_thread_start(context->log_thread);

    return context;
}

static void flippy_temp_context_free(flippyTempContext* context) {
    // Stop the timers
    furi_timer_stop(context->buffer_timer);
    furi_timer_free(context->buffer_timer);
    furi_timer_stop(context->header_timer);
    furi_timer_free(context->header_timer);

    // Stop the log thread
    context->is_running = false;
    FURI_LOG_D("THREAD", "Waiting for log thread to finish");
    furi_semaphore_release(context->log_semaphore); // Wake up the logging thread to exit
    furi_thread_join(context->log_thread);
    FURI_LOG_D("THREAD", "Log thread finished");
    furi_thread_free(context->log_thread);

    // Free the semaphore
    furi_semaphore_free(context->log_semaphore);

    // Free the message queues
    furi_message_queue_free(context->event_queue);

    // Free the view port and GUI resources
    gui_remove_view_port(context->gui, context->view_port);
    view_port_free(context->view_port);
    furi_record_close(RECORD_GUI);

    // Free the circular buffer
    circular_buffer_free(&context->temp_buffer);

    // Close and free the log file and storage
    storage_file_free(context->log_file);
    furi_record_close(RECORD_STORAGE);

    // Free the context itself
    free(context);
}

int32_t flippy_temp_main(void* p) {
    UNUSED(p);
    flippyTempContext* context = flippy_temp_context_alloc();

    // fill the buffer with initial data
    if(sht30_read(&context->current_measurement)) {
        circular_buffer_put(&context->temp_buffer, &context->current_measurement);
        view_port_update(context->view_port);
        NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
        notification_message(notifications, &sequence_blink_blue_100);
    }

    InputEvent event;
    while(context->is_running) {
        if(furi_message_queue_get(context->event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.type == InputTypeLong && event.key == InputKeyBack) {
                context->is_running = false;
                // Wake up the logging thread to exit
                furi_semaphore_release(context->log_semaphore);
            }
            if(event.type == InputTypeShort && event.key == InputKeyOk &&
               context->info_screen_visible) {
                context->info_screen_visible = !context->info_screen_visible;
                view_port_update(context->view_port);
            }
        }
    }

    flippy_temp_context_free(context);
    return 0;
}
