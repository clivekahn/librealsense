# rs-align Sample

## Overview

This sample demonstrates using the `rs2::align` object which allows users to align the projection of 2 streams.

In this example we align depth frames to their corresponding color frames.  
Then we use the two frames to determine the depth of each color pixel.

Using the depth information we 'remove' the background, beyond a user defined distance, of the color frame furthest from the camera.

The example presents a GUI to regulate the maximum background distance (of the original color image) that will be displayed.

## Expected Output

The application opens a window and displays a video stream from the camera. 

The window has the following elements:
- On the left side of the window is a vertical slider to regulate the depth clipping distance
- A color image with a grayed out background
- A corresponding (colorized) depth image
