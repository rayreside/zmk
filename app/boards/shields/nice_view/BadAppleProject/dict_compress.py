import numpy as np
import cv2
import random
import kmodes.kmodes as km
import math
import warnings
warnings.filterwarnings("ignore")

depth=8

all_blocks=[]

def find_nearest(target,dictionary):
    return np.argmin(np.count_nonzero(np.bitwise_xor(dictionary, target), axis=1))
    
def quant_image(image,dictionary):
    encoded = []
    for block in image:
        encoded += [find_nearest(block, dictionary)]
    return encoded
                


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

global output_file
def RLE_encoding(arr, bits):
    encoded = []
    count = 0
    prev = arr[0]
    for val in arr:
        if prev!=val:
            encoded.append((prev << 5) | count)
            prev=val
            count=1
        else:
            if count<2**5-1:
                count+=1
            else:
                encoded.append((prev << 5) | count)
                prev=val
                count=1
    encoded.append((prev << 5) | count)
    encoded.append(0)
    return np.array(encoded, dtype=np.uint16)
def main():
    random.seed(1)
    # Read the video from specified path
    cam = cv2.VideoCapture("./BadApple128x64.gif")
    max = 5000 # Which frame to stop at
    # Clustering Algorithm configs
    clusters = 2048 # Number of 8x8 blocks to keep in dictionary
    threads = 8     # Number of CPU cores to dedicate to clustering
    max_iters = 16  # Number of iterations of clustering to be run
    
    all_blocks = np.zeros((1, 64), dtype=np.uint8)
    bits = math.log2(clusters)
    frames = []
    currentframe = 0
    for _ in range(max):
        # reading from frame
        ret,frame = cam.read()
        ret,frame = cam.read()   
        if ret:
            if (currentframe % 25 == 0):
                print("Processing frame: " + str(currentframe) + ", Uniques: " + str(len(all_blocks)))
            # if video is still left continue creating images
            (_, bw_frame) = cv2.threshold(frame[:, :, 0], 10, 255, cv2.THRESH_OTSU)
            # writing the extracted images
            patterns = blockshaped(bw_frame >> 7, 8, 8)
            packed = np.array([np.ravel(pattern) for pattern in patterns], dtype=np.uint8)
            frames.append(packed)
            all_blocks = np.append(all_blocks, packed, axis=0)
            currentframe += 1
        else:
            break
    print("Finding uniques")
    all_blocks = np.unique(all_blocks, axis=0)
    print("Optimizing dictionary")
    kmode = km.KModes(n_clusters=clusters, init="random", n_init=threads, max_iter=max_iters, n_jobs=threads, verbose=2)
    kmode.fit_predict(all_blocks)
    block_dictionary = kmode.cluster_centroids_
    print("compressing frames")
    currentframe = 0
    encoded = np.array([])
    frame_indexes = np.array([], dtype=np.int16)
    for frame in frames:
        if (currentframe % 25 == 0):
            print("Processing frame: " + str(currentframe))
        compressed = quant_image(frame, block_dictionary)
        frame_indexes = np.append(frame_indexes, compressed, axis=0)
        encoded = np.append(encoded, RLE_encoding(compressed, bits))
        currentframe += 1

    block_dictionary_packed = np.packbits(block_dictionary, axis=1)
    with open('approx.npy', 'wb') as f:
        np.save(f, block_dictionary)
        np.save(f, block_dictionary_packed)
        np.save(f, encoded)
        np.save(f, frame_indexes)
    f.close()
main()
    