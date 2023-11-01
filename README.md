# icamerasrc

This repository supports MIPI cameras through the IPU6/IPU6EP/IPU6SE on Intel Tigerlake/Alderlake/Jasperlake platforms. There are 4 repositories that provide the complete setup:

- https://github.com/intel/ipu6-drivers - kernel drivers for the IPU and sensors
- https://github.com/intel/ipu6-camera-bins - IPU firmware and proprietary image processing libraries
- https://github.com/intel/ipu6-camera-hal - HAL for processing of images in userspace
- https://github.com/intel/icamerasrc/tree/icamerasrc_slim_api (branch:icamerasrc_slim_api) - Gstreamer src plugin

## Content of this repository:
* gstreamer src plugin 'icamerasrc'

## Build instructions:
* Prerequisites: ipu6-camera-bins and ipu6-camera-hal installed 
* Prerequisites: libdrm-dev

```sh
export CHROME_SLIM_CAMHAL=ON
# for libdrm.pc
export PKG_CONFIG_PATH="/usr/lib/x86_64-linux-gnu/pkgconfig"
# only for yocto
export PKG_CONFIG_PATH="/usr/lib/pkgconfig"
./autogen.sh
./configure --with-haladaptor=yes
make -j8
# binary install
sudo make install
# build rpm package and then install
make rpm
rpm -ivh --force --nodeps icamerasrc-*.rpm
```

## User permission via udev rule
To use the gstreamer plugin as a user, you need RW access to `/dev/ipu-psys0`. This can be done by
by changing its permissions via udev rule. You will also need to add your user to the `video` group.

```sh
sudo cp 70-ipu6-psys.rules /usr/lib/udev/rules.d/
sudo udevadm control --reload
sudo usermod -g -G video $USER
```
**NOTE:** If the user is currently logged in, they will need to log out and log in again for the
change to take effect.

## Pipeline examples
* Ensure `${GST_PLUGIN_PATH}` includes icamerasrc installation path
* Testpattern generator (no sensor)
```
gst-launch-1.0 icamerasrc device-name=tpg_ipu6 ! video/x-raw,format=YUY2,width=1280,height=720 ! videoconvert ! ximagesink
```

* For laptops with 11th Gen Intel Core Procesors (with ov01a1s/hm11b1 sensor):
```
gst-launch-1.0 icamerasrc buffer-count=7 ! video/x-raw,format=YUY2,width=1280,height=720 ! videoconvert ! ximagesink
```

* For laptops with 12th/13th Gen Intel Core Procesors (with ov01a10/ov02c10/ov2740/hm2170/hi556 sensor):
```
gst-launch-1.0 icamerasrc buffer-count=7 ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! ximagesink
```

* Sensor ov8856
```
gst-launch-1.0 icamerasrc device-name=ov8856-wf af-mode=2 ! video/x-raw,format=YUY2,width=1280,height=720 ! videoconvert ! ximagesink
```

* Sensor ov13b10
```
gst-launch-1.0 icamerasrc device-name=ov13b10-wf af-mode=2 ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! ximagesink
```

* Sensor ov13858
```
gst-launch-1.0 icamerasrc device-name=ov13858-uf af-mode=2 ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! ximagesink
```

* Sensor ar0234
```
gst-launch-1.0 icamerasrc device-name=ar0234 ! video/x-raw,format=NV12,width=1280,height=960 ! videoconvert ! glimagesink
```
