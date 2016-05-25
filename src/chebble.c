#include "pebble.h"
#include "consts.h"

static Window *window;
char text[4000] = "Loading yo...";

ScrollLayer *scroll;
TextLayer *textlayer;
bool memo = false;

void accel_data_handler(AccelData *data, uint32_t num_samples) {

  GPoint point = scroll_layer_get_content_offset(scroll);

  int y = (int)data->y;

  point.y += y*.05;
  /*if (y < -500){

    point.y += 5;
  }
  else if (y > 500) point.y -= 5;*/

  scroll_layer_set_content_offset(scroll, point, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "value: %d point: %d", y, point.y);
}
void memo_load(Window *win){

 
  Layer *win_layer = window_get_root_layer(win);
  GRect bounds = layer_get_bounds(win_layer);

  scroll = scroll_layer_create(bounds);
  

  textlayer = text_layer_create(GRect(0, 0, 144, 1000));
  text_layer_set_text(textlayer, text);
  text_layer_set_font(textlayer, fonts_get_system_font(FONT_KEY_GOTHIC_18));


  scroll_layer_set_content_size(scroll, text_layer_get_content_size(textlayer));
  scroll_layer_add_child(scroll, text_layer_get_layer(textlayer));
  layer_add_child(win_layer, scroll_layer_get_layer(scroll));

  scroll_layer_set_click_config_onto_window(scroll, win);

  accel_data_service_subscribe(1, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
}

void memo_unload(Window *win){

  accel_data_service_unsubscribe();  
}

void memo_view(){

  if (memo){

     memo = false;
    window_stack_pop(true);
  }
  else {
    memo = true;
    Window *win = window_create();
    window_set_window_handlers(win, (WindowHandlers){
      .load = memo_load,
      .unload = memo_unload,
    });
    window_stack_push(win, true);
  }
}

#define TOTAL_IMAGE_SLOTS 4

#define NUMBER_OF_IMAGES 10

// These images are 72 x 84 pixels (i.e. a quarter of the display),
// black and white with the digit character centered in the image.
// (As generated by the `fonttools/font2png.py` script.)
const int IMAGE_RESOURCE_IDS[NUMBER_OF_IMAGES] = {
  RESOURCE_ID_IMAGE_NUM_0, RESOURCE_ID_IMAGE_NUM_1, RESOURCE_ID_IMAGE_NUM_2,
  RESOURCE_ID_IMAGE_NUM_3, RESOURCE_ID_IMAGE_NUM_4, RESOURCE_ID_IMAGE_NUM_5,
  RESOURCE_ID_IMAGE_NUM_6, RESOURCE_ID_IMAGE_NUM_7, RESOURCE_ID_IMAGE_NUM_8,
  RESOURCE_ID_IMAGE_NUM_9
};

static GBitmap *images[TOTAL_IMAGE_SLOTS];
static BitmapLayer *image_layers[TOTAL_IMAGE_SLOTS];

#define EMPTY_SLOT -1

// The state is either "empty" or the digit of the image currently in
// the slot--which was going to be used to assist with de-duplication
// but we're not doing that due to the one parent-per-layer
// restriction mentioned above.
static int image_slot_state[TOTAL_IMAGE_SLOTS] = {EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT, EMPTY_SLOT};

static void load_digit_image_into_slot(int slot_number, int digit_value) {
  /*

     Loads the digit image from the application's resources and
     displays it on-screen in the correct location.

     Each slot is a quarter of the screen.

   */

  // TODO: Signal these error(s)?

  if ((slot_number < 0) || (slot_number >= TOTAL_IMAGE_SLOTS)) {
    return;
  }

  if ((digit_value < 0) || (digit_value > 9)) {
    return;
  }

  if (image_slot_state[slot_number] != EMPTY_SLOT) {
    return;
  }

  image_slot_state[slot_number] = digit_value;
  images[slot_number] = gbitmap_create_with_resource(IMAGE_RESOURCE_IDS[digit_value]);
  GRect frame = (GRect) {
    .origin = { (slot_number % 2) * 72, (slot_number / 2) * 84 },
    .size = gbitmap_get_bounds(images[slot_number]).size
  };
  BitmapLayer *bitmap_layer = bitmap_layer_create(frame);
  image_layers[slot_number] = bitmap_layer;
  bitmap_layer_set_bitmap(bitmap_layer, images[slot_number]);
  Layer *window_layer = window_get_root_layer(window);
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer));
}

static void unload_digit_image_from_slot(int slot_number) {
  /*

     Removes the digit from the display and unloads the image resource
     to free up RAM.

     Can handle being called on an already empty slot.

   */

  if (image_slot_state[slot_number] != EMPTY_SLOT) {
    layer_remove_from_parent(bitmap_layer_get_layer(image_layers[slot_number]));
    bitmap_layer_destroy(image_layers[slot_number]);
    gbitmap_destroy(images[slot_number]);
    image_slot_state[slot_number] = EMPTY_SLOT;
  }

}

static void display_value(unsigned short value, unsigned short row_number, bool show_first_leading_zero) {
  /*

     Displays a numeric value between 0 and 99 on screen.

     Rows are ordered on screen as:

       Row 0
       Row 1

     Includes optional blanking of first leading zero,
     i.e. displays ' 0' rather than '00'.

   */
  value = value % 100; // Maximum of two digits per row.

  // Column order is: | Column 0 | Column 1 |
  // (We process the columns in reverse order because that makes
  // extracting the digits from the value easier.)
  for (int column_number = 1; column_number >= 0; column_number--) {
    int slot_number = (row_number * 2) + column_number;
    unload_digit_image_from_slot(slot_number);
    if (!((value == 0) && (column_number == 0) && !show_first_leading_zero)) {
      load_digit_image_into_slot(slot_number, value % 10);
    }
    value = value / 10;
  }
}

static unsigned short get_display_hour(unsigned short hour) {

  if (clock_is_24h_style()) {
    return hour;
  }

  unsigned short display_hour = hour % 12;

  // Converts "0" to "12"
  return display_hour ? display_hour : 12;

}

static void display_time(struct tm *tick_time) {

  // TODO: Use `units_changed` and more intelligence to reduce
  //       redundant digit unload/load?

  display_value(get_display_hour(tick_time->tm_hour), 0, false);
  display_value(tick_time->tm_min, 1, true);
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  display_time(tick_time);
}

static void msg_received(DictionaryIterator *iter, void *context) {

  Tuple *resource = dict_find(iter, KEY_RESOURCE);
  if (resource){

    switch (resource->value->int8){

      case RESOURCE_TEXT:

        if (dict_find(iter, KEY_START)) strcpy(text, "");

        strcat(text, dict_find(iter, KEY_TEXT)->value->cstring);

        if (dict_find(iter, KEY_END)) vibes_short_pulse();

        break;
      default:
        break;
    }
  }
}

static void accel_tap(AccelAxisType axis, int32_t direction){

  memo_view();
}
static void init() {
  window = window_create();
  window_stack_push(window, true);
  window_set_background_color(window, GColorBlack);


  // Avoids a blank screen on watch start.
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  display_time(tick_time);

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
  accel_tap_service_subscribe(accel_tap);
  app_message_register_inbox_received(msg_received);
  app_message_open(512, 512);
}

static void deinit() {
  for (int i = 0; i < TOTAL_IMAGE_SLOTS; i++) {
    unload_digit_image_from_slot(i);
  }

  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}