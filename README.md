# icamerasrc

This repository supports MIPI cameras through the IPU6 on Intel Tigerlake platforms. There are 4 repositories that provide the complete setup:

* https://github.com/intel/ipu6-drivers - kernel drivers for the IPU and sensors
* https://github.com/intel/ipu6-camera-hal - HAL for processing of images in userspace
* https://github.com/intel/ipu6-camera-bins - IPU firmware and proprietary image processing libraries
* https://github.com/intel/icamerasrc (branch:icamerasrc_slim_api) - Gstreamer src plugin


## Content of this repository:
* gstreamer src plugin 'icamerasrc'

## Build instructions:
* Prerequisites: ipu6-camera-bins and ipu6-camera-hal installed 

```
 export CHROME_SLIM_CAMHAL=ON
 ./autogen.sh 
 make
 sudo make install
```
 
## Pipeline examples
* Testpattern generator (no sensor)
```
sudo -E gst-launch-1.0 icamerasrc device-name=tpg_ipu6 ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! xvimagesink
```

* Sensor ov01a1s
```
sudo -E gst-launch-1.0 icamerasrc device-name=ov01a1s ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! xvimagesink
```

* Sensor hm11b1
```
sudo -E gst-launch-1.0 icamerasrc device-name=hm11b1 ! video/x-raw,format=NV12,width=1280,height=720 ! videoconvert ! xvimagesink
```
