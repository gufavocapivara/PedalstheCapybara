cmake_minimum_required(VERSION 3.16)
project(PedalTheCapybara C CXX)          # C é necessário para compilar o Lua
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---- dependências -----------------------------------------------------------
# Ordem: 1) pasta local (third_party/glfw, third_party/imgui, third_party/lua)
#        2) download automático (FetchContent).
#
# stb_image.h é baixado automaticamente para third_party/stb/stb_image.h
# se não existir. Você NÃO precisa mais colocar nada manualmente.
set(GLFW_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw"  CACHE PATH "pasta do código-fonte do GLFW")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui" CACHE PATH "pasta do Dear ImGui")
set(LUA_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua"   CACHE PATH "pasta do Lua 5.4 (a que contém src/ ou lua.h)")
set(STB_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"   CACHE PATH "pasta do stb_image.h")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio.h")
  message(FATAL_ERROR "Missing third_party/miniaudio.h (download it from github.com/mackron/miniaudio)")
endif()

include(FetchContent)
option(FETCHCONTENT_UPDATES_DISCONNECTED "Skip updates for already downloaded dependencies" ON)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_QUIET OFF)

# ---- GLFW -------------------------------------------------------------------
if(EXISTS "${GLFW_DIR}/CMakeLists.txt")
  message(STATUS "GLFW: usando ${GLFW_DIR}")
  add_subdirectory("${GLFW_DIR}" glfw_build)
else()
  message(STATUS "GLFW: baixando (FetchContent)")
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

# ---- Dear ImGui -------------------------------------------------------------
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
  message(STATUS "ImGui: baixando (FetchContent)")
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)
  set(IMGUI_DIR "${imgui_SOURCE_DIR}")
else()
  message(STATUS "ImGui: usando ${IMGUI_DIR}")
endif()

# ---- Lua 5.4 ----------------------------------------------------------------
if(EXISTS "${LUA_DIR}/src/lua.h")
  set(LUA_SRC_DIR "${LUA_DIR}/src")
elseif(EXISTS "${LUA_DIR}/lua.h")
  set(LUA_SRC_DIR "${LUA_DIR}")
else()
  message(STATUS "Lua: baixando 5.4.7 (FetchContent)")
  FetchContent_Declare(lua_dl
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(lua_dl)
  set(LUA_SRC_DIR "${lua_dl_SOURCE_DIR}/src")
endif()
message(STATUS "Lua: usando ${LUA_SRC_DIR}")

# ---- stb_image.h ------------------------------------------------------------
# Baixa automaticamente se não existir em third_party/stb/stb_image.h.
# O arquivo é header-only; só uma TU define STB_IMAGE_IMPLEMENTATION (textures.cpp).
if(NOT EXISTS "${STB_DIR}/stb_image.h")
  message(STATUS "stb_image: baixando (FetchContent)")
  FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(stb)
  set(STB_DIR "${stb_SOURCE_DIR}")
endif()
if(NOT EXISTS "${STB_DIR}/stb_image.h")
  message(FATAL_ERROR "stb_image.h not found in ${STB_DIR}")
endif()
message(STATUS "stb_image: usando ${STB_DIR}")

# ---- Lua library ------------------------------------------------------------
file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua)\\.c$")
add_library(lua_lib STATIC ${LUA_SOURCES} "include/amps.h")
set_target_properties(lua_lib PROPERTIES LINKER_LANGUAGE C)
target_include_directories(lua_lib PUBLIC "${LUA_SRC_DIR}")
if(UNIX AND NOT APPLE)
  target_compile_definitions(lua_lib PRIVATE LUA_USE_POSIX)
  target_link_libraries(lua_lib PUBLIC m dl)
endif()

# ---- OpenGL -----------------------------------------------------------------
find_package(OpenGL REQUIRED)

# ---- miniz (ZIP: .pctp / .pp) ------------------------------------------------
set(MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")
if(NOT EXISTS "${MINIZ_DIR}/miniz.c")
  message(FATAL_ERROR "Missing third_party/miniz (miniz.c, miniz_zip.c, ...)")
endif()
add_library(miniz STATIC
  "${MINIZ_DIR}/miniz.c"
  "${MINIZ_DIR}/miniz_tdef.c"
  "${MINIZ_DIR}/miniz_tinfl.c"
  "${MINIZ_DIR}/miniz_zip.c")
target_include_directories(miniz PUBLIC "${MINIZ_DIR}")

# ---- ImGui library ----------------------------------------------------------
add_library(imgui_lib STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp "include/amps.h")
target_include_directories(imgui_lib PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# ---- app --------------------------------------------------------------------
add_executable(PedalTheCapybara
  src/main.cpp
  src/audio.cpp
  src/textures.cpp
  src/plugins.cpp
  src/plugin_package.cpp
  include/audio.h
  include/pedals.h
  include/assets.h
  include/plugins.h
  include/plugin_package.h
  include/lua_pedal.h
  include/plugin_editor.h
  include/textures.h
 "include/amps.h")

target_include_directories(PedalTheCapybara PRIVATE
  third_party
  src
  include
  ${STB_DIR})              # <-- permite #include "stb_image.h" em textures.cpp

target_link_libraries(PedalTheCapybara PRIVATE imgui_lib lua_lib miniz)

if(WIN32)
  # portable-file-dialogs.h (diálogos nativos de abrir/salvar arquivos)
  target_link_libraries(PedalTheCapybara PRIVATE comdlg32 ole32 uuid shell32)
endif()

# Copia plugins, packs e assets para a pasta do executável em builds limpas.
add_custom_command(TARGET PedalTheCapybara POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PedalTheCapybara>/Plugins"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PedalTheCapybara>/Packs")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Plugins")
  add_custom_command(TARGET PedalTheCapybara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/Plugins"
      "$<TARGET_FILE_DIR:PedalTheCapybara>/Plugins")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Packs")
  add_custom_command(TARGET PedalTheCapybara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/Packs"
      "$<TARGET_FILE_DIR:PedalTheCapybara>/Packs")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
  add_custom_command(TARGET PedalTheCapybara POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
      "$<TARGET_FILE_DIR:PedalTheCapybara>/Assets")
endif()
target_compile_definitions(PedalTheCapybara PRIVATE NOMINMAX)

if(MSVC)
  target_compile_options(PedalTheCapybara PRIVATE /utf-8)
  set_target_properties(PedalTheCapybara PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:PedalTheCapybara>")
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(PedalTheCapybara PRIVATE pthread dl m)
endif()