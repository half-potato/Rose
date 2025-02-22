cmake_minimum_required(VERSION 3.23)

project(RoseEngine)

set(ROSE_ENABLE_TESTING ON CACHE BOOL ON)
set(ROSE_BUILD_APPS     ON CACHE BOOL ON)

# Find dependencies

find_package(Vulkan REQUIRED)

# compiler options
set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_compile_definitions(_USE_MATH_DEFINES GLM_FORCE_XYZW_ONLY IMGUI_DEFINE_MATH_OPERATORS VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)

add_subdirectory(thirdparty/glfw)
add_subdirectory(thirdparty/glm)

include_directories(thirdparty)
include_directories(src)
link_libraries(Vulkan::Vulkan)
link_libraries(glm)

add_library(RoseLib)

file(GLOB_RECURSE CORE_SRCS  "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.cpp")
file(GLOB_RECURSE CORE_HPP   "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.hpp")
file(GLOB_RECURSE CORE_H     "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.h")
file(GLOB_RECURSE CORE_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/src/*/**.slang")
file(GLOB         IMGUI_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/*.cpp")

target_sources(RoseLib
	PUBLIC
		${CORE_SRCS}
		${IMGUI_SRCS}
		thirdparty/ImGuizmo.cpp
		thirdparty/imnodes.cpp
		thirdparty/miniz.c
		thirdparty/pugixml.cpp
		thirdparty/tinyply.cpp
	PUBLIC FILE_SET HEADERS
	BASE_DIRS src/
	FILES
		${CORE_HPP}
		${CORE_H}
		${CORE_SLANG}
)

set_target_properties(RoseLib PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(RoseLib glfw $ENV{VULKAN_SDK}/Lib/slang.lib)
target_include_directories(RoseLib PRIVATE thirdparty/glfw/include thirdparty/glfw/deps)

if(UNIX)
    target_link_libraries(RoseLib pthread ${CMAKE_DL_LIBS} xcb xcb-keysyms)
    add_compile_definitions(VK_USE_PLATFORM_XCB_KHR _GLFW_X11)
elseif(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR _GLFW_WIN32 WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS NOMINMAX)
    add_compile_options(/bigobj /MP)
endif()

find_package(assimp CONFIG)
if (assimp_FOUND)
	if (WIN32)
		target_link_libraries(RoseLib assimp::assimp)
	else()
		target_link_libraries(RoseLib assimp)
	endif()
    target_compile_definitions(RoseLib PUBLIC ENABLE_ASSIMP)
    message(STATUS "Assimp enabled")
endif()

find_package(OpenVDB CONFIG)
if (OpenVDB_FOUND)
    target_link_libraries(RoseLib OpenVDB::openvdb)
    target_compile_definitions(RoseLib PRIVATE ENABLE_OPENVDB)
    message(STATUS "OpenVDB enabled")
endif()

if (ROSE_BUILD_APPS)
	add_executable(SceneApp src/SceneApp.cpp)
	target_link_libraries(SceneApp PRIVATE RoseLib)

	add_executable(WorkGraphApp src/WorkGraphApp.cpp)
	target_link_libraries(WorkGraphApp PRIVATE RoseLib)
endif()

if (ROSE_ENABLE_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/thirdparty/imgui/misc/fonts/DroidSans.ttf" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/DroidSans.ttf" COPYONLY)

install(TARGETS RoseLib FILE_SET HEADERS)