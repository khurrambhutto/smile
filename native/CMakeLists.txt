cmake_minimum_required(VERSION 3.21)

project(Smile VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Multimedia
    Test
)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(smile
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/SettingsStore.cpp
    src/app/SettingsStore.h
    src/camera/CameraDevice.cpp
    src/camera/CameraDevice.h
    src/camera/CameraFormat.cpp
    src/camera/CameraFormat.h
    src/camera/CameraManager.cpp
    src/camera/CameraManager.h
    src/camera/DeviceManager.cpp
    src/camera/DeviceManager.h
    src/camera/FrameBuffer.cpp
    src/camera/FrameBuffer.h
    src/camera/FrameConverter.cpp
    src/camera/FrameConverter.h
    src/camera/V4L2CaptureSession.cpp
    src/camera/V4L2CaptureSession.h
    src/capture/ImageCaptureHandler.cpp
    src/capture/ImageCaptureHandler.h
    src/capture/VideoRecorder.cpp
    src/capture/VideoRecorder.h
    src/capture/EncoderSettings.cpp
    src/capture/EncoderSettings.h
    src/models/CameraDeviceModel.cpp
    src/models/CameraDeviceModel.h
    src/models/CameraFormatModel.cpp
    src/models/CameraFormatModel.h
)

qt_add_qml_module(smile
    URI Smile
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/components/HeaderBar.qml
        src/qml/components/CameraPreview.qml
        src/qml/components/CaptureControls.qml
        src/qml/components/DeviceSelector.qml
        src/qml/components/FormatSelector.qml
        src/qml/components/SettingsDialog.qml
        src/qml/components/RecordingIndicator.qml
)

target_include_directories(smile PRIVATE src)

target_link_libraries(smile PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

install(TARGETS smile
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

install(FILES packaging/io.github.khurrambhutto.Smile.desktop
    DESTINATION share/applications
)

install(FILES packaging/io.github.khurrambhutto.Smile.metainfo.xml
    DESTINATION share/metainfo
)

enable_testing()

qt_add_executable(smile_tests
    tests/test_core.cpp
    src/camera/CameraFormat.cpp
    src/camera/FrameConverter.cpp
)

target_include_directories(smile_tests PRIVATE src)
target_link_libraries(smile_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME smile_core_tests COMMAND smile_tests)
set_tests_properties(smile_core_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set(CPACK_PACKAGE_NAME "smile")
set(CPACK_PACKAGE_VENDOR "Khurram Bhutto")
set(CPACK_PACKAGE_CONTACT "khurrambhutto")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A fast native Linux camera app")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6, libqt6gui6, libqt6qml6, libqt6quick6, libqt6multimedia6, ffmpeg")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase, qt6-qtdeclarative, qt6-qtmultimedia, ffmpeg")
include(CPack)
