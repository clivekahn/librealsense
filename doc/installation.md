# Linux Installation

Intel® RealSense™ Linux Installation comprises the following phases:
1. Verification of 3rd Party Dependencies  
2. Updating Ubuntu, Linux OS
3. Installing Intel® RealSense™ Packages  
4. Preparing Video4Linux Backend
5. Troubleshooting Installation Errors

**Note:** Due to the USB 3.0 translation layer between native hardware and virtual machine, the *librealsense* team does not recommend or support installation in a VM.

## 3rd-Party Dependencies

The RealSense™ installation requires two external dependencies, *glfw* and *libusb-1.0*.  
In addition, the Cmake build environment requires *pkg-config*.
* **Note**  glfw3 is only required if you plan to build the example code, not for *librealsense* core library.

**Important** Several scripts below invoke `wget, git, add-apt-repository` which may be blocked by router settings or a firewall. Infrequently, apt-get mirrors or repositories may also timeout.  
For *librealsense* users behind an enterprise firewall, configuring the system-wide Ubuntu proxy generally resolves most timeout issues.

**Note:** Linux build configuration is currently configured to use the **V4L2 Backend** by default. See [Video4Linux(V4L2) Backend Preparation](#video4linux-v4l2-backend-preparation) below.

## Update Ubuntu to the latest stable version
Update Ubuntu distribution including getting the latest stable kernel: 
1. `sudo apt-get update && sudo apt-get upgrade && sudo apt-get dist-upgrade`<br />

2. Check the kernel version - > `uname -r`<br />
**Note** For stack Ubuntu 14 LTS with Kernel prior to 4.4.0-04 (e.g. 3.19..) the basic *apt-get upgrade* rule is not sufficient to bring the distribution to the latest baseline recommended.<br />
In this instance, perform the following command to promote both Kernel and FrontEnd <br />
`sudo apt-get install --install-recommends linux-generic-lts-xenial xserver-xorg-core-lts-xenial xserver-xorg-lts-xenial xserver-xorg-video-all-lts-xenial xserver-xorg-input-all-lts-xenial libwayland-egl1-mesa-lts-xenial `<br />

3. Note the exact Kernel version being installed (4.4.0-XX) for the next step.<br />

4. Update the OS Boot Menu and reboot to enforce the correct kernel selection: <br />
   `sudo update-grub && sudo reboot`<br />

5. Interrupt the boot process at Grub2 Boot Menu -> "Advanced Options for Ubuntu" -> Select the kernel version installed in the previous step.<br />
   
6. Complete the boot, login, and verify that the required kernel version in place with:
   `uname -r`  >=  4.4.0-50

## Install *librealsense* <br />
1. Install the packages required for the librealsense build: 
    1.1 *libusb-1.0* and *pkg-config*:<br />
         `sudo apt-get install libusb-1.0-0-dev pkg-config`.

    1.2 *glfw3*:<br />
    * For Ubuntu 14.04 use:
      `./scripts/install_glfw3.sh`<br />

    * For Ubuntu 16.04 install glfw3 via
      `sudo apt-get install libglfw3-dev`

2. Library Build Process<br />
  *librealsense* employs CMake as a cross-platform build and project management system.
  
    2.1 Navigate to *librealsense* root directory and run<br />
        `mkdir build && cd build`<br />
  
    2.2 The default build is set to produce the core shared object and unit-tests binaries
        `cmake ../`<br />
        
    2.3 To build *librealsense* along with the demos and tutorials use:<br />
        `cmake ../ -DBUILD_EXAMPLES=true`<br />
  
  **Note** If you don't want to have build dependencies to OpenGL and X11, you can also<br />
  build only the non-graphical examples:<br />
  `cmake ../ -DBUILD_EXAMPLES=true -DBUILD_GRAPHICAL_EXAMPLES=false`

    2.4 Generate and install binaries:<br />
        `make && sudo make install`<br />
  
  * The library will be installed in `/usr/local/lib`, 
  * header files will be installed in `/usr/local/include`<br />
  * The demos, tutorials and tests will located in `/usr/local/bin`.<br />
  **Note:** Linux build configuration is presently configured to use the V4L2 backend by default

3. Install IDE (Optional):
    We use *QtCreator* as an IDE for Linux development on Ubuntu
    * Follow the  [link](https://wiki.qt.io/Install_Qt_5_on_Ubuntu) for QtCreator5 installation

## Video4Linux backend preparation
Running RealSense Depth Cameras on Linux requires applying patches to kernel modules.<br />
**Note:** Ensure no Intel RealSense cameras are presently plugged into the system.<br />
1. Install udev rules located in librealsense source directory:<br />
    1.1 `sudo cp config/99-realsense-libusb.rules /etc/udev/rules.d/`
    1.2 `sudo udevadm control --reload-rules && udevadm trigger`

2. Install *openssl* package required for kernel module build:<br />
   `sudo apt-get install libssl-dev`<br />

3. Build the patched module for the desired machine configuration.<br />
  * **Ubuntu 14/16 LTS**
    The script will download. Patch and build the uvc kernel module from sources.<br />
    Then it will attempt to insert the patched module instead of the active one.  
    If this fails, the original uvc module will be preserved.
    * `./scripts/patch-realsense-ubuntu-xenial.sh`<br />

  * **Arch-based distributions**
    * You need to install the [base-devel](https://www.archlinux.org/groups/x86_64/base-devel/) package group.
    * You need to install the according linux-headers as well (i.e.: linux-lts-headers for the linux-lts kernel).<br />
    Navigate to the scripts folder:
    `cd ./scripts/`<br />
    Then run the following script to patch the uvc module:
    `./patch-arch.sh`<br />

4. Check installation by examining the latest entries in kernel log:
  * `sudo dmesg | tail -n 50`<br />
The log should indicate that a new uvcvideo driver has been registered. If any errors are noted, first attempt the patching process again. If patching is not successful on the second attempt, file an issue (and make sure to copy the specific error in dmesg).

## Troubleshooting Installation and Patch-related Issues

Error    |      Cause   | Correction Steps |
-------- | ------------ | ---------------- |
`git.launchpad... access timeout` | Behind Firewall | Configure Proxy Server |
`dmesg:... uvcvideo: module verification failed: signature and/or required key missing - tainting kernel` | A standard warning issued since Kernel 4.4-30+ | Notification only - does not affect module's functionality |
`sudo modprobe uvcvideo` produces `dmesg: uvc kernel module is not loaded` | The patched module kernel version is incompatible with the resident kernel | Verify the actual kernel version with `uname -r`.<br />Revert and proceed on **Make Ubuntu Up-to-date** step |
Execution of `./scripts/patch-video-formats-ubuntu-xenial.sh`  fails with `fatal error: openssl/opensslv.h` | Missing Dependency | Install *openssl* package from **Video4Linux backend preparation** step |
