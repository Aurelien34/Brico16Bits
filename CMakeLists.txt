project (Brico16Bits)

set (COMMANDS_CPP
    Commands/Command.cpp Commands/CommandBankLoadBank.cpp Commands/CommandBankStoreBank.cpp Commands/CommandSetRomWord.cpp Commands/CommandSendROM.cpp Commands/CommandSendROMPart.cpp Commands/CommandReadWord.cpp Commands/CommandReadWords.cpp Commands/CommandTrace.cpp Commands/CommandFree.cpp Commands/CommandFreq.cpp Commands/CommandClearBuffer.cpp
    Commands/CommandLs.cpp Commands/CommandReset.cpp Commands/CommandDelay.cpp
    Commands/CommandExtension.cpp Commands/CommandBootSel.cpp Commands/CommandDebug.cpp Commands/CommandComment.cpp Commands/CommandListRoms.cpp
    )

set (COMMANDS_H
    Commands/Command.h Commands/CommandBankLoadBank.h Commands/CommandBankStoreBank.h Commands/CommandSetRomWord.h Commands/CommandSendROM.h Commands/CommandSendROMPart.h Commands/CommandReadWord.h Commands/CommandReadWords.h Commands/CommandTrace.h Commands/CommandFree.h Commands/CommandFreq.h Commands/CommandClearBuffer.h
    Commands/CommandLs.h Commands/CommandReset.h Commands/CommandDelay.h
    Commands/CommandExtension.h Commands/CommandBootSel.h Commands/CommandDebug.h Commands/CommandComment.h Commands/CommandListRoms.h
    )

set (COMMANDS_68K_CPP
    Commands68k/Command68k.cpp
    Commands68k/Command68kPrintText.cpp
    Commands68k/Command68kPrintHex.cpp
    Commands68k/Command68kSwitchBank.cpp
    )

set (COMMANDS_68K_H
    Commands68k/Command68k.h
    Commands68k/Command68kPrintText.h
    Commands68k/Command68kPrintHex.h
    Commands68k/Command68kSwitchBank.h
    )

set (SRC Brico16Bits.cpp
    DMAManager.cpp PIOManager.cpp ROMImageManager.cpp CommandManager.cpp ActivityLED.cpp InputBuffer.cpp
    ${COMMANDS_CPP}
    ${COMMANDS_68K_CPP}
    BricoExtManager.cpp DisplayResult.cpp
    MassStorageManager.cpp
    TinyMassStorage.cpp 
    CommandManager68k.cpp
    NeoDiagImageBuilder.cpp
    OpcodeDecoder.cpp)

    set (HEADERS StateMachinesDefinitions.h MemoryMap.h
    PioBusAccess.pio.h PioBusWatcherAddresses.pio.h PioBusWatcherPort.pio.h
    PinDefinitions.h DMAManager.h PIOManager.h ROMImageManager.h CommandManager.h ActivityLED.h InputBuffer.cpp
    ${COMMANDS_H}
    ${COMMANDS_68K_H}
    BricoExtManager.h DisplayResult.h
    MassStorageManager.h
    CommandManager68k.h
    NeoDiagImageBuilder.h
    OpcodeDecoder.h)

add_executable(Brico16Bits ${SRC} ${HEADERS})

pico_generate_pio_header(Brico16Bits ${CMAKE_CURRENT_LIST_DIR}/PioBusAccess.pio)
pico_generate_pio_header(Brico16Bits ${CMAKE_CURRENT_LIST_DIR}/PioBusWatcherPort.pio)
pico_generate_pio_header(Brico16Bits ${CMAKE_CURRENT_LIST_DIR}/PioBusWatcherAddresses.pio)
pico_generate_pio_header(Brico16Bits ${CMAKE_CURRENT_LIST_DIR}/PioFrequencyCounter.pio)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(Brico16Bits PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

# pull in common dependencies
target_link_libraries(Brico16Bits pico_stdlib hardware_pio hardware_dma hardware_flash pico_multicore pico_unique_id tinyusb_device tinyusb_board)

# Choose stdio backend: USB (default) or UART on GPIO20 (TX) / GPIO21 (RX), i.e. UART1
set(STDIO_BACKEND "USB" CACHE STRING "Stdio backend: USB or UART (GPIO20 TX / GPIO21 RX)")
set_property(CACHE STDIO_BACKEND PROPERTY STRINGS USB UART)

set(STDIO_UART_BAUD_RATE "115200" CACHE STRING "UART baud rate used when STDIO_BACKEND=UART")

if(STDIO_BACKEND STREQUAL "UART")
    pico_enable_stdio_usb(Brico16Bits 0)
    pico_enable_stdio_uart(Brico16Bits 1)
    add_compile_definitions(
        PICO_DEFAULT_UART=1
        PICO_DEFAULT_UART_TX_PIN=20 # Matches BricoNeo SDA pin
        PICO_DEFAULT_UART_RX_PIN=21 # Matches BricoNeo SCL pin
        PICO_DEFAULT_UART_BAUD_RATE=${STDIO_UART_BAUD_RATE}
        STDIO_BACKEND_UART=1
    )
elseif(STDIO_BACKEND STREQUAL "USB")
    pico_enable_stdio_usb(Brico16Bits 1)
    pico_enable_stdio_uart(Brico16Bits 0)
    add_compile_definitions(STDIO_BACKEND_USB=1)
else()
    message(FATAL_ERROR "STDIO_BACKEND must be USB or UART, got: ${STDIO_BACKEND}")
endif()

# Compute date and time constants
string(TIMESTAMP BUILD_DATE_TIME_FULL "%Y/%m/%d %H:%M")
add_compile_definitions(BUILD_DATE_TIME_FULL="${BUILD_DATE_TIME_FULL}")

string(TIMESTAMP BUILD_DATE_TIME_YEAR %Y)
add_compile_definitions(BUILD_DATE_TIME_YEAR=${BUILD_DATE_TIME_YEAR})

string(TIMESTAMP BUILD_DATE_TIME_MONTH %m)
math(EXPR BUILD_DATE_TIME_MONTH_NEW "${BUILD_DATE_TIME_MONTH}" OUTPUT_FORMAT DECIMAL)
add_compile_definitions(BUILD_DATE_TIME_MONTH=${BUILD_DATE_TIME_MONTH_NEW})

string(TIMESTAMP BUILD_DATE_TIME_DAY %d)
math(EXPR BUILD_DATE_TIME_DAY_NEW "${BUILD_DATE_TIME_DAY}" OUTPUT_FORMAT DECIMAL)
add_compile_definitions(BUILD_DATE_TIME_DAY=${BUILD_DATE_TIME_DAY_NEW})

string(TIMESTAMP BUILD_DATE_TIME_HOURS %H)
math(EXPR BUILD_DATE_TIME_HOURS_NEW "${BUILD_DATE_TIME_HOURS}" OUTPUT_FORMAT DECIMAL)
add_compile_definitions(BUILD_DATE_TIME_HOURS=${BUILD_DATE_TIME_HOURS_NEW})

string(TIMESTAMP BUILD_DATE_TIME_MINUTES %M)
math(EXPR BUILD_DATE_TIME_MINUTES_NEW "${BUILD_DATE_TIME_MINUTES}" OUTPUT_FORMAT DECIMAL)
add_compile_definitions(BUILD_DATE_TIME_MINUTES=${BUILD_DATE_TIME_MINUTES_NEW})

string(TIMESTAMP BUILD_DATE_TIME_SECONDS %S)
math(EXPR BUILD_DATE_TIME_SECONDS_NEW "${BUILD_DATE_TIME_SECONDS}" OUTPUT_FORMAT DECIMAL)
add_compile_definitions(BUILD_DATE_TIME_SECONDS=${BUILD_DATE_TIME_SECONDS_NEW})

# Set USB device name
add_compile_definitions(USBD_PRODUCT="BricoNeo")
add_compile_definitions(PICO_STDIO_ENABLE_CRLF_SUPPORT=1)
add_compile_definitions(PICO_STDIO_USB_CONNECTION_WITHOUT_DTR)

# Activate mass storage
add_compile_definitions(USE_MASS_STORAGE)
add_compile_definitions(MASS_STORAGE_DISABLE_AUTOPLAY)
#add_compile_definitions(MASS_STORAGE_DEBUG)

# Absolute time management
#add_compile_definitions(PICO_OPAQUE_ABSOLUTE_TIME_T)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(Brico16Bits)

# apply custom memmap to allow 128kB RAM pre-allocation and circual input buffer
if (PICO_PLATFORM STREQUAL "rp2040")
    pico_set_linker_script(Brico16Bits "${CMAKE_CURRENT_LIST_DIR}/memmap_with_rom_image_2040.ld")
elseif (PICO_PLATFORM MATCHES "^rp2350")
    pico_set_linker_script(Brico16Bits "${CMAKE_CURRENT_LIST_DIR}/memmap_with_rom_image_2350.ld")
else()
    message(FATAL_ERROR "PICO_PLATFORM not managed: ${PICO_PLATFORM}")
endif()
