idf_component_register(
    SRCS
        "main.c"
        "display_st7789.c"
        "terminal.c"
        "vfs.c"
        "shell.c"
        "elf_loader.c"
        "process.c"
        "tca8418.c"
        "ds3231.c"
        "i2c_bus.c"
        "editor.c"
        "dino.c"
    INCLUDE_DIRS
        "."
        "../include"
    REQUIRES
        driver
        spiffs
)

# Provide strong precompiled library symbols (override ESP-IDF defaults)
target_link_libraries(__idf_kernel INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/libprecompiled.a")

# Linker wraps for QEMU MMU bug workaround:
#   - spi_flash_mmap: PSRAM remap consumes all MMU pages, esp_mmu_map fails with 0x101
#   - spi_flash_munmap: no-op for the buffer-based mmap
#   - esp_partition_find_first: direct return of hardcoded partitions
target_link_libraries(__idf_kernel INTERFACE
    "-Wl,--wrap=spi_flash_mmap"
    "-Wl,--wrap=spi_flash_munmap"
    "-Wl,--wrap=esp_partition_find_first"
)


