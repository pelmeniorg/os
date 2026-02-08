cmake_minimum_required(VERSION 3.20)

# ═══════════════════════════════════════════════════════════════════
#  Определение платформы (до project!)
# ═══════════════════════════════════════════════════════════════════
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(HOST_WINDOWS TRUE)
    set(HOST_LINUX FALSE)
    set(EXE_SUFFIX ".exe")
    set(SCRIPT_SUFFIX ".bat")
else()
    set(HOST_WINDOWS FALSE)
    set(HOST_LINUX TRUE)
    set(EXE_SUFFIX "")
    set(SCRIPT_SUFFIX ".sh")
endif()

# ═══════════════════════════════════════════════════════════════════
#  Кросс-компиляция
# ═══════════════════════════════════════════════════════════════════
set(CMAKE_SYSTEM_NAME          Generic)
set(CMAKE_SYSTEM_PROCESSOR     x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Поиск компилятора с учётом платформы
find_program(CLANG_C   NAMES clang   clang${EXE_SUFFIX})
find_program(CLANG_CXX NAMES clang++ clang++${EXE_SUFFIX})

if(CLANG_C AND CLANG_CXX)
    set(CMAKE_C_COMPILER   "${CLANG_C}")
    set(CMAKE_CXX_COMPILER "${CLANG_CXX}")
else()
    message(FATAL_ERROR "Clang not found! Please install LLVM/Clang.")
endif()

project(shlakoblokunos LANGUAGES C CXX)

# ═══════════════════════════════════════════════════════════════════
#  Конфигурация
# ═══════════════════════════════════════════════════════════════════
set(KERNEL_OUTPUT   "shlakoblokunos.elf")
set(ISO_OUTPUT      "shlakoblokunos.iso")
set(RUST_CRATE_NAME "shlakoblokunos")
set(SRC_DIR         "${CMAKE_SOURCE_DIR}/src")
set(LINKER_SCRIPT   "${CMAKE_SOURCE_DIR}/linker.ld")
set(LIMINE_DIR      "${CMAKE_BINARY_DIR}/limine")
set(ISO_ROOT        "${CMAKE_BINARY_DIR}/iso_root")
set(TOOLS_DIR       "${CMAKE_BINARY_DIR}/tools")

# ═══════════════════════════════════════════════════════════════════
#  Поиск инструментов
# ═══════════════════════════════════════════════════════════════════
find_program(CARGO  NAMES cargo  cargo${EXE_SUFFIX} REQUIRED)
find_program(GIT    NAMES git    git${EXE_SUFFIX}   REQUIRED)
find_program(NASM   NAMES nasm   nasm${EXE_SUFFIX}  REQUIRED)
find_program(RUSTUP NAMES rustup rustup${EXE_SUFFIX})

# LLD: разные имена на разных платформах
find_program(LLD NAMES ld.lld lld ld.lld${EXE_SUFFIX} lld${EXE_SUFFIX})
if(NOT LLD)
    # На Windows может быть в LLVM/bin
    find_program(LLD NAMES lld-link
            HINTS
            "$ENV{ProgramFiles}/LLVM/bin"
            "$ENV{ProgramW6432}/LLVM/bin"
    )
endif()
if(NOT LLD)
    message(FATAL_ERROR "LLD linker not found! Please install LLVM.")
endif()

# ═══════════════════════════════════════════════════════════════════
#  xorriso / mkisofs (кроссплатформенный поиск)
# ═══════════════════════════════════════════════════════════════════
set(ISO_TOOL_FOUND FALSE)
set(ISO_TOOL_TYPE "")

if(HOST_LINUX)
    # Linux: предпочитаем xorriso
    find_program(XORRISO NAMES xorriso)
    if(XORRISO)
        set(ISO_TOOL_FOUND TRUE)
        set(ISO_TOOL_TYPE "xorriso")
    else()
        find_program(XORRISO NAMES mkisofs genisoimage)
        if(XORRISO)
            set(ISO_TOOL_FOUND TRUE)
            set(ISO_TOOL_TYPE "mkisofs")
        endif()
    endif()
else()
    # Windows: ищем xorriso
    find_program(XORRISO_EXE NAMES xorriso xorriso.exe
            HINTS
            "${TOOLS_DIR}"
            "C:/Tools"
            "$ENV{USERPROFILE}/scoop/shims"
            "C:/msys64/usr/bin"
            "C:/cygwin64/bin"
    )
    if(XORRISO_EXE)
        set(XORRISO "${XORRISO_EXE}")
        set(ISO_TOOL_FOUND TRUE)
        set(ISO_TOOL_TYPE "xorriso")  # ← Это xorriso, не mkisofs!
    else()
        # Fallback: ищем mkisofs
        find_program(MKISOFS_EXE NAMES mkisofs mkisofs.exe genisoimage genisoimage.exe
                HINTS
                "${TOOLS_DIR}"
                "C:/Tools/cdrtools"
                "C:/Tools"
                "$ENV{USERPROFILE}/scoop/shims"
        )
        if(MKISOFS_EXE)
            set(XORRISO "${MKISOFS_EXE}")
            set(ISO_TOOL_FOUND TRUE)
            set(ISO_TOOL_TYPE "mkisofs")
        endif()
    endif()

    # Автозагрузка mkisofs для Windows (если ничего не найдено)
    if(NOT ISO_TOOL_FOUND)
        set(MKISOFS_PATH "${TOOLS_DIR}/mkisofs.exe")
        if(NOT EXISTS "${MKISOFS_PATH}")
            message(STATUS "Downloading mkisofs for Windows...")
            file(MAKE_DIRECTORY "${TOOLS_DIR}")
            file(DOWNLOAD
                    "https://github.com/AveYo/fox/raw/main/cdrtfe/tools/cdrtools/mkisofs.exe"
                    "${MKISOFS_PATH}"
                    STATUS _DL_STATUS
                    SHOW_PROGRESS
                    TIMEOUT 60
            )
            list(GET _DL_STATUS 0 _DL_CODE)
            if(_DL_CODE EQUAL 0 AND EXISTS "${MKISOFS_PATH}")
                set(XORRISO "${MKISOFS_PATH}")
                set(ISO_TOOL_FOUND TRUE)
                set(ISO_TOOL_TYPE "mkisofs")
            endif()
        else()
            set(XORRISO "${MKISOFS_PATH}")
            set(ISO_TOOL_FOUND TRUE)
            set(ISO_TOOL_TYPE "mkisofs")
        endif()
    endif()
endif()

if(NOT ISO_TOOL_FOUND)
    message(WARNING
            "xorriso/mkisofs not found — ISO creation disabled\n"
            "Install: ${HOST_LINUX} ? 'apt install xorriso' : 'scoop install xorriso'"
    )
else()
    message(STATUS "Found ISO tool: ${XORRISO} (type: ${ISO_TOOL_TYPE})")
endif()

# ═══════════════════════════════════════════════════════════════════
#  Rust target
# ═══════════════════════════════════════════════════════════════════
set(RUST_TARGET "x86_64-unknown-none")

if(RUSTUP)
    execute_process(
            COMMAND ${RUSTUP} target list --installed
            OUTPUT_VARIABLE _INSTALLED
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
    if(NOT _INSTALLED MATCHES "${RUST_TARGET}")
        message(STATUS "Installing Rust target ${RUST_TARGET}...")
        execute_process(
                COMMAND ${RUSTUP} target add ${RUST_TARGET}
                RESULT_VARIABLE _RC
        )
        if(NOT _RC EQUAL 0)
            message(WARNING "Failed to install Rust target automatically")
        endif()
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════
#  Скачивание Limine (кроссплатформенно)
# ═══════════════════════════════════════════════════════════════════
set(LIMINE_VERSION "8.6.0")

# Функция для распаковки архива
function(extract_archive ARCHIVE DEST)
    get_filename_component(EXT "${ARCHIVE}" LAST_EXT)

    if(EXT STREQUAL ".xz" OR ARCHIVE MATCHES "\\.tar\\.xz$")
        # .tar.xz
        find_program(TAR NAMES tar tar${EXE_SUFFIX})
        if(TAR)
            execute_process(
                    COMMAND ${TAR} -xf "${ARCHIVE}" --strip-components=1 -C "${DEST}"
                    RESULT_VARIABLE _RC
            )
            if(_RC EQUAL 0)
                return()
            endif()
        endif()

        # Fallback: 7z (особенно для Windows)
        find_program(SEVENZIP NAMES 7z 7za 7z.exe
                HINTS
                "C:/Program Files/7-Zip"
                "$ENV{ProgramFiles}/7-Zip"
                "$ENV{ProgramW6432}/7-Zip"
        )
        if(SEVENZIP)
            get_filename_component(ARCHIVE_DIR "${ARCHIVE}" DIRECTORY)
            # Распаковка .xz -> .tar
            execute_process(
                    COMMAND ${SEVENZIP} x "${ARCHIVE}" -o"${ARCHIVE_DIR}" -y
                    OUTPUT_QUIET
            )
            string(REGEX REPLACE "\\.xz$" "" TAR_FILE "${ARCHIVE}")
            if(EXISTS "${TAR_FILE}")
                # Распаковка .tar
                execute_process(
                        COMMAND ${SEVENZIP} x "${TAR_FILE}" -o"${DEST}" -y
                        OUTPUT_QUIET
                )
                # Перемещаем содержимое подпапки наверх
                file(GLOB SUBDIRS "${DEST}/limine-*")
                if(SUBDIRS)
                    list(GET SUBDIRS 0 SUBDIR)
                    file(GLOB ITEMS "${SUBDIR}/*")
                    foreach(ITEM ${ITEMS})
                        get_filename_component(ITEM_NAME "${ITEM}" NAME)
                        file(RENAME "${ITEM}" "${DEST}/${ITEM_NAME}")
                    endforeach()
                    file(REMOVE_RECURSE "${SUBDIR}")
                endif()
                file(REMOVE "${TAR_FILE}")
            endif()
            return()
        endif()
    endif()

    message(WARNING "Cannot extract ${ARCHIVE}")
endfunction()

# Скачивание Limine
if(NOT EXISTS "${LIMINE_DIR}/limine-bios.sys")
    message(STATUS "Downloading Limine ${LIMINE_VERSION} binaries...")

    set(LIMINE_ARCHIVE "${CMAKE_BINARY_DIR}/limine.tar.xz")
    set(LIMINE_URL "https://github.com/limine-bootloader/limine/releases/download/v${LIMINE_VERSION}/limine-${LIMINE_VERSION}-binary.tar.xz")

    file(DOWNLOAD
            "${LIMINE_URL}"
            "${LIMINE_ARCHIVE}"
            STATUS _DL_STATUS
            SHOW_PROGRESS
            TIMEOUT 120
    )

    list(GET _DL_STATUS 0 _DL_CODE)

    if(_DL_CODE EQUAL 0 AND EXISTS "${LIMINE_ARCHIVE}")
        file(MAKE_DIRECTORY "${LIMINE_DIR}")
        extract_archive("${LIMINE_ARCHIVE}" "${LIMINE_DIR}")
        file(REMOVE "${LIMINE_ARCHIVE}")
    endif()

    # Fallback: git clone
    if(NOT EXISTS "${LIMINE_DIR}/limine-bios.sys")
        message(STATUS "Trying git clone...")
        file(REMOVE_RECURSE "${LIMINE_DIR}")
        execute_process(
                COMMAND ${GIT} clone --depth=1 --branch=v8.x-binary
                https://github.com/limine-bootloader/limine.git
                "${LIMINE_DIR}"
                RESULT_VARIABLE _RC
        )
        if(NOT _RC EQUAL 0)
            message(FATAL_ERROR "Failed to download Limine")
        endif()
    endif()

    # Копируем limine.h
    if(EXISTS "${LIMINE_DIR}/limine.h")
        file(COPY "${LIMINE_DIR}/limine.h" DESTINATION "${SRC_DIR}")
    endif()
endif()

# Проверка файлов
if(NOT EXISTS "${LIMINE_DIR}/limine-bios.sys")
    message(FATAL_ERROR
            "Limine binaries not found!\n"
            "Please manually download from:\n"
            "https://github.com/limine-bootloader/limine/releases\n"
            "And extract to: ${LIMINE_DIR}"
    )
endif()

# ═══════════════════════════════════════════════════════════════════
#  Флаги компилятора (идентичные на обеих платформах)
# ═══════════════════════════════════════════════════════════════════
set(TARGET_TRIPLE "x86_64-unknown-none-elf")

set(_COMMON_FLAGS
        "--target=${TARGET_TRIPLE}"
        "-ffreestanding"
        "-fno-stack-protector"
        "-fno-pic"
        "-fno-pie"
        "-mno-red-zone"
        "-mcmodel=kernel"
        "-Wall"
        "-Wextra"
)
list(JOIN _COMMON_FLAGS " " _COMMON)

set(CMAKE_C_FLAGS   "${_COMMON} -std=c17"                            CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${_COMMON} -std=c++20 -fno-exceptions -fno-rtti" CACHE STRING "" FORCE)

# ═══════════════════════════════════════════════════════════════════
#  Исходники
# ═══════════════════════════════════════════════════════════════════
file(GLOB_RECURSE ASM_SRCS CONFIGURE_DEPENDS "${SRC_DIR}/*.asm")
file(GLOB_RECURSE C_SRCS   CONFIGURE_DEPENDS "${SRC_DIR}/*.c")
file(GLOB_RECURSE CXX_SRCS CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE RS_SRCS  CONFIGURE_DEPENDS "${SRC_DIR}/*.rs")

set(CC_SRCS ${C_SRCS} ${CXX_SRCS})
set(LINK_OBJS "")
set(LINK_LIBS "")
set(LINK_DEPS "${LINKER_SCRIPT}")
set(KERNEL_TARGETS "")

# ═══════════════════════════════════════════════════════════════════
#  1. NASM → .o
# ═══════════════════════════════════════════════════════════════════
foreach(SRC IN LISTS ASM_SRCS)
    file(RELATIVE_PATH REL "${SRC_DIR}" "${SRC}")
    string(REGEX REPLACE "[/\\\\]" "_" FLAT "${REL}")
    string(REGEX REPLACE "\\.asm$" ".o" FLAT "${FLAT}")
    set(OBJ "${CMAKE_BINARY_DIR}/asm/${FLAT}")

    add_custom_command(
            OUTPUT  "${OBJ}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/asm"
            COMMAND ${NASM} -f elf64 -I "${SRC_DIR}/" -o "${OBJ}" "${SRC}"
            DEPENDS "${SRC}"
            COMMENT "[NASM] ${REL}"
            VERBATIM
    )

    list(APPEND LINK_OBJS "${OBJ}")
    list(APPEND LINK_DEPS "${OBJ}")
endforeach()

# ═══════════════════════════════════════════════════════════════════
#  2. C/C++ → .a
# ═══════════════════════════════════════════════════════════════════
if(CC_SRCS)
    add_library(cc_lib STATIC ${CC_SRCS})
    target_include_directories(cc_lib PRIVATE "${SRC_DIR}")

    # Отключаем стандартные предупреждения MSVC-стиля на Windows
    if(HOST_WINDOWS)
        target_compile_definitions(cc_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    list(APPEND LINK_LIBS      "$<TARGET_FILE:cc_lib>")
    list(APPEND LINK_DEPS      "$<TARGET_FILE:cc_lib>")
    list(APPEND KERNEL_TARGETS cc_lib)
endif()

# ═══════════════════════════════════════════════════════════════════
#  3. Rust → .a
# ═══════════════════════════════════════════════════════════════════
set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/rust")
string(REPLACE "-" "_" _CRATE "${RUST_CRATE_NAME}")
set(RUST_LIB "${RUST_TARGET_DIR}/${RUST_TARGET}/release/lib${_CRATE}.a")

add_custom_command(
        OUTPUT  "${RUST_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env
        CARGO_TARGET_DIR="${RUST_TARGET_DIR}"
        ${CARGO} build
        --release
        --target ${RUST_TARGET}
        --target-dir "${RUST_TARGET_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS ${RS_SRCS} "${CMAKE_SOURCE_DIR}/Cargo.toml"
        COMMENT "[Cargo] lib${_CRATE}.a"
        VERBATIM
)
add_custom_target(rust_lib DEPENDS "${RUST_LIB}")

list(APPEND LINK_LIBS      "${RUST_LIB}")
list(APPEND LINK_DEPS      "${RUST_LIB}")
list(APPEND KERNEL_TARGETS rust_lib)

# ═══════════════════════════════════════════════════════════════════
#  4. Линковка → ELF
# ═══════════════════════════════════════════════════════════════════
set(KERNEL_PATH "${CMAKE_BINARY_DIR}/${KERNEL_OUTPUT}")

# Определяем правильную команду линковки
get_filename_component(LLD_NAME "${LLD}" NAME_WE)
string(TOLOWER "${LLD_NAME}" LLD_NAME_LOWER)

if(LLD_NAME_LOWER STREQUAL "lld-link")
    # Windows lld-link требует другой синтаксис - используем ld.lld через flavor
    set(LLD_CMD ${LLD} -flavor gnu)
else()
    set(LLD_CMD ${LLD})
endif()

add_custom_command(
        OUTPUT  "${KERNEL_PATH}"
        COMMAND ${LLD_CMD}
        -T "${LINKER_SCRIPT}"
        -o "${KERNEL_PATH}"
        --gc-sections
        -nostdlib
        -static
        ${LINK_OBJS}
        --whole-archive ${LINK_LIBS} --no-whole-archive
        DEPENDS ${LINK_DEPS}
        COMMENT "[LD] ${KERNEL_OUTPUT}"
        VERBATIM
)

add_custom_target(kernel ALL DEPENDS "${KERNEL_PATH}")
foreach(_T IN LISTS KERNEL_TARGETS)
    add_dependencies(kernel ${_T})
endforeach()

# ═══════════════════════════════════════════════════════════════════
#  5. ISO образ (BIOS + UEFI)
# ═══════════════════════════════════════════════════════════════════
if(ISO_TOOL_FOUND)
    set(ISO_PATH "${CMAKE_BINARY_DIR}/${ISO_OUTPUT}")

    # Определяем режим загрузки
    if(ISO_TOOL_TYPE STREQUAL "xorriso")
        set(BOOT_MODE "BIOS + UEFI")
    else()
        set(BOOT_MODE "BIOS only")
    endif()

    # Используем относительные пути для xorriso на Windows
    set(ISO_ROOT_REL "iso_root")
    set(ISO_OUTPUT_REL "${ISO_OUTPUT}")

    # Собираем команды подготовки
    set(ISO_PREP_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${ISO_ROOT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ISO_ROOT}/boot/limine"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ISO_ROOT}/EFI/BOOT"
            COMMAND ${CMAKE_COMMAND} -E copy "${KERNEL_PATH}" "${ISO_ROOT}/boot/"
            COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/limine.conf" "${ISO_ROOT}/boot/limine/"
            COMMAND ${CMAKE_COMMAND} -E copy "${LIMINE_DIR}/limine-bios.sys" "${ISO_ROOT}/boot/limine/"
            COMMAND ${CMAKE_COMMAND} -E copy "${LIMINE_DIR}/limine-bios-cd.bin" "${ISO_ROOT}/boot/limine/"
    )

    # UEFI файлы (только для xorriso)
    if(ISO_TOOL_TYPE STREQUAL "xorriso")
        list(APPEND ISO_PREP_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy "${LIMINE_DIR}/limine-uefi-cd.bin" "${ISO_ROOT}/boot/limine/"
                COMMAND ${CMAKE_COMMAND} -E copy "${LIMINE_DIR}/BOOTX64.EFI" "${ISO_ROOT}/EFI/BOOT/"
                COMMAND ${CMAKE_COMMAND} -E copy "${LIMINE_DIR}/BOOTIA32.EFI" "${ISO_ROOT}/EFI/BOOT/"
        )
    endif()

    # Команда создания ISO с ОТНОСИТЕЛЬНЫМИ путями
    if(ISO_TOOL_TYPE STREQUAL "xorriso")
        set(ISO_CREATE_COMMAND
                COMMAND ${XORRISO}
                -as mkisofs
                -b boot/limine/limine-bios-cd.bin
                -no-emul-boot
                -boot-load-size 4
                -boot-info-table
                --efi-boot boot/limine/limine-uefi-cd.bin
                -efi-boot-part
                --efi-boot-image
                --protective-msdos-label
                -o "${ISO_OUTPUT_REL}"
                "${ISO_ROOT_REL}"
        )
    else()
        # mkisofs / genisoimage (только BIOS)
        set(ISO_CREATE_COMMAND
                COMMAND ${XORRISO}
                -b boot/limine/limine-bios-cd.bin
                -no-emul-boot
                -boot-load-size 4
                -boot-info-table
                -o "${ISO_OUTPUT_REL}"
                "${ISO_ROOT_REL}"
        )
    endif()

    add_custom_command(
            OUTPUT  "${ISO_PATH}"
            ${ISO_PREP_COMMANDS}
            ${ISO_CREATE_COMMAND}
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            DEPENDS "${KERNEL_PATH}" "${CMAKE_SOURCE_DIR}/limine.conf"
            COMMENT "[ISO] ${ISO_OUTPUT} (${BOOT_MODE})"
            VERBATIM
    )

    add_custom_target(iso DEPENDS "${ISO_PATH}")
    add_dependencies(iso kernel)
else()
    # Заглушка если ISO tools не найдены
    add_custom_target(iso
            COMMAND ${CMAKE_COMMAND} -E echo "ERROR: xorriso/mkisofs not found"
            COMMAND ${CMAKE_COMMAND} -E echo "Install: apt install xorriso (Linux) or scoop install xorriso (Windows)"
            COMMAND ${CMAKE_COMMAND} -E false
    )
    set(BOOT_MODE "N/A")
endif()


# ═══════════════════════════════════════════════════════════════════
#  Итог
# ═══════════════════════════════════════════════════════════════════
list(LENGTH ASM_SRCS _N0)
list(LENGTH C_SRCS   _N1)
list(LENGTH CXX_SRCS _N2)
list(LENGTH RS_SRCS  _N3)

if(HOST_WINDOWS)
    set(PLATFORM_STR "Windows")
else()
    set(PLATFORM_STR "Linux")
endif()

message(STATUS "")
message(STATUS "  ╔═══════════════════════════════════════════╗")
message(STATUS "  ║   Shlakoblokunos (Limine x86_64)          ║")
message(STATUS "  ╠═══════════════════════════════════════════╣")
message(STATUS "  ║  Platform : ${PLATFORM_STR}")
message(STATUS "  ║  ASM      : ${_N0} files")
message(STATUS "  ║  C        : ${_N1} files")
message(STATUS "  ║  C++      : ${_N2} files")
message(STATUS "  ║  Rust     : ${_N3} files")
message(STATUS "  ╠═══════════════════════════════════════════╣")
if(ISO_TOOL_FOUND)
    message(STATUS "  ║  ISO tool : ${ISO_TOOL_TYPE} (${BOOT_MODE})")
else()
    message(STATUS "  ║  ISO tool : NOT FOUND")
endif()
if(QEMU)
    message(STATUS "  ║  QEMU     : Found")
else()
    message(STATUS "  ║  QEMU     : Not found")
endif()
message(STATUS "  ╠═══════════════════════════════════════════╣")
message(STATUS "  ║  cmake --build build                      ║")
message(STATUS "  ║  cmake --build build --target iso         ║")
message(STATUS "  ╚═══════════════════════════════════════════╝")
message(STATUS "")