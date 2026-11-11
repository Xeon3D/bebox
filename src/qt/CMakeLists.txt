# BeBox emulator for Qt: the application on Windows, Linux, macOS and
# Haiku (Qt 6.5 or later). Sound with Qt Multimedia, joysticks with SDL2,
# when found.

set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(Qt6 6.5 QUIET COMPONENTS Multimedia)
pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)

set(SOURCES
	main.cpp
	AudioOutput.cpp
	Joysticks.cpp
	KeyMap.cpp
	LedWidget.cpp
	MainWindow.cpp
	PrefsDialog.cpp
	ScreenWidget.cpp
	SerialConsole.cpp)
if(HAIKU)
	# the shared folders keep BeOS's attributes in Haiku's
	list(APPEND SOURCES ../haiku/HaikuAttributes.cpp)
endif()

if(APPLE)
	add_executable(BeBoxQt MACOSX_BUNDLE ${SOURCES})
	set_target_properties(BeBoxQt PROPERTIES OUTPUT_NAME BeBox
		MACOSX_BUNDLE_BUNDLE_NAME BeBox
		MACOSX_BUNDLE_GUI_IDENTIFIER org.bebox.emulator
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
elseif(WIN32)
	# BeBox.exe is the Win32 application; this one is BeBoxQt.exe
	add_executable(BeBoxQt WIN32 ${SOURCES} ../win32/BeBox.rc)
	target_include_directories(BeBoxQt PRIVATE ../win32)
else()
	add_executable(BeBoxQt ${SOURCES})
	set_target_properties(BeBoxQt PROPERTIES OUTPUT_NAME BeBox)
endif()

target_link_libraries(BeBoxQt PRIVATE bebox_frontend bebox_core Qt6::Widgets)
if(Qt6Multimedia_FOUND)
	target_compile_definitions(BeBoxQt PRIVATE HAVE_QT_MULTIMEDIA)
	target_link_libraries(BeBoxQt PRIVATE Qt6::Multimedia)
else()
	message(STATUS "Qt Multimedia not found: the Qt application is silent")
endif()
if(SDL2_FOUND)
	target_compile_definitions(BeBoxQt PRIVATE HAVE_SDL2)
	target_link_libraries(BeBoxQt PRIVATE PkgConfig::SDL2)
else()
	message(STATUS "SDL2 not found: the Qt application has no joysticks")
endif()
if(WIN32)
	target_link_libraries(BeBoxQt PRIVATE winmm)
elseif(HAIKU)
	target_link_libraries(BeBoxQt PRIVATE be)
endif()

# the page and tab icons
file(GLOB ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../frontend/icons/*.png)
qt_add_resources(BeBoxQt icons PREFIX /icons
	BASE ../frontend/icons
	FILES ${ICONS})
