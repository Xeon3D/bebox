/*
 * BeBox emulator for Windows - resource identifiers.
 * Copyright 2026, the BeBox emulator authors. MIT License.
 */
#ifndef WIN32_RESOURCE_H
#define WIN32_RESOURCE_H

#define IDI_BEBOX					1

// Preferences section and tab icons (src/frontend/icons)
#define IDI_PAGE_MACHINE			10
#define IDI_PAGE_ROM				11
#define IDI_PAGE_DISPLAY			12
#define IDI_PAGE_INPUT				13
#define IDI_PAGE_SOUND				14
#define IDI_PAGE_NETWORK			15
#define IDI_PAGE_PORTS				16
#define IDI_PAGE_STORAGE			17
#define IDI_PAGE_PERFORMANCE		18
#define IDI_PAGE_KEYBOARD			19
#define IDI_PAGE_JOYSTICK			20

// Dialogs
#define IDD_PREFS					99
#define IDD_PREFS_MACHINE			100
#define IDD_PREFS_ROMS				101
#define IDD_PREFS_DISPLAY			102
#define IDD_PREFS_STORAGE			103
#define IDD_PREFS_SOUND				104
#define IDD_PREFS_INPUT				105
#define IDD_PREFS_GAMEPORTS			106
#define IDD_PREFS_NETWORK			107
#define IDD_PREFS_SERIAL			108
#define IDD_PREFS_ADVANCED			109
#define IDD_NEW_DISK				120
#define IDD_PROFILE_NAME			121

// Machine page
#define IDC_MODEL					1000
#define IDC_MEMORY					1001
#define IDC_CPUS					1002
#define IDC_BOARD					1003
#define IDC_IO_CARD					1004
#define IDC_SPEED					1005
#define IDC_MACHINE_NOTE			1006

// ROM & NVRAM page
#define IDC_ROM_DIR					1010
#define IDC_ROM_DIR_CHOOSE			1011
#define IDC_BOOT_ROM				1012
#define IDC_BOOT_ROM_CHOOSE			1013
#define IDC_HOBBIT_KERNEL			1014
#define IDC_HOBBIT_KERNEL_CHOOSE	1015
#define IDC_RESET_FLASH				1016
#define IDC_CLEAR_NVRAM				1017

// Display page
#define IDC_VIDEO					1020
#define IDC_VRAM					1021
#define IDC_LED_STYLE				1022
#define IDC_SHOW_LEDS				1023

// Storage page: IDE rows, then SCSI rows (kind, path, choose, read only)
// at IDC_SCSI_KIND + id * 4 ..., then the floppy.
#define IDC_IDE_PATH				1030		// + i * 4
#define IDC_IDE_CHOOSE				1031
#define IDC_IDE_READ_ONLY			1032
#define IDC_SCSI_KIND				1040		// + id * 4
#define IDC_SCSI_PATH				1041
#define IDC_SCSI_CHOOSE				1042
#define IDC_SCSI_READ_ONLY			1043
#define IDC_FLOPPY_PATH				1070
#define IDC_FLOPPY_CHOOSE			1071
#define IDC_FLOPPY_PROTECT			1072
#define IDC_NEW_DISK				1073

// Sound page
#define IDC_SOUND_ENABLED			1080
#define IDC_SOUND_VOLUME			1081

// Keyboard & mouse page
#define IDC_HOST_KEY				1090
#define IDC_MOUSE_SPEED				1091
#define IDC_SWAP_BUTTONS			1092
#define IDC_CAPTURE_ON_CLICK		1093

// Game ports page
#define IDC_STICK					1100		// + i

// Network page
#define IDC_ETHERNET				1110
#define IDC_ETHERNET_PORT			1111
#define IDC_ETHERNET_IRQ			1112
#define IDC_MAC_ADDRESS				1113
#define IDC_PORT_FORWARDS			1114

// Serial page
#define IDC_SERIAL_KIND				1120		// + i * 2
#define IDC_SERIAL_TARGET			1121

// Advanced page
#define IDC_QUANTUM					1130
#define IDC_CPI						1131
#define IDC_THREADS					1132

// New disk dialog
#define IDC_DISK_PATH				1140
#define IDC_DISK_CHOOSE				1141
#define IDC_DISK_SIZE				1142
#define IDC_DISK_FORMAT				1143

// Profile name dialog
#define IDC_PROFILE_NAME			1150
#define IDC_PROFILE_PROMPT			1151

// Preferences window (the pages go on its tabs or in its frame)
#define IDC_PREFS_SIDEBAR			1160
#define IDC_PREFS_ICON				1161
#define IDC_PREFS_TITLE				1162
#define IDC_PREFS_TABS				1163
#define IDC_PREFS_FRAME				1164

#endif	// WIN32_RESOURCE_H
