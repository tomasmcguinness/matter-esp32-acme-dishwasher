set(SRC_LIST   app_driver.cpp
               app_main.cpp
               dishwasher_manager.cpp
               status_display.cpp
               mode_selector.cpp
               esp_lcd_panel_ssd1681.c
   )

idf_component_register(SRCS               "${SRC_LIST}"
                       INCLUDE_DIRS       ${INCLUDE_DIRS_LIST})

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
