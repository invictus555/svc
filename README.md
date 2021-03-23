# svc

## environment
xcode + macos + ffmpeg + openh264

## procedure
1. read local media file using ffmpeg 
2. decode H264 packet using ffmpeg 
3. encode I420 to svc(temporal and spatial coding) using openh264
4. decode svc(temporal and spatial) compressed data using openh264
