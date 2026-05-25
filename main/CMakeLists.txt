idf_component_register(SRCS "main.c" "alert.c" "display.c"
                    INCLUDE_DIRS "."
                    REQUIRES st7735_driver ina219)
