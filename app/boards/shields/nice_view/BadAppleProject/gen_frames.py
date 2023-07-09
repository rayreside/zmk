# Importing all necessary libraries
import cv2
import numpy as np
def blockshaped(arr, nrows, ncols):
    """
    Return an array of shape (n, nrows, ncols) where
    n * nrows * ncols = arr.size

    If arr is a 2D array, the returned array should look like n subblocks with
    each subblock preserving the "physical" layout of arr.
    """
    h, w = arr.shape
    assert h % nrows == 0, f"{h} rows is not evenly divisible by {nrows}"
    assert w % ncols == 0, f"{w} cols is not evenly divisible by {ncols}"
    return (arr.reshape(h//nrows, nrows, -1, ncols)
               .swapaxes(1,2)
               .reshape(-1, nrows, ncols))
def RLE_encoding_diff(img, p_img):
    encoded = []
    count = 0
    fimg = ((img.flatten()) ^ (p_img.flatten())) >> 7
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
def RLE_encoding(img):
    encoded = []
    count = 0
    fimg = img.flatten() >> 7
    prev = fimg[0]
    colors = [1, 0]
    for pixel in fimg:
        if prev!=pixel:
            encoded.append((colors[prev] << 7) | count)
            prev=pixel
            count=1
        else:
            if count<127:
                count+=1
            else:
                encoded.append((colors[prev] << 7) | count)
                prev=pixel
                count=1
    encoded.append((colors[prev] << 7) | count)
    encoded.append(0)
    return np.array(encoded, dtype=np.uint8)
def RLE_encoding_b32(img):
    encoded = []
    count = 0
    bimg = blockshaped(img, 32, 32)
    for fimg in bimg:
        encoded += RLE_encoding_run
    encoded.append(0)
    return np.array(encoded, dtype=np.uint8)
def RLE_encoding_run(img):
    run_len = 16
    int_size = 32
    encoded = []
    count = 0
    bimg = blockshaped(img, 4, 4)
    prev = bimg[0]
    for block in bimg:
        if any(prev.flatten()!=block.flatten()):
            encoded.append((np.packbits(prev.flatten())[0] << run_len) | count)
            prev=block
            count=1
        else:
            if count<2**(int_size-run_len):
                count+=1
            else:
                encoded.append((np.packbits(prev.flatten())[0] << run_len) | count)
                prev=block
                count=1
    encoded.append((np.packbits(prev.flatten())[0] << run_len) | count)
    encoded.append(0)
    return np.array(encoded, dtype=np.uint16)
# Read the video from specified path
cam = cv2.VideoCapture("./BadApple64x64_10fps.mp4")
# frame
currentframe = 0
max = 2500
frame_data = np.array([], dtype=np.uint8)
for i in range(max):
    # reading from frame
    ret,frame = cam.read()  
    if ret:
        if (currentframe % 50 == 0):
            print("Processing frame: " + str(currentframe))
        # if video is still left continue creating images
        (thresh, bw_frame) = cv2.threshold(frame[:,:,0], 25, 255, cv2.THRESH_OTSU)
        # writing the extracted images
        frame_encoded = RLE_encoding(bw_frame)
        currentframe += 1
        frame_data = np.append(frame_data, frame_encoded)
    else:
        break
print(len(frame_data))
with open('frames.npy', 'wb') as f:
    np.save(f, frame_data)
    np.save(f, len(frame_data))