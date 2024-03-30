cmake_minimum_required(VERSION 3.20)

project(RoseEngine)

# Find dependencies

find_package(Vulkan REQUIRED)
find_package(assimp CONFIG)
find_package(OpenVDB CONFIG)

# download slang release rom github if it doesnt already exist
set(SLANG_ROOT "${CMAKE_CURRENT_LIST_DIR}/thirdparty/slang")
if (NOT EXISTS ${SLANG_ROOT})
	message(STATUS "Downloading slang")
	set(SLANG_URL https://github.com/shader-slang/slang/releases/download/v2024.0.14/slang-2024.0.14)
	if (WIN32)
		file(DOWNLOAD "${SLANG_URL}-win64.zip" "${SLANG_ROOT}.zip")
		message(STATUS "Extracting slang")
		file(ARCHIVE_EXTRACT INPUT "${SLANG_ROOT}.zip" DESTINATION "${SLANG_ROOT}")
		file(REMOVE "${SLANG_ROOT}.zip")
	else()
		file(DOWNLOAD  "${SLANG_URL}-linux-x86_64.zip" "${SLANG_ROOT}.zip")
		message(STATUS "Extracting slang")
		file(ARCHIVE_EXTRACT INPUT "${SLANG_ROOT}.zip" DESTINATION "${SLANG_ROOT}")
		file(REMOVE "${SLANG_ROOT}.zip")
	endif()
endif()

# compiler options
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_definitions(_USE_MATH_DEFINES IMGUI_DEFINE_MATH_OPERATORS VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

add_subdirectory(thirdparty/glfw)
add_subdirectory(thirdparty/glm)

include_directories(thirdparty)
include_directories(src)
link_libraries(Vulkan::Vulkan)
link_libraries(glm)


file(GLOB_RECURSE CORE_SRCS  "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/**.cpp")
file(GLOB_RECURSE RENDER_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/Render/**.cpp")
file(GLOB IMGUI_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/*.cpp")
add_library(RoseLib
	${CORE_SRCS}
	${RENDER_SRCS}
	thirdparty/miniz.c
	thirdparty/pugixml.cpp
	${IMGUI_SRCS}
	thirdparty/ImGuizmo.cpp
	thirdparty/imnodes.cpp)
set_target_properties(RoseLib PROPERTIES LINKER_LANGUAGE CXX)

add_executable(Rose src/main.cpp)
target_link_libraries(Rose RoseLib)

target_link_libraries(RoseLib glfw)
target_include_directories(RoseLib PRIVATE thirdparty/glfw/include thirdparty/glfw/deps)

if(UNIX)
    target_link_libraries(RoseLib pthread ${CMAKE_DL_LIBS} xcb xcb-keysyms)
    add_compile_definitions(VK_USE_PLATFORM_XCB_KHR _GLFW_X11)

	# Link slang
    target_link_libraries(RoseLib "${SLANG_ROOT}/bin/linux-x64/release/libslang.so")
elseif(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR _GLFW_WIN32 WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX)
    add_compile_options(/bigobj /MP)

	# Link slang
    target_link_libraries(RoseLib "${SLANG_ROOT}/bin/windows-x64/release/slang.lib")

	# Copy slang DLLs to output directory
	configure_file("${SLANG_ROOT}/bin/windows-x64/release/slang-glslang.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang-glslang.dll" COPYONLY)
	configure_file("${SLANG_ROOT}/bin/windows-x64/release/slang.dll" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/slang.dll" COPYONLY)
endif()

if (assimp_FOUND)
	if (WIN32)
		target_link_libraries(RoseLib assimp::assimp)
	else()
		target_link_libraries(RoseLib assimp)
	endif()
    target_compile_definitions(RoseLib PUBLIC ENABLE_ASSIMP)
    message(STATUS "Assimp enabled")
endif()

if (OpenVDB_FOUND)
    target_link_libraries(RoseLib OpenVDB::openvdb)
    target_compile_definitions(RoseLib ENABLE_OPENVDB)
    message(STATUS "OpenVDB enabled")
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/thirdparty/imgui/misc/fonts/DroidSans.ttf" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DroidSans.ttf" COPYONLY)


enable_testing()

add_executable(test_program src/tests/program.cpp)
target_link_libraries(test_program RoseLib)
add_test(program bin/test_program)
set_tests_properties(program PROPERTIES PASS_REGULAR_EXPRESSION "SUCCESS")

add_executable(test_mesh src/tests/mesh.cpp)
target_link_libraries(test_mesh RoseLib)
add_test(mesh bin/test_mesh)
set_tests_properties(mesh PROPERTIES PASS_REGULAR_EXPRESSION "SUCCESS")
