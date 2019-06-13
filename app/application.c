#include <application.h>

// LED instance
bc_led_t led;
bool led_state = false;

// PIR instance
bc_module_pir_t pir;

#define LED_STRIP_MOTION_DETECTOR_TIMEOUT 1000

// Button instance
bc_button_t button;

bc_scheduler_task_id_t task_motion_timeout_id;

void led_strip_turn_off();
void bc_night_lamp_time_set(uint64_t *id, const char *topic, void *value, void *param);

int lightTime = 10;

static const bc_radio_sub_t subs[] = 
{
    {"led-strip/-/time/set", BC_RADIO_SUB_PT_INT, bc_night_lamp_time_set, (void *) NULL}
};
// Led strip
static uint32_t _bc_module_power_led_strip_dma_buffer[LED_STRIP_COUNT * LED_STRIP_TYPE * 2];
const bc_led_strip_buffer_t led_strip_buffer =
{
    .type = LED_STRIP_TYPE,
    .count = LED_STRIP_COUNT,
    .buffer = _bc_module_power_led_strip_dma_buffer
};


void bc_night_lamp_time_set(uint64_t *id, const char *topic, void *value, void *param)
{
    lightTime = *(int *)value;
}

static struct
{
    enum
    {
        LED_STRIP_SHOW_COLOR = 0,
        LED_STRIP_SHOW_COMPOUND = 1,
        LED_STRIP_SHOW_EFFECT = 2,
    } show;
    bc_led_strip_t self;
    uint32_t color;
    bc_scheduler_task_id_t update_task_id;

} led_strip = { .show = LED_STRIP_SHOW_COLOR, .color = 255 };

        
void led_strip_fill(void)
{
    bc_led_strip_fill(&led_strip.self, led_strip.color);
}

void bc_radio_node_on_state_set(uint64_t *id, uint8_t state_id, bool *state)
{
    (void) id;
    if (state_id == BC_RADIO_NODE_STATE_LED)
    {
        led_state = *state;

        bc_led_set_mode(&led, led_state ? BC_LED_MODE_ON : BC_LED_MODE_OFF);

        bc_radio_pub_state(BC_RADIO_PUB_STATE_LED, &led_state);
    }
}

void bc_radio_node_on_led_strip_color_set(uint64_t *id, uint32_t *color)
{
    (void) id;

    bc_led_strip_effect_stop(&led_strip.self);

    led_strip.color = *color;

    led_strip.show = LED_STRIP_SHOW_COLOR;

    led_strip_fill();

    bc_scheduler_plan_now(led_strip.update_task_id);

    bc_scheduler_plan_from_now(task_motion_timeout_id, LED_STRIP_MOTION_DETECTOR_TIMEOUT * lightTime);

}

void led_strip_update_task(void *param)
{
    (void) param;

    if (!bc_led_strip_is_ready(&led_strip.self))
    {
        bc_scheduler_plan_current_now();

        return;
    }

    bc_led_strip_write(&led_strip.self);

    bc_scheduler_plan_current_relative(250);
}

void bc_radio_node_on_led_strip_brightness_set(uint64_t *id, uint8_t *brightness)
{
    (void) id;

    bc_led_strip_set_brightness(&led_strip.self, *brightness);

    led_strip_fill();

    bc_scheduler_plan_now(led_strip.update_task_id);
}

void pir_event_handler(bc_module_pir_t *self, bc_module_pir_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_MODULE_PIR_EVENT_MOTION)
    {
        bc_radio_pub_bool("movement", 1);
        bc_scheduler_plan_from_now(task_motion_timeout_id, LED_STRIP_MOTION_DETECTOR_TIMEOUT * lightTime);
    }
}

void application_init(void)
{

    // Initialize radio
    bc_radio_init(BC_RADIO_MODE_NODE_LISTENING);
    bc_radio_pairing_request("night-lamp", VERSION);
    bc_radio_set_subs((bc_radio_sub_t *) subs, sizeof(subs)/sizeof(bc_radio_sub_t));

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Initialize power module
    bc_module_power_init(); 
    bc_led_strip_init(&led_strip.self, bc_module_power_get_led_strip_driver(), &led_strip_buffer);
    led_strip.update_task_id = bc_scheduler_register(led_strip_update_task, NULL, 0);

    // Inialize PIR
    bc_module_pir_init(&pir);
    bc_module_pir_set_sensitivity(&pir, BC_MODULE_PIR_SENSITIVITY_MEDIUM);
    bc_module_pir_set_event_handler(&pir, pir_event_handler, NULL);

    task_motion_timeout_id = bc_scheduler_register(led_strip_turn_off, NULL, BC_TICK_INFINITY);

    bc_led_pulse(&led, 100);
}

void led_strip_turn_off()
{
    led_strip.color = 0;
    led_strip_fill();
}