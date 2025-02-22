function(AddTest testName testSrcs)
	set(targetName Test${testName})

	add_executable(${targetName} ${testSrcs})
	target_link_libraries(${targetName} RoseLib)
	set_target_properties(${targetName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Tests")

	add_test(${testName} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Tests/${targetName}")
	set_tests_properties(${testName} PROPERTIES PASS_REGULAR_EXPRESSION "SUCCESS")
endfunction()

function(CompileShaderForTest testName srcFile)
	get_filename_component(shaderName ${srcFile} NAME)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_LIST_DIR}/${shaderName}.h"
		COMMAND slangc ${srcFile} -I${CMAKE_SOURCE_DIR}/src -target cpp -o ${shaderName}.h
		DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${shaderName}"
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

	add_custom_target(Generate${shaderName} ALL DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${shaderName}.h")
	add_dependencies(Test${testName} Generate${shaderName})
endfunction()

add_subdirectory(Mesh)
add_subdirectory(Program)
add_subdirectory(RadixSort)