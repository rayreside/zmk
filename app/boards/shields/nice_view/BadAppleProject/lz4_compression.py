import os
import lz4.frame
import cv2
import numpy as np

# Read the video from specified path
cam = cv2.VideoCapture("./BadApple64x64.mp4")
# frame
currentframe = 0
max = 5
frame_data = []
for i in range(max):
    # reading from frame
    ret,frame = cam.read()  
    if ret:
        if (currentframe % 50 == 0):
            print("Processing frame: " + str(currentframe))
        # if video is still left continue creating images
        (thresh, bw_frame) = cv2.threshold(frame, 25, 255, cv2.THRESH_BINARY)
        frame = bw_frame[:,:,0].flatten() >> 7
        frame = np.packbits(frame, axis=0)
        currentframe += 1
        frame_data += frame.tolist()
    else:
        break
input_data = bytes(frame_data)
compressed = lz4.frame.compress(input_data)
print(compressed)
