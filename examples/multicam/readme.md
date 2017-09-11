# rs-multicam sample

## Overview

The multicam sample demonstrates using the SDK for streaming and rendering multiple devices.

## Expected Output

The application opens a window that is split to a number of view ports (at least one per camera).  
Each part of the split window displays a single stream from a different camera.  
The name of the stream appears in the top left corner of each view port. 

In the following example we've used two Intel® RealSense™ Depth Cameras pointing at different locations:

<p align="center"><img src="example_screenshot.gif" alt="screenshot gif"/></p>

> Note: The above animation was cropped to display only the depth streams.

## Code Overview 

We start by including the Intel RealSense Cross Platform API:

```cpp
#include <librealsense2/rs.hpp>     // Include RealSense Cross Platform API
```

In this example we will also use the auxiliary library of `example.hpp`:

```cpp
#include "example.hpp"              // Include short list of convenience functions for rendering
```

`examples.hpp` lets us easily open a new window and prepare textures for rendering.

We define an additional class to help manage the state of the application and the connected devices:
```cpp
class device_container{ ... }
```
We will elaborate on `device_container` later in this overview.

The first object we use is a `window` that will be used to display the images from all the cameras:

```cpp
// Create a simple OpenGL window for rendering:
window app(1280, 960, "CPP Multi-Camera Example");
```

The `window` class resides in `example.hpp` and lets us easily open a new window to prepare textures for rendering.

Next, we declare a `device_container ` object:
```cpp
device_container connected_devices;
```
This object encapsulates the connected cameras.  
It is used to manage the state of the program and all operations related to the cameras and rendering of frames.

The last variable we declare is `rs2::context`.

```cpp
rs2::context ctx;    // Create librealsense context for managing devices
```
`context` encapsulates all of the devices and sensors and provides some additional functionalities.
In this example we will ask the `context` to notify us of device changes (connections and disconnections).
 
```cpp
// Register callback for tracking which devices are currently connected
ctx.set_devices_changed_callback([&](rs2::event_information& info)
{
    connected_devices.remove_devices(info);
    for (auto&& dev : info.get_new_devices())
    {
        connected_devices.enable_device(dev);
    }
});
```
Using `set_devices_changed_callback` we can register any callable object that accepts `event_information&` as its parameter.
`event_information` contains a list of devices that were disconnected and the list of the currently connected devices.
In this case, once the `context` notifies us about a change, we update `connected_devices` about the device removed and order it to start streaming from new devices.

After we registered to get notification from the `context`, we query it for all connected devices, and add each device:
```cpp
// Initial population of the device list
for (auto&& dev : ctx.query_devices()) // Query the list of connected RealSense devices
{
    connected_devices.enable_device(dev);
}
```
`enable_device` adds the device to the internal list of devices in use and starts it.  
Each device is wrapped with a `rs2::pipeline` to easily configure and start streaming from it. 

The `pipeline` holds a queue of the latest frames and allows polling from this queue - we will use it later on.

After adding the device we begin our main loop of the application.
In this loop we display all the streams from all the devices.

```cpp
while (app) // Application still alive?
{
```

In every iteration we first try to poll all available frames from all device:

```cpp
   connected_devices.poll_frames();
```
Diving into `device_container::poll_frames` :
```cpp
void poll_frames()
{
    std::lock_guard<std::mutex> lock(_mutex);
    // Go over all device
    for (auto&& view : _devices)
    {
        // Ask each pipeline if there are new frames available
        rs2::frame f;
        if (view.second.pipe.poll_for_frame(&f))
        {
```

In the above code, we go over all devices, which are actually `view_port`s.
`view_port` is a helper struct which encapsulates a pipeline, its latest polled frames, and 2 other utilities that allow converting depth stream to color (`colorizer`) and to render frames to the window (`texture`).
For each pipeline, we try to poll for frames. 

```cpp
            if (auto frameset = f.as<rs2::composite_frame>())
            {
                for (int i = 0; i < frameset.size(); i++)
                {
                    rs2::frame new_frame = frameset[i];
                    int stream_id = new_frame.get_profile().unique_id();
                    view.second.frames_per_stream[stream_id] = view.second.colorize_frame(new_frame); //update view port with the new stream
                }
            }
        }
    }
}
```
A `rs2::frame` is 'extendable' to a `composite_frame` which holds more than a single type of frame.
In the above code, we expect the pipeline to return composite frames and store each frame instead of the previously polled frame.
The stored frames will be used next to count the number of active streams and for rendering to screen.
 
Back to main...

After polling for frames we count the number of active streams and display an appropriate message to the user:
```cpp
    auto total_number_of_streams = connected_devices.stream_count();
    if (total_number_of_streams == 0)
    {
        draw_text(std::max(0.f, (app.width() / 2) - no_cammera_message.length() * 3), app.height() / 2, no_cammera_message.c_str());
        continue;
    }
    if (connected_devices.device_count() == 1)
    {
        draw_text(0, 10, "Please connect another camera");
    }
```
We calculate the alignment of the view ports in the window according to the number of streams:

```cpp
    int cols = std::ceil(std::sqrt(total_number_of_streams));
    int rows = std::ceil(total_number_of_streams / static_cast<float>(cols));

    float view_width = (app.width() / cols);
    float view_height = (app.height() / rows);
```

And finally we call `render_textures`:
```cpp
    connected_devices.render_textures(cols, rows, view_width, view_height);
```


In `device_container::render_textures` we go over all the stored frames:

```cpp
void render_textures(int cols, int rows, float view_width, float view_height)
{
    std::lock_guard<std::mutex> lock(_mutex);
    int stream_no = 0;
    for (auto&& view : _devices)
    {
        // For each device get its frames
        for (auto&& id_to_frame : view.second.frames_per_stream)
        {
```

If a new frame was stored, we upload it to the graphics buffer:

```cpp
            // If the frame is available
            if (id_to_frame.second)
            {
                view.second.tex.upload(id_to_frame.second);
            }
```

Next, we calculate the correct location of the current stream:
```cpp
            rect frame_location{ view_width * (stream_no % cols), view_height * (stream_no / cols), view_width, view_height };
```            

And finally, since this example shows multi-**camera** streaming, we verify that the stream is of video frames. 
Note that the library supports streaming of video, motion information, and other.

As with the `composite_frame` that we saw earlier, a `frame` can also be a  `rs2::video_frame` which represents a camera image and has additional properties such as width and height.
```cpp
            if (rs2::video_frame vid_frame = id_to_frame.second.as<rs2::video_frame>())
            {
```
We use the width and height of the frame to scale it into the view port: **CHANGE "adjuested" be adjusted??**
```cpp
               rect adjuested = frame_location.adjust_ratio({ static_cast<float>(vid_frame.get_width())
                                                             , static_cast<float>(vid_frame.get_height()) });
```
And finally, call `show` with the position of the stream, to render it to screen.**CHANGE "adjuested" be adjusted??**
```cpp
                view.second.tex.show(adjuested);
                stream_no++;
            }
        }
    }
}
```

