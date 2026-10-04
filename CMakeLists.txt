cmake_minimum_required(VERSION 3.21)

project(AX25Chess VERSION 2.0.3 DESCRIPTION "Chess over AX.25 packet radio with an embedded Dire Wolf modem" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Android's integer version, monotonic across releases: 2.0.0 -> 20000.
math(EXPR AX25CHESS_VERSION_CODE "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")

option(AX25CHESS_BUILD_APP "Build the application (Qt Quick)" ON)
option(AX25CHESS_BUILD_TESTS "Build the unit tests (needs Qt6::Test)" ON)

# ---- Embedded Dire Wolf core --------------------------------------------
if(MSVC)
  # Microsoft's math.h only defines M_PI with this; clang-cl uses that math.h.
  # And the CRT's deprecation notices for strncpy and friends are noise here.
  add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
  # The sources are UTF-8 (the French catalog): say so to the compiler
  # rather than let it guess from the code page.
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # clang-cl links with lld-link.  Qt gives every executable a manifest
    # (longPathAware, asInvoker), which CMake hands to the linker as
    # /MANIFESTINPUT; lld-link then merges its own trustInfo block into it
    # and, in the versions shipped with Visual Studio, prefixes the merged
    # attributes with an XML namespace (ms_asmv1:level) that Windows rejects:
    # the program fails to start with "side-by-side configuration is
    # incorrect" (error 14001).  Qt's own fix, applied here as well: no
    # linker trustInfo block at all - the manifest already declares asInvoker.
    add_link_options(/MANIFESTUAC:NO)
  endif()
endif()

include(cmake/DirewolfCore.cmake)

# Qt 6.4 is what Ubuntu 24.04 ships: nothing here, in C++ or in QML, needs
# more than that.  Android and Windows use a newer Qt from qt.io.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

# ---- Qt engine around the core, AX.25 frames, channel access -------------
add_library(ax25chess_radio STATIC
  src/engine/DirewolfEngine.h
  src/engine/DirewolfEngine.cpp
  src/engine/AndroidPtt.h
  src/engine/AndroidPtt.cpp
  src/engine/AndroidNotify.h
  src/engine/AndroidNotify.cpp
  src/protocol/AX25Frame.h
  src/protocol/AX25Frame.cpp
  src/protocol/DirewolfHeaders.h
  src/radio/ChannelManager.h
  src/radio/ChannelManager.cpp
  src/radio/RadioConfig.h
  src/radio/RadioConfig.cpp
  src/radio/ModemConfigFile.h
  src/radio/ModemConfigFile.cpp
  src/radio/AudioDevices.h
  src/radio/AudioDevices.cpp
  src/radio/RadioLink.h
  src/radio/RadioLink.cpp
)
target_include_directories(ax25chess_radio PUBLIC src/engine src/protocol src/radio)
target_link_libraries(ax25chess_radio PUBLIC Qt6::Core direwolf_core)
if(WIN32)
  target_link_libraries(ax25chess_radio PRIVATE winmm)    # the sound card list
endif()

# ---- Chess rules, the CHS-1 protocol, saved games -----------------------
add_library(ax25chess_chess STATIC
  src/chess/Board.h
  src/chess/Board.cpp
  src/chess/GameSession.h
  src/chess/GameSession.cpp
  src/chess/GameStore.h
  src/chess/GameStore.cpp
)
target_include_directories(ax25chess_chess PUBLIC src/chess)
target_link_libraries(ax25chess_chess PUBLIC Qt6::Core)

if(AX25CHESS_BUILD_TESTS AND NOT ANDROID)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()
  add_executable(tst_chess tests/tst_chess.cpp)
  target_compile_definitions(tst_chess PRIVATE TEST_VECTORS="${CMAKE_SOURCE_DIR}/tests/vectors/python_vectors.json")
  target_link_libraries(tst_chess PRIVATE Qt6::Test ax25chess_chess ax25chess_radio)
  add_test(NAME chess COMMAND tst_chess)
  add_executable(tst_radio tests/tst_radio.cpp)
  target_compile_definitions(tst_radio PRIVATE TEST_CONF_DIR="${CMAKE_SOURCE_DIR}/conf")
  target_link_libraries(tst_radio PRIVATE Qt6::Test ax25chess_chess ax25chess_radio)
  add_test(NAME radio COMMAND tst_radio)
  # End to end over the real modem: audio made by Dire Wolf's gen_packets
  # (its own build), decoded by the embedded core.  Skipped without it.
  add_executable(tst_modem tests/tst_modem.cpp)
  target_link_libraries(tst_modem PRIVATE ax25chess_chess ax25chess_radio)
  find_program(GEN_PACKETS gen_packets)
  if(GEN_PACKETS)
    add_test(NAME modem COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_modem_test.sh ${CMAKE_BINARY_DIR} ${GEN_PACKETS})
  endif()
endif()

# ---- The application ---------------------------------------------------------
if(AX25CHESS_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS Gui Quick QuickControls2)
  # Android deployment paths are plain strings here; say so to Qt.
  if(QT_KNOWN_POLICY_QTP0002)
    qt_policy(SET QTP0002 NEW)
  endif()

  qt_add_executable(ax25chess
    src/app/main.cpp
    src/app/AppController.h
    src/app/AppController.cpp
    src/app/AppConfig.h
    src/app/AppConfig.cpp
    src/app/BoardView.h
    src/app/BoardView.cpp
    src/app/ListModels.h
    src/app/ListModels.cpp
    src/app/Catalog.h
    src/app/Catalog.cpp
    src/app/AndroidUi.h
    src/app/AndroidUi.cpp
  )
  target_include_directories(ax25chess PRIVATE src/app)
  target_compile_definitions(ax25chess PRIVATE AX25CHESS_VERSION="${PROJECT_VERSION}")

  set(AX25CHESS_QML_FILES
    src/qml/Main.qml
    src/qml/BoardPanel.qml
    src/qml/CapturesBar.qml
    src/qml/GamePage.qml
    src/qml/ChatPage.qml
    src/qml/LogPage.qml
    src/qml/SettingsPage.qml
    src/qml/GamesDialog.qml
    src/qml/PromotionDialog.qml
    src/qml/MessageDialog.qml
    src/qml/ConfEditor.qml
    src/qml/Card.qml
    src/qml/AccentButton.qml
    src/qml/WrapSwitch.qml
    src/qml/WideButton.qml
    src/qml/Field.qml
    src/qml/UpperField.qml
    src/qml/Led.qml
    src/qml/SectionTitle.qml
  )
  # The files sit in src/qml but must land at the module's root
  # (qrc:/qt/qml/AX25Chess/Main.qml), hence the aliases.  The URL form of
  # the load works on every Qt 6; loadFromModule() only from 6.5.
  foreach(_qml ${AX25CHESS_QML_FILES})
    get_filename_component(_name ${_qml} NAME)
    set_source_files_properties(${_qml} PROPERTIES QT_RESOURCE_ALIAS ${_name})
  endforeach()
  qt_add_qml_module(ax25chess
    URI AX25Chess
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${AX25CHESS_QML_FILES}
  )
  # The pieces come from a font compiled in: identical on every platform,
  # and never an invisible piece for want of a glyph (assets/fonts/README.md).
  qt_add_resources(ax25chess "assets" PREFIX "/" BASE assets
    FILES assets/ax25chess.png assets/fonts/AX25ChessPieces.ttf)
  # Dire Wolf's tables, written to the data folder at start-up (main.cpp):
  # one way for every platform, nothing to install beside the program.
  qt_add_resources(ax25chess "direwolf_data" PREFIX "/direwolf-data" BASE ${DIREWOLF_DIR}/data
    FILES ${DIREWOLF_DIR}/data/tocalls.yaml ${DIREWOLF_DIR}/data/symbols-new.txt)
  if(WIN32)
    target_sources(ax25chess PRIVATE assets/ax25chess.rc)
  endif()
  set_target_properties(ax25chess PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
  target_link_libraries(ax25chess PRIVATE Qt6::Gui Qt6::Quick Qt6::QuickControls2 ax25chess_chess ax25chess_radio)

  if(ANDROID)
    # android/ holds the Java side (USB PTT, the service that keeps the
    # station running off screen, notifications) and the launcher icon; the
    # manifest is Qt's own template, filled from these properties and
    # completed by build_android.sh.
    set_target_properties(ax25chess PROPERTIES
      QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
      QT_ANDROID_PACKAGE_NAME "org.ax25chess"
      QT_ANDROID_APP_NAME "AX25Chess"
      QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
      QT_ANDROID_VERSION_CODE "${AX25CHESS_VERSION_CODE}"
      QT_ANDROID_APP_ICON "@mipmap/ic_launcher"
      QT_ANDROID_MIN_SDK_VERSION 28
      QT_ANDROID_TARGET_SDK_VERSION 35)
    if(COMMAND qt_add_android_permission)
      qt_add_android_permission(ax25chess NAME android.permission.RECORD_AUDIO)
      qt_add_android_permission(ax25chess NAME android.permission.MODIFY_AUDIO_SETTINGS)
      qt_add_android_permission(ax25chess NAME android.permission.WAKE_LOCK)
      qt_add_android_permission(ax25chess NAME android.permission.POST_NOTIFICATIONS)
      qt_add_android_permission(ax25chess NAME android.permission.ACCESS_WIFI_STATE)
      qt_add_android_permission(ax25chess NAME android.permission.FOREGROUND_SERVICE)
      qt_add_android_permission(ax25chess NAME android.permission.FOREGROUND_SERVICE_MICROPHONE)
      qt_add_android_permission(ax25chess NAME android.permission.FOREGROUND_SERVICE_SPECIAL_USE)
      qt_add_android_permission(ax25chess NAME android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS)
    else()
      message(WARNING "This Qt has no qt_add_android_permission(); add RECORD_AUDIO and the foreground service permissions to the manifest by hand.")
    endif()
  endif()

  # ---- install ---------------------------------------------------------------
  # Not on Android: there the program is a shared library that
  # androiddeployqt puts in the APK, and install() would want a LIBRARY
  # destination it has no use for.
  include(GNUInstallDirs)
  if(NOT ANDROID)
    install(TARGETS ax25chess RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
  endif()
  if(UNIX AND NOT APPLE AND NOT ANDROID)
    install(FILES README.md LICENSE.txt CHANGELOG.md docs/PROTOCOL.md
            DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ax25chess)
    set(BINDIR ${CMAKE_INSTALL_FULL_BINDIR})
    configure_file(packaging/desktop/ax25chess.desktop.in ${CMAKE_BINARY_DIR}/ax25chess.desktop @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/ax25chess.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(DIRECTORY packaging/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
    string(TIMESTAMP AX25CHESS_RELEASE_DATE "%Y-%m-%d" UTC)
    configure_file(packaging/appstream/io.github.ax25chess.AX25Chess.metainfo.xml.in
                   ${CMAKE_BINARY_DIR}/io.github.ax25chess.AX25Chess.metainfo.xml @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/io.github.ax25chess.AX25Chess.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    # Manual page and changelog, compressed as the policy asks (a native
    # package, so the file is changelog.gz, not changelog.Debian.gz).
    find_program(GZIP gzip)
    if(GZIP)
      add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/ax25chess.1.gz
        COMMAND ${GZIP} -9 -n -c ${CMAKE_SOURCE_DIR}/packaging/man/ax25chess.1 > ${CMAKE_BINARY_DIR}/ax25chess.1.gz
        DEPENDS ${CMAKE_SOURCE_DIR}/packaging/man/ax25chess.1 VERBATIM)
      add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/changelog.gz
        COMMAND ${GZIP} -9 -n -c ${CMAKE_SOURCE_DIR}/packaging/deb/changelog > ${CMAKE_BINARY_DIR}/changelog.gz
        DEPENDS ${CMAKE_SOURCE_DIR}/packaging/deb/changelog VERBATIM)
      add_custom_target(compressed_docs ALL
        DEPENDS ${CMAKE_BINARY_DIR}/ax25chess.1.gz ${CMAKE_BINARY_DIR}/changelog.gz)
      install(FILES ${CMAKE_BINARY_DIR}/ax25chess.1.gz DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
      install(FILES ${CMAKE_BINARY_DIR}/changelog.gz DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ax25chess)
    endif()
    install(FILES packaging/deb/copyright DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/ax25chess)
    # A literal path: Debian qualifies CMAKE_INSTALL_LIBDIR with the
    # architecture, and udev only looks in lib/udev/rules.d.
    install(FILES packaging/udev/99-ax25chess-cm108.rules DESTINATION lib/udev/rules.d)

    # ---- Debian package ----------------------------------------------------------
    # Dependencies are derived by dpkg-shlibdeps from the libraries actually
    # linked.  The QML modules are loaded at run time and linked by nothing,
    # so no tool can see them: they are listed here, or the program opens an
    # empty window and says nothing.
    set(CPACK_PACKAGE_NAME "ax25chess")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "AX25Chess Community <noreply@ax25chess.invalid>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Chess over AX.25 packet radio with a built-in modem")
    set(CPACK_PACKAGE_DESCRIPTION
"Point-to-point chess between two amateur radio stations over AX.25 UI\n\
 frames, with the CHS-1 protocol: stop-and-wait reliability, a position\n\
 fingerprint in every move and resynchronisation by replaying the history.\n\
 The modem is Dire Wolf, compiled into the program: a sound card and a PTT\n\
 line are all the hardware needed.")
    set(CPACK_STRIP_FILES ON)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire-alsa | pulseaudio-utils")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_SOURCE_DIR}/packaging/deb/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
    set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
    include(CPack)
  endif()

  if(AX25CHESS_BUILD_TESTS AND NOT ANDROID)
    # The QML under real pointer events, from the source tree.
    add_executable(tst_ui tests/tst_ui.cpp
      src/app/AppController.h src/app/AppController.cpp
      src/app/AppConfig.h src/app/AppConfig.cpp
      src/app/BoardView.h src/app/BoardView.cpp
      src/app/ListModels.h src/app/ListModels.cpp
      src/app/Catalog.h src/app/Catalog.cpp
      src/app/AndroidUi.h src/app/AndroidUi.cpp)
    target_include_directories(tst_ui PRIVATE src/app)
    target_compile_definitions(tst_ui PRIVATE AX25CHESS_VERSION="${PROJECT_VERSION}"
      AX25CHESS_QML_DIR="${CMAKE_SOURCE_DIR}/src/qml" AX25CHESS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    qt_add_resources(tst_ui "tst_assets" PREFIX "/" BASE assets
      FILES assets/ax25chess.png assets/fonts/AX25ChessPieces.ttf)
    target_link_libraries(tst_ui PRIVATE Qt6::Test Qt6::Gui Qt6::Quick Qt6::QuickControls2 ax25chess_chess ax25chess_radio)
    add_test(NAME ui COMMAND tst_ui)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()
endif()
