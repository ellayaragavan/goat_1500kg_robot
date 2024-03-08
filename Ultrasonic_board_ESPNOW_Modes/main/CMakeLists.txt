if(CONFIG_ULTRASONIC_SPEED_MODE)
idf_component_register(SRCS "speed_main.c"
                    INCLUDE_DIRS ".")
else()
idf_component_register(SRCS "normal_main.c"
                    INCLUDE_DIRS ".")
endif()