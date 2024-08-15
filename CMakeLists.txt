cmake_minimum_required(VERSION 3.16)
project(microamp C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

set(TP ${CMAKE_SOURCE_DIR}/third_party)
foreach(d FreeRTOS-Kernel nanopb Unity)
  if(NOT EXISTS ${TP}/${d})
    message(FATAL_ERROR "third_party/${d} missing - run scripts/bootstrap.sh first")
  endif()
endforeach()

# ---------------- portable firmware core (no RTOS, no heap, no float) ----------------
add_library(fwcore STATIC
  firmware/ml/features.c firmware/ml/nn_int8.c firmware/ml/detector_core.c
  firmware/model/model_data.c firmware/power/energy.c firmware/drivers/accel_driver.c)
target_include_directories(fwcore PUBLIC firmware/ml firmware/model firmware/power
  firmware/drivers firmware/hal)

add_library(nanopb_rt STATIC ${TP}/nanopb/pb_common.c ${TP}/nanopb/pb_encode.c ${TP}/nanopb/pb_decode.c)
target_include_directories(nanopb_rt PUBLIC ${TP}/nanopb)
add_library(proto STATIC firmware/proto/microamp.pb.c)
target_link_libraries(proto PUBLIC nanopb_rt)
target_include_directories(proto PUBLIC firmware/proto)

# ---------------- FreeRTOS POSIX simulator port ----------------
set(FR ${TP}/FreeRTOS-Kernel)
add_library(freertos STATIC
  ${FR}/tasks.c ${FR}/queue.c ${FR}/list.c
  ${FR}/portable/ThirdParty/GCC/Posix/port.c
  ${FR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c)
target_include_directories(freertos PUBLIC ${FR}/include
  ${FR}/portable/ThirdParty/GCC/Posix ${FR}/portable/ThirdParty/GCC/Posix/utils firmware/app)
target_compile_options(freertos PRIVATE -Wno-error -w)
target_link_libraries(freertos PUBLIC pthread)

add_executable(microamp
  firmware/app/main.c firmware/app/sampler.c firmware/app/detector_task.c
  firmware/app/reporter.c firmware/app/tickless.c firmware/app/design.c
  firmware/hal/hal_posix_sim.c)
target_include_directories(microamp PRIVATE firmware/app firmware/hal)
target_compile_definitions(microamp PRIVATE
  MICROAMP_DEFAULT_SIM_DIR="${CMAKE_SOURCE_DIR}/sim")
target_link_libraries(microamp fwcore proto freertos)

# ---------------- model size / cycle report ----------------
add_executable(ml_bench tools/ml_bench.c)
target_link_libraries(ml_bench fwcore)

# ---------------- unit tests (Unity) ----------------
enable_testing()
add_library(unity STATIC ${TP}/Unity/src/unity.c)
target_include_directories(unity PUBLIC ${TP}/Unity/src)
foreach(t driver energy features nn detector pb)
  add_executable(test_${t} tests/c/test_${t}.c)
  target_link_libraries(test_${t} unity fwcore proto)
  target_include_directories(test_${t} PRIVATE tests/c)
  add_test(NAME c_${t} COMMAND test_${t})
endforeach()
