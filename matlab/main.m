clc; 
clear;
clear preferences.getInstance
clear platformControl.getInstance

hPreferences = preferences.getInstance();
% hPreferences.showGUI();

hPlatformControl = platformControl.getInstance();
hPlatformControl.showGUI();