cmake_minimum_required(VERSION 3.10)

add_executable(simplidfs-tests.exe)

target_sources(simplidfs-tests.exe
	PRIVATE
	tests_main.cpp
	
)

target_link_libraries(simplidfs-tests.exe
	PRIVATE

)

target_include_directories(simplidfs-tests.exe
	PRIVATE
	${PROJECT_SOURCE_DIR}/src	
)

include(CTest)
add_test(NAME simplidfs-tests.exe COMMAND simplidfs-tests.exe)
