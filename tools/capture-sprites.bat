@echo off
rem Sprite filtering capture (faithful pixel-art sprites at high resolution).
rem
rem Play to each screen below. On each one, with sprites on screen, press
rem Print Screen (saves a screenshot) and then 7 (captures 1.5 s of draws).
rem Keep the screen still for the 1.5 s after pressing 7.
rem
rem   1. The Organize screen, with a unit's sprites showing.
rem   2. The world map, with units on it.
rem   3. A battle, with both sides' units on screen.
rem
rem Each press of 7 adds one [modes] table to build-dist\sprite-capture.log:
rem every textured draw with its image, format, tile size, texture filter
rem (point/bilerp) and screen pixels per texel. Screenshots go to
rem build-dist\screenshots. Nothing is written into the game.
rem
rem Then send a message saying the order of the presses.

cd /d "%~dp0..\build-dist"
set OGRE_NO_AUDIO=1
set OGRE_TEX_TRACE=1
set OGRE_TEX_TRACE_MODES=1
set OGRE_TEX_TRACE_KEY=7
set OGRE_TEX_TRACE_MS=1500
set OGRE_SCENE_LOG=1
"%~dp0..\build-dist\ogrebattle64.exe" > sprite-capture.log 2>&1

