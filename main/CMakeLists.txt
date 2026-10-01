idf_component_register(SRCS "main.c"
                            "joy_nav.c"
                            "core/hal_input.c"
                            "core/input_filter.c"
                            "core/crsf.c"
                            "core/storage_manager.c"
                            "core/utils.c"
                            "tasks/sensor_task.c"
                            "tasks/radio_task.c"
                            "tasks/ui_task.c"
                    INCLUDE_DIRS "." "core" "tasks"
                    REQUIRES rc_input ui_display ble_hid nvs_flash
                    PRIV_REQUIRES esp_app_format)
