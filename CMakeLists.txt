cmake_minimum_required(VERSION 3.21)
project(UDPVision VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Qt5 5.14 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(OpenCV REQUIRED COMPONENTS core dnn imgproc imgcodecs videoio)
find_package(OpenMP)

# Backend has no QWidget dependency; the desktop frontend owns presentation.
add_library(vision_backend STATIC
    src/backend/network/UdpReceiver.cpp
    src/backend/network/Ft601Receiver.cpp
    src/backend/processing/UdpFramePipelineWorker.cpp
    src/backend/processing/VideoRecorderWorker.cpp
    src/backend/inference/YoloProcessor.cpp)
target_include_directories(vision_backend PUBLIC
    src/backend/network src/backend/processing src/backend/inference)
target_link_libraries(vision_backend PUBLIC Qt5::Core Qt5::Gui Qt5::Network ${OpenCV_LIBS})
if(OpenMP_CXX_FOUND)
    target_link_libraries(vision_backend PUBLIC OpenMP::OpenMP_CXX)
endif()
if(WIN32)
    target_link_libraries(vision_backend PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(vision_backend PUBLIC /utf-8)
endif()

add_executable(udp-vision WIN32
    src/app/main.cpp src/app/ApplicationLauncher.cpp
    src/frontend/ControlUI.cpp src/frontend/UdpFrameProcessor.cpp)
target_include_directories(udp-vision PRIVATE src/app src/frontend)
target_link_libraries(udp-vision PRIVATE vision_backend Qt5::Widgets)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt5 REQUIRED COMPONENTS Test)
    add_executable(pipeline_tests tests/pipeline_tests.cpp)
    target_link_libraries(pipeline_tests PRIVATE vision_backend Qt5::Test)
    add_test(NAME pipeline COMMAND pipeline_tests)
    add_test(NAME desktop_demo COMMAND udp-vision --demo --smoke-test
        --screenshot ${CMAKE_BINARY_DIR}/demo-smoke.png)
    set_tests_properties(pipeline desktop_demo PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;OMP_NUM_THREADS=2")
endif()

install(TARGETS udp-vision RUNTIME DESTINATION .)
install(FILES models/best.onnx DESTINATION models)
install(FILES assets/demo_reference.png DESTINATION assets)
install(FILES src/backend/inference/onnx_helper.py DESTINATION src/backend/inference)
install(FILES requirements-inference.txt README.md README.zh-CN.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs PATTERN history EXCLUDE PATTERN reports EXCLUDE)
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "udp-vision-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)
