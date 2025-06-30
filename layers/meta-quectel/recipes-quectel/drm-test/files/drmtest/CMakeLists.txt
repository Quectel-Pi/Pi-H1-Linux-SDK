cmake_minimum_required(VERSION 2.8) 

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)

project(modtest-signle-buffer) 
add_executable(modtest-signle-buffer sample-code/drm/application/modeset-single-buffer/modeset-single-buffer.c)
target_link_libraries(modtest-signle-buffer PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modtest-signle-buffer PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modtest-signle-buffer DESTINATION bin)

project(modtest-double-buffer) 
add_executable(modtest-double-buffer sample-code/drm/application/modeset-double-buffer/modeset-double-buffer.c)
target_link_libraries(modtest-double-buffer PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modtest-double-buffer PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modtest-double-buffer DESTINATION bin)

project(modeset-page-flip) 
add_executable(modeset-page-flip sample-code/drm/application/modeset-page-flip/modeset-page-flip.c)
target_link_libraries(modeset-page-flip PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modeset-page-flip PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modeset-page-flip DESTINATION bin)

project(modeset-plane-test) 
add_executable(modeset-plane-test sample-code/drm/application/modeset-plane-test/modeset-plane-test.c)
target_link_libraries(modeset-plane-test PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modeset-plane-test PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modeset-plane-test DESTINATION bin)

project(modeset-atomic-crtc) 
add_executable(modeset-atomic-crtc sample-code/drm/application/modeset-atomic-crtc/modeset-atomic-crtc.c)
target_link_libraries(modeset-atomic-crtc PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modeset-atomic-crtc PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modeset-atomic-crtc DESTINATION bin)

project(modeset-atomic-plane) 
add_executable(modeset-atomic-plane sample-code/drm/application/modeset-atomic-plane/modeset-atomic-plane.c)
target_link_libraries(modeset-atomic-plane PRIVATE ${LIBDRM_LIBRARIES})
target_include_directories(modeset-atomic-plane PRIVATE ${LIBDRM_INCLUDE_DIRS})
install (TARGETS modeset-atomic-plane DESTINATION bin)


