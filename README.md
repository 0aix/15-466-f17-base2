# *Eight-Ball Pool Dozer*

*Eight-Ball Pool Dozer* is *Brian Xiao*'s implementation of [*Pool Dozer*](http://http://graphics.cs.cmu.edu/courses/15-466-f17/game3-designs/jmccann/) for game3 in 15-466-f17.

It is his take on the game while ignoring many of the actual rules of eight-ball.

![](https://github.com/0aix/15-466-f17-base2/blob/game3/screenshots/screenshot.png?raw=true)

## Controls
Left player
- A/S - rotate
- W/D - adjust power
- Left shift - shoot

Right player
- Left/Right - rotate
- Up/Down - adjust power
- Right shift - shoot

Camera
- Click & Drag

## Asset Pipeline

The assets for this game are all contained in [robot.blend](https://github.com/0aix/15-466-f17-base2/blob/game3/models/pool.blend). The meshes in the file are exported using a python script ([export-meshes.py](https://github.com/0aix/15-466-f17-base2/blob/game3/models/export-meshes.py)) to a blob file storing vertex positions and colors. The scene in the file is exported using the same script to a blob file storing objects and transformations. The meshes and scene are loaded on startup.

## Architecture

The meshes and scene were loaded with the base code with a vector of objects being kept.

Position/velocity updates and collision checks are split into <= 1ms time steps.

For the lighting, I use the diffuse toon-like lighting in addition to some ambient lighting.

Draw.cpp from base0 is used to draw a UI.

## Reflection

I wouldn't say this assignment was too hard, but it was definitely long and tedious, and a lot of thought went into re-designing gameplay and making implementation a bit simpler. 
If I were to spend more time on this assignment, I would like to adjust/fine-tune parameters and change the starting configuration of the table. I'd also try implementing the original design.

The design document was unclear on how many rules of eight-ball would work in Pool Dozer. To take a simple route, it would have been fine to let balls simply disappear when they reach the holes and ignore scratches.
I thought that the game would be weird to play (sorry, professor), so I changed up the rules to be turn-based and more like eight-ball.

# About Base2

This game is based on Base2, starter code for game2 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng
 - blender (for mesh export script)

On Linux or OSX these requirements should be available from your package manager without too much hassle.

## Building

This code has been set up to be built with [FT jam](https://www.freetype.org/jam/).

### Getting Jam

For more information on Jam, see the [Jam Documentation](https://www.perforce.com/documentation/jam-documentation) page at Perforce, which includes both reference documentation and a getting started guide.

On unixish OSs, Jam is available from your package manager:
```
	brew install ftjam #on OSX
	apt get ftjam #on Debian-ish Linux
```

On Windows, you can get a binary [from sourceforge](https://sourceforge.net/projects/freetype/files/ftjam/2.5.2/ftjam-2.5.2-win32.zip/download),
and put it somewhere in your `%PATH%`.
(Possibly: also set the `JAM_TOOLSET` variable to `VISUALC`.)

### Bulding
Open a terminal (on windows, a Visual Studio Command Prompt), change to this directory, and type:
```
	jam
```

### Building (local libs)

Depending on your OSX, clone 
[kit-libs-linux](https://github.com/ixchow/kit-libs-linux),
[kit-libs-osx](https://github.com/ixchow/kit-libs-osx),
or [kit-libs-win](https://github.com/ixchow/kit-libs-win)
as a subdirectory of the current directory.

The Jamfile sets up library and header search paths such that local libraries will be preferred over system libraries.
