cmake_minimum_required(VERSION 2.8) 

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)

project(ql-test-dri) 
add_executable(ql-test-dri dri-test.c)
target_link_libraries(ql-test-dri PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(ql-test-dri PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS ql-test-dri DESTINATION bin)

project(ql-test-getevent) 
add_executable(ql-test-getevent getevent.c)
install (TARGETS ql-test-getevent DESTINATION bin)

project(ql-test-i2c) 
add_executable(ql-test-i2c i2c-test.c)
install (TARGETS ql-test-i2c DESTINATION bin)
