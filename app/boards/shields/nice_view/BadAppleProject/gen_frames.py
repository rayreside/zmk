# Importing all necessary libraries
import cv2
import numpy as np

def RLE_encoding(img):
    encoded = []
    count = 0
    fimg = img.flatten() >> 7
    prev = fimg[0]
    for pixel in fimg:
        if prev!=pixel:
            encoded.append((prev << 7) | count)
            prev=pixel
            count=1
        else:
            if count<127:
                count+=1
            else:
                encoded.append((prev << 7) | count)
                prev=pixel
                count=1
    encoded.append((prev << 7) | count)
    encoded.append(0)
    return np.array(encoded, dtype=np.uint8)

# Read the video from specified path
cam = cv2.VideoCapture("./BadApple64x64.mp4")
# frame
currentframe = 0
max = 2000
frame_data = np.array([], dtype=np.uint8)
for i in range(max):
    # reading from frame
    ret,frame = cam.read()  
    if ret:
        if (currentframe % 50 == 0):
            print("Processing frame: " + str(currentframe))
        # if video is still left continue creating images
        (thresh, bw_frame) = cv2.threshold(frame, 25, 255, cv2.THRESH_BINARY)
        # writing the extracted images
        frame_encoded = RLE_encoding(bw_frame[:,:,0])
        currentframe += 1
        frame_data = np.append(frame_data, frame_encoded)
    else:
        break
print(len(frame_data))
with open('frames.npy', 'wb') as f:
    np.save(f, frame_data)
    np.save(f, len(frame_data))