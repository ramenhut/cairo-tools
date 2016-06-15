## Cairo Codec Demos
A simple trio of utilities that demonstrate the basic functionality of the Cairo codec. Convert many different types of video files to Cairo EVX files using the *convert* tool, inspect the Cairo encoder state with *inspect*, and play back Cairo files using the *player* tool.

### Open Source Release
The purpose of this release is to serve as an educational resource for students who are interested in video compression. As such, these tools contain only minimalist implementations that rely upon the *unoptimized* version of Cairo to demonstrate a basic compression pipeline without the complexities of optimizations or platform dependencies.

### Usage: convert 
Converts a source video file into a Cairo video file. Source video decoding is accomplished using ffmpeg, so a wide variety of source file formats are supported. *Convert* will compress the content according to the specified quality level. Quality ranges from 0 to 31, with 0 indicating the highest quality (least compression).

> **Usage**: `convert <source file> <quality> <output file>`

### Usage: inspect 
Inspects the state of the Cairo encoder. 

> **Usage**: `inspect <input file>`

### Usage: player 
Plays back a Cairo video file using OpenGL. 

> **Usage**: `player <input file>`

### More Information
For more information, including pre-built binaries, visit [http://www.bertolami.com](http://bertolami.com/index.php?engine=portfolio&content=compression&detail=cairo-tools).
