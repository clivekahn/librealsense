# rs-save-to-disk Sample

## Overview

This sample demonstrates how to configure the camera for streaming in a textual environment and save depth and color data to PNG format. It also touches on the subject of capturing [per-frame metadata](../../doc/frame_metadata.md)

## Expected Output
The application runs for about a second and exits after saving PNG and CSV files to disk: 
![expected output](expected_output.PNG)

## Code Overview 

We start by including the Cross-Platform API (as we did in the [first tutorial](../capture/)):
```cpp
#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
```

We use [nothings/stb](https://github.com/nothings/stb) to save data to disk in PNG format: 
```cpp
// 3rd party header for writing png files
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
```

Then we define the depth colorizer and start the pipeline:
```cpp
// Declare depth colorizer for pretty visualization of depth data
rs2::colorizer color_map;

// Declare RealSense pipeline, encapsulating the actual device and sensors
rs2::pipeline pipe;
// Start streaming with default recommended configuration
pipe.start();
```

It is preferable to capture after auto-exposure has stabilized (so we do not save the first frame that arrives from the device):
```cpp
// Capture 30 frames to give autoexposure, etc. a chance to settle
for (auto i = 0; i < 30; ++i) pipe.wait_for_frames();
```

Note that Intel® RealSense™ devices are not limited to video streaming only - some devices offer motion tracking and 6-DOF positioning as well. However, for this example we are only interested only in video frames: 
```cpp
// We can only save video frames as pngs, so we skip the rest
if (auto vf = frame.as<rs2::video_frame>())
```

To better visualize the depth data we apply the colorizer to any incoming depth frame:
```cpp
// Use the colorizer to get an rgb image for the depth stream
if (vf.is<rs2::depth_frame>()) vf = color_map(frame);
```

Then we save the frame data to PNG: 
```cpp
stbi_write_png(png_file.str().c_str(), vf.get_width(), vf.get_height(),
               vf.get_bytes_per_pixel(), vf.get_data(), vf.get_stride_in_bytes());
```

A frame may include metadata field(s).  
We iterate over all possible metadata fields and save those that are available to CSV:
```cpp
// Record all the available metadata attributes
for (size_t i = 0; i < RS2_FRAME_METADATA_COUNT; i++)
{
    if (frm.supports_frame_metadata((rs2_frame_metadata)i))
    {
        csv << rs2_frame_metadata_to_string((rs2_frame_metadata)i) << ","
            << frm.get_frame_metadata((rs2_frame_metadata)i) << "\n";
    }
}
```
Please see [per-frame metadata](../../doc/frame_metadata.md) for more information.
