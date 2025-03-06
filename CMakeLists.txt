cmake_minimum_required(VERSION 3.19)
project(Shadowfax LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

qt_add_executable(Shadowfax
    main.cpp
    Bot/bot.cpp Bot/bot.h
)

target_link_libraries(Shadowfax
    PRIVATE
        Qt::Core
        Qt::Network  # 🔹 Підключаємо бібліотеку Network
)

include(GNUInstallDirs)

install(TARGETS Shadowfax
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Shadowfax
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
