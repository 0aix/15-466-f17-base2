# *Robot Fun Police*

*Robot Fun Police* is *Brian Xiao*'s implementation of [*Robot Fun Police*](http://http://graphics.cs.cmu.edu/courses/15-466-f17/game2-designs/jmccann/) for game2 in 15-466-f17.

![](https://github.com/0aix/15-466-f17-base2/blob/master/screenshots/robot-fun-police.png?raw=true)

## Asset Pipeline

The assets for this game are all contained in [robot.blend](https://github.com/0aix/15-466-f17-base2/blob/master/models/robot.blend). The meshes in the file are exported using a python script ([export-meshes.py](https://github.com/0aix/15-466-f17-base2/blob/master/models/export-meshes.py)) to a blob file storing vertex positions and colors. The scene in the file is exported using the same script to a blob file storing objects and transformations. The meshes and scene are loaded on startup.

## Architecture

The meshes and scene were loaded with the base code. However, because of my shortcomings with blender (and really, complete lack of understanding), I had manually set up hierarchy in the robot arm by hardcoding in the relative transformations and rotations. 

To determine the rotation of each part of the arm, I keep 4 Euler angle vectors (for base, link1, link2, and link3), and I calculate the rotation quaternions from these. To prevent clipping to the best of my abilities, I limit how much each part can rotate. I also prevent any rotations that bring the tip of the arm underneath the platform.

The tip/nail's position is calculated using the local_to_world transform of the "hand" of the arm and a vector <0, 0, 0.5> (empirically taken from blender). Also, the balloons are treated as spheres of radii 0.6 (also taken from blender), and collision is determined by the distance of the tip to a balloon's center/position. 

When a balloon is first popped, the mesh in the object is replaced with its popped version. After 0.2s passes, the object is then actually deleted from the scene.

For the lighting, I used the toon-like lighting from the BRDF, but I left off the specular part since I didn't want to set roughness/shininess. (I did add some ambient color though since the scene felt either too dark or too bright.) It also took me the longest time to figure out that the light vector had to be transformed to camera space. 

## Reflection

The most difficult part of this assignment was trying to set up hierarchy. I wasted a few hours trying to understand what was exactly wrong with the exported coordinates and then manually setting up the robot arm. A lot of time was spent tuning things like clipping/collision and lighting. If I were to do this assignment again, I would figure out how to get blender to do what I actually want (which is provide hierarchy information and local coordinates), though I'll undoubtedly do that for the final project (if it's 3D...). I would probably also change up Scene to make removing objects easier. 

The design document was unclear whether or not the robot arm pieces could pass through itself and through other objects (like the platform underneath). That said, I limited some of the degrees of rotation on the arm, and I prevented the tip/hand from passing through the platform. Also, only the tip of the arm can pop balloons while the rest of the arm will just phase through. 

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
