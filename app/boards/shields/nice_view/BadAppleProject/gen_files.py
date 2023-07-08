import cv2
import numpy as np

with open('frames.npy', 'rb') as f:
    frame_data = np.load(f)
    frame_len = np.load(f)
f = open("frames.c", "w")
f.write("#include <stdint.h>\n")
f.write("const uint8_t frames_enc[" + str(frame_len) + "] = {\n")
arr_str = np.array2string(frame_data, separator=',', max_line_width=80, threshold=np.inf)
f.write(arr_str[1:-1])
f.write("};\n")
f.close()
