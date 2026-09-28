idf_component_register(SRCS "esp_lcd_jd9365.c" INCLUDE_DIRS "include" PRIV_REQUIRES "driver" REQUIRES "esp_lcd")


# Version macros normally provided by the registry's cmake_utilities package.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP_LCD_JD9365_VER_MAJOR=2 ESP_LCD_JD9365_VER_MINOR=0 ESP_LCD_JD9365_VER_PATCH=2)
