# GLSL -> SPIR-V compilation.
#
# Looks for glslc or glslangValidator (both ship with the Vulkan SDK; on Linux
# also in the "shaderc" / "glslang-tools" / "glslang" packages). If neither
# is found, glslang is downloaded and built from source as a fallback, so a
# fresh machine (e.g. a Steam Deck) can still build without extra installs.
#
# Compiled shaders go to <exe dir>/shaders/<name>.spv, e.g. sprite.vert ->
# sprite.vert.spv, loaded at runtime with core::shaderPath("sprite.vert.spv").

find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
find_program(GLSLANG_EXECUTABLE NAMES glslangValidator glslang
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

if(GLSLC_EXECUTABLE)
    message(STATUS "Shader compiler: ${GLSLC_EXECUTABLE}")
elseif(GLSLANG_EXECUTABLE)
    message(STATUS "Shader compiler: ${GLSLANG_EXECUTABLE}")
else()
    message(STATUS "No glslc/glslangValidator found - building glslang from source")
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        16.4.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glslang)
    set(GLSLANG_EXECUTABLE $<TARGET_FILE:glslang-standalone>)
endif()

# target_shaders(<target> OUTPUT_DIR <dir> SOURCES <file>...)
function(target_shaders target)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES" ${ARGN})

    set(outputs "")
    foreach(source ${ARG_SOURCES})
        get_filename_component(source_abs ${source} ABSOLUTE)
        get_filename_component(name ${source} NAME)
        set(output ${ARG_OUTPUT_DIR}/${name}.spv)

        # Rebuild when any shared include (*.glsl) in the shader folder changes.
        get_filename_component(source_dir ${source_abs} DIRECTORY)
        file(GLOB includes CONFIGURE_DEPENDS ${source_dir}/*.glsl)

        if(GLSLC_EXECUTABLE)
            set(command ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 -O -g -o ${output} ${source_abs})
        else()
            set(command ${GLSLANG_EXECUTABLE} -V --target-env vulkan1.3 -g -o ${output} ${source_abs})
        endif()

        add_custom_command(
            OUTPUT ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_OUTPUT_DIR}
            COMMAND ${command}
            DEPENDS ${source_abs} ${includes}
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND outputs ${output})
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${outputs} SOURCES ${ARG_SOURCES})
    set_target_properties(${target}_shaders PROPERTIES FOLDER "build")
    add_dependencies(${target} ${target}_shaders)
endfunction()
