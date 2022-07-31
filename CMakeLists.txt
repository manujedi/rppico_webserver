cmake_minimum_required(VERSION 3.13)

#enable pico W (not sure which one is actually needed)
set(BOARD=pico-w)
set(PICO_BOARD pico_w)


# initialize pico-sdk from submodule
# note: this must happen before project()
include(../pico-sdk/pico_sdk_init.cmake)

project(main)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_compile_options(-Wall
		-Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
		-Wno-unused-function # we have some for the docs that aren't called
		-Wno-maybe-uninitialized
		)

# all of your files
add_executable(main
		main.c
		websites/websites.h
)

#so the sdk can find our lwipopts files
target_include_directories(main PRIVATE
		${CMAKE_CURRENT_LIST_DIR}
		)

target_compile_definitions(main PRIVATE
		WIFI_SSID=\"somessid\"
		WIFI_PASSWORD=\"somepassword\"
		)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(main
		#pico_lwip_iperf
		pico_cyw43_arch_lwip_threadsafe_background
		pico_stdlib)

#use usb serial
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)


# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(main)


