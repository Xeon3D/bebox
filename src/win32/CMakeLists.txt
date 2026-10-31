# BeBox emulator for Windows: the application (BeBox.exe).

add_executable(BeBox WIN32
	main.cpp
	KeyMap.cpp
	LedColumn.cpp
	MainWindow.cpp
	PrefsDialog.cpp
	ScreenView.cpp
	SerialConsole.cpp
	Settings.cpp
	Util.cpp
	WinAudio.cpp
	BeBox.rc)

# wWinMain; Windows 10 1607 and later (GetDpiForWindow).
target_compile_definitions(BeBox PRIVATE UNICODE _UNICODE
	_WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_options(BeBox PRIVATE -municode)
target_link_libraries(BeBox PRIVATE bebox_core comctl32 comdlg32 winmm
	shell32 ole32 uuid gdi32)
set_source_files_properties(BeBox.rc PROPERTIES OBJECT_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/BeBox.ico;${CMAKE_CURRENT_SOURCE_DIR}/BeBox.manifest")
