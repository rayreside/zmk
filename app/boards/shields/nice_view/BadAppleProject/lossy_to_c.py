import numpy as np
with open('approx.npy', 'rb') as f:
    block_dictionary = np.load(f)
    block_dictionary_packed = np.load(f)
    frame_enc = np.load(f)
    frame_indexes = np.load(f)

print(block_dictionary.shape)
print(frame_indexes[:64])
# for i in range(0, len(frame_indexes), 64):
#     frame = block_dictionary[frame_indexes[i:i+64]]
#     new_frame = np.empty((64, 64), dtype=np.uint8)
#     chunk_idx = 0
#     for c_row in range(8):
#         for c_col in range(8):
#             new_frame[c_row * 8:(c_row+1)*8, c_col * 8:(c_col + 1) * 8] = np.reshape(frame[chunk_idx], (8, 8))
#             chunk_idx += 1
#     print(np.array2string(new_frame, threshold=np.inf, max_line_width=170, separator=""))
# print(frame_indexes.shape)
f = open("frames.c", "w")
f.write("#include <stdint.h>\n")
f.write("const uint16_t frames_enc[" + str(len(frame_enc)) + "] = {\n")
arr_str = np.array2string(np.array(frame_enc, dtype=np.uint16), separator=',', max_line_width=80, threshold=np.inf)
f.write(arr_str[1:-1])
f.write("};\n")
f.write("const uint8_t frame_dict[" + str(len(block_dictionary_packed)) + "][8] = {\n")
arr_str = np.array2string(block_dictionary_packed, separator=',', max_line_width=80, threshold=np.inf).replace("[", "{").replace("]", "}")
f.write(arr_str[1:-1])
f.write("};\n")
f.close()
