# UEDBot

**UEDBot** is a high-performance Terran StarCraft 2 bot built in C++ that utilizes advanced strategies for both defense and offense. It features dynamic ramp blocking, optimized resource collection, and aggressive rush attacks to dominate opponents. UEDBot recently claimed **1st place** in a tournament of 11 bots, finishing undefeated with a record of **57 wins**, **3 draws**, and **0 losses**.

## Features

- **Dynamic Ramp Blocking**  
  UEDBot blocks key ramps with Supply Depots and a Barracks for early-game defense, preventing enemy units from advancing.

- **Optimized Resource Collection**  
  UEDBot uses SCVs and Mules effectively for fast and efficient mineral and gas collection, ensuring a steady economic advantage.

- **Unit Kiting**  
  Inspired by professional play, UEDBot employs advanced kiting tactics with Marines and Battlecruisers to minimize damage while engaging enemies.

- **Super Fast Rush Attack with Battlecruiser Teleport**  
  UEDBot teleports a Battlecruiser to the enemy base before the 5-minute 30-second mark, followed by reinforcements of Marines and Siege Tanks for relentless pressure.

- **Mixed Defense with Marines, Siege Tanks, and Missile Turrets**  
  UEDBot uses Marines and Siege Tanks for defense while expanding its base with Missile Turrets to counter air threats in the later stages.

## Tournament Achievement

UEDBot is an undefeated champion, securing **1st place** in a tournament of **11 StarCraft 2 bots** with an impressive record of **57 wins**, **3 draws**, and **0 losses**.


# Developer Install / Compile Instructions
## Requirements
* [CMake](https://cmake.org/download/)
* Starcraft 2 ([Windows](https://starcraft2.com/en-us/)) ([Linux](https://github.com/Blizzard/s2client-proto#linux-packages)) 
* [Starcraft 2 Map Packs](https://github.com/Blizzard/s2client-proto#map-packs), The maps we will be using are in the `Ladder 2017 Season 1` pack. Read the instructions for how to extract and where to place the maps.

## Windows

Download and install [Visual Studio 2022](https://www.visualstudio.com/downloads/) if you need it.

```bat
:: Clone the project
$ git clone --recursive https://github.com/Team-UED/UEDBot.git
$ cd UEDBot

:: Create build directory.
$ mkdir build
$ cd build

:: Generate VS solution.
$ cmake ../ -G "Visual Studio 17 2022"

:: Build the project using Visual Studio.
$ start UEDBot.sln
```

## Mac

Note: Try opening the SC2 game client before installing. If the game crashes before opening, you may need to change your Share name:
* Open `System Preferences`
* Click on `Sharing`
* In the `Computer Name` textfield, change the default 'Macbook Pro' to a single word name (the exact name shouldn't matter, as long as its not the default name)

To build, you must use the version of clang that comes with MacOS. 
```bat
:: Clone the project
$ git clone --recursive https://github.com/Team-UED/UEDBot.git
$ cd UEDBot

:: Create build directory.
$ mkdir build
$ cd build

:: Set Apple Clang as the default compiler
export CC=/usr/bin/clang
export CXX=/usr/bin/clang++

:: Generate a Makefile
:: Use 'cmake -DCMAKE_BUILD_TYPE=Debug ../' if debug info is needed
$ cmake -DCMAKE_BUILD_TYPE=Release ../

:: Build
$ make
```

## Linux
The Linux version is headless, meaning that you will not be able to see your bot 
First, download the [Linux package](https://github.com/Blizzard/s2client-proto#linux-packages).
Unzip it to your home directory. 
The directory should read as `/home/<USER>/StarCraftII/`.

Rename the `Maps` directory to lowercase, and place any downloaded maps inside this directory:
```bash
$ mv /home/<USER>/StarCraftII/Maps /home/<USER>/StarCraftII/maps
```

Finally, create a directory (note the added space) which contains a file `ExecuteInfo.txt`, which lists the executable directory:
```bash
$ mkdir "/home/<USER>/StarCraft II"
$ echo "executable = /home/<USER>/StarCraftII/Versions/Base75689/SC2_x64" > "/home/<USER>/StarCraft II/ExecuteInfo.txt"
```
The `Base75689` will need to match the correct version which matches the version you downloaded. To check, navigate to `/home/<USER>/StarCraftII/Versions/`.

Remember to replace `<USER>` with the name of your user profile.

# Playing against the built-in AI

In addition to competing against other bots using the [Sc2LadderServer](https://github.com/solinas/Sc2LadderServer), this bot can play against the built-in
AI by specifying command line argurments.

You can find the build target under the `bin` directory. For example,

```
# Windows
./UEDBot.exe -c -a zerg -d Hard -m CactusValleyLE.SC2Map

# Mac
./UEDBot -c -a zerg -d Hard -m CactusValleyLE.SC2Map
```

will result in the bot playing against the zerg built-in AI on hard difficulty on the map CactusValleyLE.
