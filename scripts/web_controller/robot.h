// FLORA's controller window: the page it opens and the colours for the connect page.
// She lives on her ESP32 (no ROBOT_START: nothing to start on this PC); FIND gets her address from the fleet list.
#pragma once

#define ROBOT_NAME L"FLORA"
#define ROBOT_NAME_U8 "FLORA"
#define ROBOT_FLEET_NAME "FLORA"
#define ROBOT_HOST L"192.168.4.2"
#define ROBOT_PORT 5018
#define ROBOT_PATH L"/"
#define ROBOT_WIDTH 1000
#define ROBOT_HEIGHT 860

// colour_scheme.xml, for the page shown before there's a robot to show
#define ROBOT_BACKGROUND "#070D09"
#define ROBOT_PANEL "#0E1810"
#define ROBOT_BORDER "#1F3424"
#define ROBOT_TEXT "#EAF2E6"
#define ROBOT_ACCENT "#7BC96F"
