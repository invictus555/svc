# svc

## environment
xcode + macos + ffmpeg + openh264

## procedure
1. read local media file using ffmpeg 
2. decode H264 packet using ffmpeg 
3. encode I420 to svc(temporal or spatial coding) using openh264
4. decode svc(temporal or spatial) compressed data using openh264

## note
openh264 with temporal and spatial together will produce a much more complex output, so this demo not present it