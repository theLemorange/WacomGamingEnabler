# WacomGamingEnabler
An app that enables playing unity games that usually don't recognise wacom tablet inputs
Tested it with 7 Days To Die and GTFO.

For accessability reasons some people cannot use a mouse and are using wacom tablets to play games. Some unity games are using raw mouse inputs and therefore one is not able to control the camera in the game using a wacom tablet. 
This app uses the wintab wacom api to grab the pen data and emulate raw mouse inputs which allows those games to be played.

This will mean that the mouse will be moved twice when outside the game (or in game ui). Once the normal way, and once by the app. 
I recommend going to the wacom tablet properties and under Mapping set the pen to Mouse Mode, and set the mouse speed to the slowest (also mouse acceleration should be off), that way we can mitigate the issue of the mouse being more sensitive outside the game mostly.


download the first version here:
https://github.com/theLemorange/WacomGamingEnabler/releases/tag/v1.0

and do let me know about any issues or improvements that need to be made.
This is my first c++ project and I mainly made this for a friend who is not able to use a mouse. 
If this helped you I'd love to know! 
