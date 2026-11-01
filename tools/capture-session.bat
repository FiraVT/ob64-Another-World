@echo off
rem Play session for War Director research (docs/anotherworld/design-war-director.md, Phase 0).
rem
rem Press 9 at each moment listed below. Every press writes the whole game
rem memory to build-dist\ram-<n>-full.bin (numbered in order), and the log of
rem the session goes to build-dist\capture.log. Nothing is written into the game.
rem
rem   1. On a mission's field, before liberating a town or temple.
rem   2. Right after that liberation.
rem   3. One in-game hour later (watch the clock), without moving.
rem   4. Just before a scripted enemy unit appears (for example Tenne Plains:
rem      the Knight from Buchanan after Mulsuk is liberated).
rem   5. Right after it appears.
rem
rem Then send a message saying which press was which.

cd /d "%~dp0..\build-dist"
set OGRE_RAM_KEY=9
set OGRE_RAM_KEY_FULL=1
set OGRE_SCENE_LOG=1
ogrebattle64.exe > capture.log 2>&1

