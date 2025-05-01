#include "exfs2.h"   // Include the header file defining constants, structs, and prototypes
#include <search.h> // Required for qsort (used in list_recursive for sorting directory entries)

// --- Global Variables Definition ---
// These track the highest segment index encountered or created.
// Used to know where to potentially create the next segment.
int next_inode_segment_idx = 0; // Index for the next inode segment file (e.g., inode_segment_0.exfs, inode_segment_1.exfs, ...)
int next_data_segment_idx = 0;  // Index for the next data segment file (e.g., data_segment_0.exfs, data_segment_1.exfs, ...)

// --- Helper Functions ---

/**
 * @brief Generates the filename for a segment based on prefix and index.
 * @param prefix The segment type prefix (e.g., "inode_segment_").
 * @param index The segment index number.
 * @param buffer Output buffer to store the generated filename.
 * @param buffer_size Size of the output buffer.
 */
void get_segment_filename(const char* prefix, int index, char* buffer, size_t buffer_size) {
    // Use snprintf for safe string formatting into the buffer.
    snprintf(buffer, buffer_size, "%s%d%s", prefix, index, SEGMENT_SUFFIX);
}

/**
 * @brief Opens a segment file. If opening in read/write mode ("rb+") fails
 * and the file doesn't exist, it attempts to create it ("wb+").
 * Newly created files are truncated to SEGMENT_SIZE.
 * @param prefix The segment type prefix.
 * @param index The segment index number.
 * @param mode The file opening mode (e.g., "rb", "rb+", "wb+").
 * @return FILE pointer to the opened segment, or NULL on error.
 */
FILE* open_segment(const char* prefix, int index, const char* mode) {
    char filename[100]; // Buffer for the filename
    get_segment_filename(prefix, index, filename, sizeof(filename)); // Generate the filename

    // Attempt to open the file with the specified mode.
    FILE *fp = fopen(filename, mode);

    // If opening with "rb+" failed (potentially because the file doesn't exist)...
    if (!fp && (mode[0] == 'r' && mode[1] == 'b' && mode[2] == '+')) {
         // Try creating the file with "wb+" mode (write/read, binary, create/truncate).
         fp = fopen(filename, "wb+");
         if (fp) {
            // If creation succeeded, set its size to the standard SEGMENT_SIZE.
            if (ftruncate(fileno(fp), SEGMENT_SIZE) != 0) {
                 perror("Failed to set segment size"); // Report error if ftruncate fails
                 fclose(fp); // Close the file
                 return NULL; // Return error
            }
            rewind(fp); // Go back to the beginning of the newly created file
         }
    }
    // If opening/creating ultimately failed, print an error.
     if (!fp) {
         fprintf(stderr, "Error opening/creating segment %s: %s\n", filename, strerror(errno));
     }
    // Return the file pointer (or NULL if failed).
    return fp;
}

/**
 * @brief Creates and initializes a new inode segment file.
 * @param index The index number for the new inode segment.
 * @return 0 on success, -1 on error.
 */
int create_inode_segment(int index) {
    // Open (create or overwrite) the inode segment file in "wb+" mode.
    FILE *fp = open_segment(INODE_SEGMENT_PREFIX, index, "wb+");
    if (!fp) {
        // Error message printed by open_segment if it fails.
        return -1; // Return error
    }

    // Create an empty inode bitmap (all bytes initialized to 0).
    uint8_t bitmap[INODE_BITMAP_SIZE] = {0};
    // Write the empty bitmap to the beginning of the segment file.
    if (fwrite(bitmap, 1, INODE_BITMAP_SIZE, fp) != INODE_BITMAP_SIZE) {
        perror("Failed to write inode bitmap"); // Report error if write fails
        fclose(fp); // Close the file
        return -1; // Return error
    }

    // The rest of the segment file (inode area) is implicitly zeroed
    // because ftruncate (called within open_segment for new files) extends with zeros.

    printf("Created inode segment %d\n", index); // Informative message
    fclose(fp); // Close the file

    // Update the global index if we created a segment with a higher index.
    if (index >= next_inode_segment_idx) {
        next_inode_segment_idx = index + 1;
    }
    return 0; // Success
}

/**
 * @brief Creates and initializes a new data segment file.
 * @param index The index number for the new data segment.
 * @return 0 on success, -1 on error.
 */
int create_data_segment(int index) {
    // Open (create or overwrite) the data segment file in "wb+" mode.
    FILE *fp = open_segment(DATA_SEGMENT_PREFIX, index, "wb+");
    if (!fp) {
        // Error message printed by open_segment if it fails.
        return -1; // Return error
    }

    // Create an empty data block bitmap (all bytes initialized to 0).
    uint8_t bitmap[DATA_BITMAP_SIZE] = {0};
    // Write the empty bitmap to the beginning of the segment file.
    if (fwrite(bitmap, 1, DATA_BITMAP_SIZE, fp) != DATA_BITMAP_SIZE) {
        perror("Failed to write data bitmap"); // Report error if write fails
        fclose(fp); // Close the file
        return -1; // Return error
    }

    // The rest of the segment file (data block area) is implicitly zeroed by ftruncate.

    printf("Created data segment %d\n", index); // Informative message
    fclose(fp); // Close the file

    // Update the global index if we created a segment with a higher index.
     if (index >= next_data_segment_idx) {
        next_data_segment_idx = index + 1;
    }
    return 0; // Success
}

// --- Bitmap Operations ---

/**
 * @brief Gets the value of a bit within a bitmap array.
 * @param bitmap Pointer to the start of the bitmap data.
 * @param index The 0-based index of the bit to check.
 * @return 1 if the bit is set (used), 0 if the bit is clear (free).
 */
int get_bit(const uint8_t *bitmap, uint32_t index) {
    // Calculate the byte containing the bit (index / 8).
    // Calculate the bit position within that byte (index % 8).
    // Right-shift the byte to bring the target bit to the least significant position.
    // Use bitwise AND with 1 to isolate the target bit's value.
    return (bitmap[index / 8] >> (index % 8)) & 1;
}

/**
 * @brief Sets a bit within a bitmap array to 1 (marks as used).
 * @param bitmap Pointer to the start of the bitmap data (will be modified).
 * @param index The 0-based index of the bit to set.
 */
void set_bit(uint8_t *bitmap, uint32_t index) {
    // Calculate the byte containing the bit (index / 8).
    // Calculate the bit position within that byte (index % 8).
    // Create a mask with only the target bit set (1 << (index % 8)).
    // Use bitwise OR to set the target bit in the byte without affecting other bits.
    bitmap[index / 8] |= (1 << (index % 8));
}

/**
 * @brief Clears a bit within a bitmap array to 0 (marks as free).
 * @param bitmap Pointer to the start of the bitmap data (will be modified).
 * @param index The 0-based index of the bit to clear.
 */
void clear_bit(uint8_t *bitmap, uint32_t index) {
    // Calculate the byte containing the bit (index / 8).
    // Calculate the bit position within that byte (index % 8).
    // Create a mask with only the target bit set (1 << (index % 8)).
    // Invert the mask (~ operator) so only the target bit is 0 and all others are 1.
    // Use bitwise AND to clear the target bit in the byte without affecting other bits.
    bitmap[index / 8] &= ~(1 << (index % 8));
}


// --- Inode Operations ---

/**
 * @brief Finds the first free inode, allocating a new segment if necessary.
 * @return The allocated inode number, or UINT32_MAX on error.
 */
uint32_t find_free_inode() {
    // Iterate through existing inode segments.
    for (int seg_idx = 0; seg_idx < next_inode_segment_idx; ++seg_idx) {
        // Open the segment for reading and writing the bitmap.
        FILE *fp = open_segment(INODE_SEGMENT_PREFIX, seg_idx, "rb+");
        if (!fp) continue; // If segment can't be opened, try the next one.

        // Read the inode bitmap from the start of the segment.
        uint8_t bitmap[INODE_BITMAP_SIZE];
        if (fread(bitmap, 1, INODE_BITMAP_SIZE, fp) != INODE_BITMAP_SIZE) {
            perror("Failed to read inode bitmap");
            fclose(fp);
            continue; // Try next segment on read error.
        }

        // Iterate through possible inode indices within this segment's bitmap.
        for (uint32_t local_idx = 0; local_idx < MAX_INODES_PER_SEGMENT; ++local_idx) {
            // Check if the bit corresponding to this local index is free (0).
            if (!get_bit(bitmap, local_idx)) {
                // Found a free inode. Mark it as used (set the bit to 1).
                set_bit(bitmap, local_idx);
                // Seek back to the beginning of the file to rewrite the bitmap.
                rewind(fp);
                // Write the modified bitmap back to the segment file.
                if (fwrite(bitmap, 1, INODE_BITMAP_SIZE, fp) != INODE_BITMAP_SIZE) {
                    perror("Failed to write updated inode bitmap");
                    // Attempt to rollback the change in memory (won't fix disk state).
                    clear_bit(bitmap, local_idx);
                    fclose(fp);
                    return UINT32_MAX; // Indicate error.
                }
                fclose(fp); // Close the segment file.
                // Calculate and return the global inode number.
                return (uint32_t)seg_idx * MAX_INODES_PER_SEGMENT + local_idx;
            }
        }
        fclose(fp); // Close the segment if no free inode was found in it.
    }

    // If no free inode was found in any existing segments, create a new one.
    int new_seg_idx = next_inode_segment_idx; // Get the index for the new segment.
    if (create_inode_segment(new_seg_idx) == 0) { // Attempt to create the segment.
        // Creation successful. The first inode (local index 0) in the new segment is free.
        // Open the newly created segment to mark the first inode as used.
         FILE *fp = open_segment(INODE_SEGMENT_PREFIX, new_seg_idx , "rb+");
         if (!fp) return UINT32_MAX; // Error opening the new segment.

         // Read the bitmap (should be all zeros initially).
         uint8_t bitmap[INODE_BITMAP_SIZE];
         fread(bitmap, 1, INODE_BITMAP_SIZE, fp);
         // Mark the first inode (local index 0) as used.
         set_bit(bitmap, 0);
         // Seek to the beginning and write the updated bitmap.
         rewind(fp);
         fwrite(bitmap, 1, INODE_BITMAP_SIZE, fp);
         fclose(fp); // Close the segment.

         // Calculate and return the global inode number of the newly allocated inode.
         return (uint32_t)new_seg_idx * MAX_INODES_PER_SEGMENT + 0;

    } else {
        // Failed to create a new inode segment.
        fprintf(stderr, "Failed to create new inode segment.\n");
        return UINT32_MAX; // Indicate error.
    }
}

/**
 * @brief Reads the Inode struct for a given inode number.
 * @param inode_num The global inode number to read.
 * @param inode_buffer Pointer to an Inode struct to store the result.
 * @return 0 on success, -1 on error. Returns 0 with zeroed buffer on EOF.
 */
int read_inode(uint32_t inode_num, Inode *inode_buffer) {
    // Calculate the segment index containing the inode.
    uint32_t seg_idx = inode_num / MAX_INODES_PER_SEGMENT;
    // Calculate the local index of the inode within the segment.
    uint32_t local_idx = inode_num % MAX_INODES_PER_SEGMENT;
    // Calculate the byte offset of the inode within the segment file.
    // Offset = size of bitmap + (local index * size of each inode).
    long offset = (long)INODE_BITMAP_SIZE + (long)local_idx * INODE_SIZE;

    // Open the correct inode segment file for reading ("rb").
    FILE *fp = open_segment(INODE_SEGMENT_PREFIX, seg_idx, "rb");
    if (!fp) {
        // Error if segment cannot be opened.
        fprintf(stderr, "read_inode: Could not open inode segment %u for inode %u\n", seg_idx, inode_num);
        return -1;
    }

    // Seek to the calculated offset within the segment file.
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode");
        fclose(fp);
        return -1;
    }

    // Read the Inode structure from the file into the provided buffer.
    if (fread(inode_buffer, sizeof(Inode), 1, fp) != 1) {
        // Check if the read failed due to reaching the end-of-file.
        if(feof(fp)) {
             // This can happen if reading an uninitialized part of the segment.
             // Treat it as an empty/invalid inode by zeroing the buffer.
             memset(inode_buffer, 0, sizeof(Inode));
        } else {
            // Read failed for a reason other than EOF.
            perror("Failed to read inode");
            fclose(fp);
            return -1;
        }
    }

    fclose(fp); // Close the segment file.
    return 0; // Success
}

/**
 * @brief Writes an Inode struct to the specified inode number location.
 * @param inode_num The global inode number to write to.
 * @param inode_buffer Pointer to the Inode struct containing data to write.
 * @return 0 on success, -1 on error.
 */
int write_inode(uint32_t inode_num, const Inode *inode_buffer) {
    // Calculate segment index, local index, and offset (same as read_inode).
    uint32_t seg_idx = inode_num / MAX_INODES_PER_SEGMENT;
    uint32_t local_idx = inode_num % MAX_INODES_PER_SEGMENT;
    long offset = (long)INODE_BITMAP_SIZE + (long)local_idx * INODE_SIZE;

    // Open the correct inode segment file for reading and writing ("rb+").
    FILE *fp = open_segment(INODE_SEGMENT_PREFIX, seg_idx, "rb+");
    if (!fp) {
         fprintf(stderr, "write_inode: Could not open inode segment %u for writing inode %u\n", seg_idx, inode_num);
        return -1;
    }

    // Seek to the calculated offset.
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("Failed to seek to inode location for writing");
        fclose(fp);
        return -1;
    }

    // Write the contents of the provided Inode buffer to the file.
    if (fwrite(inode_buffer, sizeof(Inode), 1, fp) != 1) {
        perror("Failed to write inode");
        fclose(fp);
        return -1;
    }

    fclose(fp); // Close the segment file.
    return 0; // Success
}

/**
 * @brief Marks an inode as free in its segment's bitmap.
 * IMPORTANT: Does NOT free the data blocks associated with the inode.
 * @param inode_num The global inode number to free.
 */
void free_inode(uint32_t inode_num) {
    // Prevent freeing the root directory inode (inode 0).
    if (inode_num == 0) {
        fprintf(stderr, "Warning: Attempted to free root inode (0). Operation aborted.\n");
        return;
    }

    // Calculate segment index and local index.
    uint32_t seg_idx = inode_num / MAX_INODES_PER_SEGMENT;
    uint32_t local_idx = inode_num % MAX_INODES_PER_SEGMENT;

    // Open the inode segment for reading and writing the bitmap.
    FILE *fp = open_segment(INODE_SEGMENT_PREFIX, seg_idx, "rb+");
    if (!fp) {
        fprintf(stderr, "Error opening inode segment %d to free inode %u\n", seg_idx, inode_num);
        return;
    }

    // Read the inode bitmap.
    uint8_t bitmap[INODE_BITMAP_SIZE];
     if (fread(bitmap, 1, INODE_BITMAP_SIZE, fp) != INODE_BITMAP_SIZE) {
        perror("Failed to read inode bitmap for freeing");
        fclose(fp);
        return;
     }

     // Check if the inode bit is currently set (used).
     if (get_bit(bitmap, local_idx)) {
        // Clear the bit to mark the inode as free.
        clear_bit(bitmap, local_idx);
        // Seek to the beginning to rewrite the bitmap.
        rewind(fp);
        // Write the modified bitmap back.
        if (fwrite(bitmap, 1, INODE_BITMAP_SIZE, fp) != INODE_BITMAP_SIZE) {
            perror("Failed to write updated inode bitmap after freeing");
            // Error occurred during write, disk state might be inconsistent.
        }
     } else {
         // The inode was already marked as free. Print a warning.
         fprintf(stderr, "Warning: Attempted to free already free inode %u\n", inode_num);
     }

    fclose(fp); // Close the segment file.
    // Note: Caller is responsible for freeing data blocks *before* calling this.
}


// --- Data Block Operations ---

/**
 * @brief Finds the first free data block, allocating a new segment if necessary.
 * @return The allocated data block number, or UINT32_MAX on error.
 */
uint32_t find_free_data_block() {
    // Iterate through existing data segments.
    for (int seg_idx = 0; seg_idx < next_data_segment_idx; ++seg_idx) {
        // Open segment for reading/writing bitmap.
        FILE *fp = open_segment(DATA_SEGMENT_PREFIX, seg_idx, "rb+");
        if (!fp) continue; // Try next segment on open error.

        // Read the data block bitmap.
        uint8_t bitmap[DATA_BITMAP_SIZE];
        if (fread(bitmap, 1, DATA_BITMAP_SIZE, fp) != DATA_BITMAP_SIZE) {
             perror("Failed to read data bitmap");
             fclose(fp);
             continue; // Try next segment on read error.
        }

        // Iterate through possible block indices within this segment.
        for (uint32_t local_idx = 0; local_idx < MAX_BLOCKS_PER_DATA_SEGMENT; ++local_idx) {
            // Check if the bit corresponding to this block is free (0).
            if (!get_bit(bitmap, local_idx)) {
                // Found a free block. Mark it as used (set bit to 1).
                set_bit(bitmap, local_idx);
                // Seek to beginning and rewrite the bitmap.
                rewind(fp);
                if (fwrite(bitmap, 1, DATA_BITMAP_SIZE, fp) != DATA_BITMAP_SIZE) {
                    perror("Failed to write updated data bitmap");
                    clear_bit(bitmap, local_idx); // Attempt rollback in memory.
                    fclose(fp);
                    return UINT32_MAX; // Indicate error.
                }
                fclose(fp); // Close segment.
                // Calculate and return the global block number.
                return (uint32_t)seg_idx * MAX_BLOCKS_PER_DATA_SEGMENT + local_idx;
            }
        }
        fclose(fp); // Close segment if no free block found.
    }

    // No free block found in existing segments, create a new data segment.
    int new_seg_idx = next_data_segment_idx; // Get index for the new segment.
    if (create_data_segment(new_seg_idx) == 0) { // Attempt creation.
        // Creation successful. Allocate the first block (local index 0).
         FILE *fp = open_segment(DATA_SEGMENT_PREFIX, new_seg_idx, "rb+"); // Open the new segment.
         if (!fp) return UINT32_MAX; // Error opening.

         // Read the new bitmap (should be zeros).
         uint8_t bitmap[DATA_BITMAP_SIZE];
         fread(bitmap, 1, DATA_BITMAP_SIZE, fp);
         // Mark the first block as used.
         set_bit(bitmap, 0);
         // Seek and rewrite the bitmap.
         rewind(fp);
         fwrite(bitmap, 1, DATA_BITMAP_SIZE, fp);
         fclose(fp); // Close segment.

         // Calculate and return the global block number.
         return (uint32_t)new_seg_idx * MAX_BLOCKS_PER_DATA_SEGMENT + 0;

    } else {
        // Failed to create a new data segment.
        fprintf(stderr, "Failed to create new data segment.\n");
        return UINT32_MAX; // Indicate error.
    }
}

/**
 * @brief Reads the content of a specific data block.
 * @param block_num The global data block number to read.
 * @param buffer Buffer (size BLOCK_SIZE) to store the read data.
 * @return 0 on success, -1 on error. Returns 0 with zero-padding on EOF/short read.
 */
int read_data_block(uint32_t block_num, char *buffer) {
    // Calculate segment index and local index.
    uint32_t seg_idx = block_num / MAX_BLOCKS_PER_DATA_SEGMENT;
    uint32_t local_idx = block_num % MAX_BLOCKS_PER_DATA_SEGMENT;
    // Calculate the byte offset of the data block within the segment file.
    // Offset = size of bitmap + (local index * size of each block).
    long offset = (long)DATA_AREA_OFFSET + (long)local_idx * BLOCK_SIZE;

    // Open the correct data segment file for reading ("rb").
    FILE *fp = open_segment(DATA_SEGMENT_PREFIX, seg_idx, "rb");
    if (!fp) {
        // Error opening segment.
        return -1;
    }

    // Seek to the calculated offset.
     if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("Failed to seek to data block");
        fclose(fp);
        return -1;
    }

    // Read BLOCK_SIZE bytes from the file into the buffer.
    size_t bytes_read = fread(buffer, 1, BLOCK_SIZE, fp);
    // Check if the number of bytes read is less than expected.
    if (bytes_read < BLOCK_SIZE) {
         // Check if the short read was due to reaching the end-of-file.
         if(feof(fp)) {
            // This might happen for uninitialized blocks or reading past segment end.
            // Zero out the remainder of the buffer for consistency.
            memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);
         } else {
            // Read failed for another reason.
            perror("Failed to read data block");
            fclose(fp);
            return -1;
         }
    }

    fclose(fp); // Close the segment file.
    return 0; // Success
}

/**
 * @brief Writes data to a specific data block.
 * @param block_num The global data block number to write to.
 * @param buffer Buffer (size BLOCK_SIZE) containing the data to write.
 * @return 0 on success, -1 on error.
 */
int write_data_block(uint32_t block_num, const char *buffer) {
    // Calculate segment index, local index, and offset (same as read_data_block).
    uint32_t seg_idx = block_num / MAX_BLOCKS_PER_DATA_SEGMENT;
    uint32_t local_idx = block_num % MAX_BLOCKS_PER_DATA_SEGMENT;
    long offset = (long)DATA_AREA_OFFSET + (long)local_idx * BLOCK_SIZE;

    // Open the correct data segment file for reading and writing ("rb+").
    FILE *fp = open_segment(DATA_SEGMENT_PREFIX, seg_idx, "rb+");
    if (!fp) {
        fprintf(stderr, "write_data_block: Could not open data segment %u for writing block %u\n", seg_idx, block_num);
        return -1;
    }

    // Seek to the calculated offset.
    if (fseek(fp, offset, SEEK_SET) != 0) {
        perror("Failed to seek to data block location for writing");
        fclose(fp);
        return -1;
    }

    // Write BLOCK_SIZE bytes from the buffer to the file.
    if (fwrite(buffer, 1, BLOCK_SIZE, fp) != BLOCK_SIZE) {
        perror("Failed to write data block");
        fclose(fp);
        return -1;
    }

    fclose(fp); // Close the segment file.
    return 0; // Success
}

/**
 * @brief Marks a data block as free in its segment's bitmap.
 * @param block_num The global data block number to free.
 */
void free_data_block(uint32_t block_num) {
     // Check for the special invalid block number marker.
     if (block_num == UINT32_MAX) {
         fprintf(stderr, "Warning: Attempt to free invalid block number UINT32_MAX\n");
         return;
     }

    // Calculate segment index and local index.
    uint32_t seg_idx = block_num / MAX_BLOCKS_PER_DATA_SEGMENT;
    uint32_t local_idx = block_num % MAX_BLOCKS_PER_DATA_SEGMENT;

    // Open the data segment for reading/writing the bitmap.
    FILE *fp = open_segment(DATA_SEGMENT_PREFIX, seg_idx, "rb+");
    if (!fp) {
         fprintf(stderr, "Error opening data segment %d to free block %u\n", seg_idx, block_num);
         return;
     }

    // Read the data block bitmap.
    uint8_t bitmap[DATA_BITMAP_SIZE];
     if (fread(bitmap, 1, DATA_BITMAP_SIZE, fp) != DATA_BITMAP_SIZE) {
         perror("Failed to read data bitmap for freeing");
         fclose(fp);
         return;
     }

     // Check if the block bit is currently set (used).
     if (get_bit(bitmap, local_idx)) {
         // Clear the bit to mark the block as free.
         clear_bit(bitmap, local_idx);
         // Seek to the beginning and rewrite the bitmap.
         rewind(fp);
         if (fwrite(bitmap, 1, DATA_BITMAP_SIZE, fp) != DATA_BITMAP_SIZE) {
             perror("Failed to write updated data bitmap after freeing block");
             // Error during write, disk state might be inconsistent.
         }
     }
     // Optional: Add warning if attempting to free an already free block.

    fclose(fp); // Close the segment file.
}

/**
 * @brief Recursively frees blocks pointed to by an indirect block and the indirect block itself.
 * @param block_num The block number of the indirect block (single or double).
 * @param level The level of indirection: 0 for single, 1 for double.
 */
void free_indirect_blocks(uint32_t block_num, int level) {
    // Base cases: invalid block number, invalid level, or block already freed (block_num == 0).
    if (block_num == 0 || block_num == UINT32_MAX || level < 0 || level > 1) {
        return; // Nothing to do.
    }

    // Buffer to hold the pointers read from the indirect block.
    uint32_t pointers[POINTERS_PER_BLOCK];
    // Buffer to read the indirect block content.
    char block_buffer[BLOCK_SIZE];

    // Read the indirect block itself.
    if (read_data_block(block_num, block_buffer) != 0) {
        fprintf(stderr, "Error reading indirect block %u (level %d) for freeing\n", block_num, level);
        // If read fails, still attempt to free the indirect block number itself.
        free_data_block(block_num);
        return;
    }
    // Copy the raw block data into the array of pointers.
    memcpy(pointers, block_buffer, BLOCK_SIZE);

    // Iterate through all possible pointers within the indirect block.
    for (int i = 0; i < POINTERS_PER_BLOCK; ++i) {
        // Check if the pointer is valid (not 0 or UINT32_MAX).
        if (pointers[i] != 0 && pointers[i] != UINT32_MAX) {
            if (level == 0) {
                // Level 0 (single indirect): pointers[i] points directly to a data block. Free it.
                free_data_block(pointers[i]);
            } else { // level == 1 (double indirect)
                // Level 1 (double indirect): pointers[i] points to a single indirect block.
                // Recursively call free_indirect_blocks for that single indirect block (level 0).
                free_indirect_blocks(pointers[i], level - 1);
            }
        }
    }
    // After freeing all blocks pointed to by this indirect block, free the indirect block itself.
    free_data_block(block_num);
}


// --- Path and Directory Operations ---

/**
 * @brief Parses an absolute path, finding the target and its parent inode.
 * @param path The absolute path string (e.g., "/a/b/c").
 * @param parent_inode_num Output: Inode number of the parent directory.
 * @param target_inode_num Output: Inode number of the target component (or UINT32_MAX if not found).
 * @param target_name Output: Buffer to store the name of the target component.
 * @return 0 on success, -1 on error (invalid path, component not found or not a directory).
 */
int parse_path(const char *path, uint32_t *parent_inode_num, uint32_t *target_inode_num, char *target_name) {
    // Basic validation: path must exist and start with '/'.
    if (path == NULL || path[0] != '/') {
        fprintf(stderr, "Invalid path format. Must start with '/'.\n");
        return -1;
    }

    // Start traversal from the root directory (inode 0).
    uint32_t current_inode_num = 0;
    *parent_inode_num = 0; // Root's parent is initially considered root itself.
    *target_inode_num = UINT32_MAX; // Initialize target as not found.
    target_name[0] = '\0'; // Initialize target name buffer.

    // Create a mutable copy of the path for strtok_r.
    char path_copy[PATH_MAX];
    strncpy(path_copy, path, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0'; // Ensure null termination.

    char *token; // Current path component.
    char *rest = path_copy; // Pointer for strtok_r state.
    char *last_token = NULL; // Stores the previous token (potential parent).

    // Skip the leading '/' character.
    if (rest[0] == '/') rest++;

    // Handle the special case of the root path "/".
    if (strlen(rest) == 0) {
        *target_inode_num = 0; // Target is root inode.
        strncpy(target_name, "/", MAX_FILENAME_LEN); // Target name is "/".
        target_name[MAX_FILENAME_LEN] = '\0';
        *parent_inode_num = 0; // Root's parent is root.
        return 0; // Success.
    }

    // Use strtok_r to tokenize the path by '/' delimiter.
    while ((token = strtok_r(rest, "/", &rest))) {
        // If this is not the first token (i.e., we processed a component before this one)...
        if (last_token) {
             uint32_t found_inode;
             // Find the inode corresponding to the previous token (last_token)
             // within the directory represented by current_inode_num.
             if (find_entry_in_dir(current_inode_num, last_token, &found_inode) != 0) {
                 // The previous component was not found in the current directory. Path is invalid.
                 fprintf(stderr, "Path component '%s' not found in directory inode %u\n", last_token, current_inode_num);
                 return -1;
             }

             // Read the inode of the found component to check if it's a directory.
             Inode temp_inode;
             if (read_inode(found_inode, &temp_inode) != 0) {
                 fprintf(stderr, "Failed to read inode %u for path component '%s'\n", found_inode, last_token);
                 return -1;
             }
             // If an intermediate path component is not a directory, the path is invalid.
             if (!temp_inode.is_directory) {
                 fprintf(stderr, "Path component '%s' is not a directory.\n", last_token);
                 return -1;
             }

             // Update the parent inode number to the one we just traversed from.
             *parent_inode_num = current_inode_num;
             // Move into the directory we just found.
             current_inode_num = found_inode;
        }
        // Store the current token; it will be the 'last_token' in the next iteration,
        // or it's the final component if the loop terminates.
         last_token = token;
    }

    // After the loop, last_token holds the final component of the path.
    if (last_token) {
        // Copy the final component name to the output buffer.
        strncpy(target_name, last_token, MAX_FILENAME_LEN);
        target_name[MAX_FILENAME_LEN] = '\0'; // Ensure null termination.

        // Try to find the final component within the last directory traversed (current_inode_num).
        uint32_t final_inode;
        if (find_entry_in_dir(current_inode_num, target_name, &final_inode) == 0) {
            // Found the final component. Store its inode number.
            *target_inode_num = final_inode;
        } else {
            // Final component not found. Keep target_inode_num as UINT32_MAX.
            *target_inode_num = UINT32_MAX;
        }
        // The parent of the target is the directory we were in when looking for the final component.
        *parent_inode_num = current_inode_num;
        return 0; // Parsing successful. target_inode_num indicates existence.
    } else {
        // Should not happen if path starts with '/' and is not just "/", but handle defensively.
        fprintf(stderr, "Error parsing path - no components found after '/'.\n");
        return -1;
    }
}


/**
 * @brief Searches for a named entry within a directory's data blocks.
 * @param dir_inode_num Inode number of the directory to search.
 * @param name Name of the entry to find.
 * @param entry_inode_num Output: Inode number of the found entry.
 * @return 0 if found, -1 if not found or error.
 */
int find_entry_in_dir(uint32_t dir_inode_num, const char *name, uint32_t *entry_inode_num) {
    // Read the directory inode.
    Inode dir_inode;
    if (read_inode(dir_inode_num, &dir_inode) != 0 || !dir_inode.is_directory) {
        // Fail silently if not a directory, as this might be called speculatively.
        return -1;
    }

    // Buffer to hold one block of directory entries.
    char block_buffer[BLOCK_SIZE];
    // Cast buffer to DirectoryEntry pointer for easy access.
    DirectoryEntry *entries = (DirectoryEntry *)block_buffer;
    size_t entries_searched = 0; // Count valid entries encountered.
    // Get the total number of valid entries expected from the inode's size field.
    size_t total_entries_in_inode = dir_inode.size;

    // --- Search Direct Blocks ---
    for (int i = 0; i < MAX_DIRECT_POINTERS && entries_searched < total_entries_in_inode; ++i) {
        uint32_t block_num = dir_inode.direct_blocks[i];
        if (block_num == 0 || block_num == UINT32_MAX) continue; // Skip unused pointers.

        if (read_data_block(block_num, block_buffer) != 0) {
            fprintf(stderr, "Error reading directory data block %u while searching for '%s'\n", block_num, name);
            continue; // Skip this block on error.
        }

        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
             if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) { // Check if entry is valid
                 entries_searched++;
                 if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                     *entry_inode_num = entries[j].inode_num; // Found it!
                     return 0; // Return success.
                 }
                 if (entries_searched >= total_entries_in_inode) goto search_end; // Optimization
             }
        }
    }

    // --- Search Single Indirect Block ---
    if (dir_inode.single_indirect != 0 && dir_inode.single_indirect != UINT32_MAX && entries_searched < total_entries_in_inode) {
        uint32_t pointers[POINTERS_PER_BLOCK];
        char indirect_block_buffer[BLOCK_SIZE];
        if (read_data_block(dir_inode.single_indirect, indirect_block_buffer) == 0) {
            memcpy(pointers, indirect_block_buffer, BLOCK_SIZE);
            for (int i = 0; i < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++i) {
                uint32_t block_num = pointers[i];
                if (block_num == 0 || block_num == UINT32_MAX) continue; // Skip unused pointers

                if (read_data_block(block_num, block_buffer) != 0) {
                    fprintf(stderr, "Error reading directory data block %u (from single indirect) while searching for '%s'\n", block_num, name);
                    continue; // Skip block
                }
                for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                    if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                        entries_searched++;
                        if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                            *entry_inode_num = entries[j].inode_num; // Found it!
                            return 0; // Return success
                        }
                        if (entries_searched >= total_entries_in_inode) goto search_end; // Optimization
                    }
                }
            }
        } else {
            fprintf(stderr, "Error reading single indirect block %u while searching for '%s'\n", dir_inode.single_indirect, name);
        }
    }

    // --- Search Double Indirect Block ---
    if (dir_inode.double_indirect != 0 && dir_inode.double_indirect != UINT32_MAX && entries_searched < total_entries_in_inode) {
        uint32_t pointers1[POINTERS_PER_BLOCK]; // Pointers in double indirect block
        char block_buffer1[BLOCK_SIZE];       // Buffer for double indirect block
        if (read_data_block(dir_inode.double_indirect, block_buffer1) == 0) {
            memcpy(pointers1, block_buffer1, BLOCK_SIZE);
            // Iterate through pointers in double indirect block (point to single indirect blocks)
            for (int idx1 = 0; idx1 < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++idx1) {
                uint32_t single_indirect_block_num = pointers1[idx1];
                if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) continue;

                uint32_t pointers2[POINTERS_PER_BLOCK]; // Pointers in single indirect block
                char block_buffer2[BLOCK_SIZE];       // Buffer for single indirect block
                if (read_data_block(single_indirect_block_num, block_buffer2) == 0) {
                    memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                    // Iterate through pointers in single indirect block (point to data blocks)
                    for (int i = 0; i < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++i) {
                        uint32_t block_num = pointers2[i];
                        if (block_num == 0 || block_num == UINT32_MAX) continue;

                        if (read_data_block(block_num, block_buffer) != 0) {
                             fprintf(stderr, "Error reading directory data block %u (from double indirect) while searching for '%s'\n", block_num, name);
                             continue; // Skip block
                        }
                        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                            if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                                entries_searched++;
                                if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                                    *entry_inode_num = entries[j].inode_num; // Found it!
                                    return 0; // Return success
                                }
                                if (entries_searched >= total_entries_in_inode) goto search_end; // Optimization
                            }
                        }
                    }
                } else {
                     fprintf(stderr, "Error reading single indirect block %u (from double indirect) while searching for '%s'\n", single_indirect_block_num, name);
                }
            }
        } else {
             fprintf(stderr, "Error reading double indirect block %u while searching for '%s'\n", dir_inode.double_indirect, name);
        }
    }

search_end: // Label for goto statement.
    return -1; // Entry not found after searching available blocks.
}

/**
 * @brief Adds a directory entry (name -> inode mapping) to a directory.
 * Finds an empty slot or allocates a new block if needed. Updates inode size.
 * @param dir_inode_num Inode number of the directory to modify.
 * @param name Name of the new entry.
 * @param entry_inode_num Inode number the new entry points to.
 * @return 0 on success, -1 on error.
 */
int add_entry_to_dir(uint32_t dir_inode_num, const char *name, uint32_t entry_inode_num) {
    // Read the directory inode.
    Inode dir_inode;
    if (read_inode(dir_inode_num, &dir_inode) != 0 || !dir_inode.is_directory) {
        fprintf(stderr, "Cannot add entry: Inode %u is not a valid directory.\n", dir_inode_num);
        return -1;
    }

    // Check if the provided name exceeds the maximum allowed length.
     if (strlen(name) > MAX_FILENAME_LEN) {
         fprintf(stderr, "Error: Filename '%s' is too long (max %d chars).\n", name, MAX_FILENAME_LEN);
         return -1;
     }

    // Buffer for reading/writing directory data blocks.
    char block_buffer[BLOCK_SIZE];
    DirectoryEntry *entries = (DirectoryEntry *)block_buffer; // Cast for easy access.
    int found_slot = 0; // Flag: 1 if an empty slot is found, 0 otherwise.
    uint32_t target_block_num = UINT32_MAX; // Block number where the empty slot is found.
    int target_entry_index = -1; // Index within the target block where the empty slot is found.
    // Keep track of the first unused pointer slot in the inode's block list.
    uint32_t first_free_block_ptr_index = UINT32_MAX;
    int first_free_block_level = -1; // -1: none, 0: direct, 1: single, 2: double
    uint32_t single_indirect_block_for_new_ptr = UINT32_MAX; // Track which single indirect block needs update
    uint32_t double_indirect_block_for_new_ptr = UINT32_MAX; // Track which double indirect block needs update


    // --- Phase 1: Search existing blocks for an empty slot (inode_num == 0) ---
    // --- Search Direct Blocks ---
    for (int i = 0; i < MAX_DIRECT_POINTERS; ++i) {
        uint32_t block_num = dir_inode.direct_blocks[i];
        if (block_num == 0 || block_num == UINT32_MAX) { // Found unused direct pointer slot
             if (first_free_block_level == -1) { // If no free slot found yet
                 first_free_block_level = 0;
                 first_free_block_ptr_index = i;
             }
             continue; // Skip to next pointer.
        }
        // Read the existing data block.
        if (read_data_block(block_num, block_buffer) != 0) {
             fprintf(stderr, "Warning: Error reading dir block %u while searching for empty slot.\n", block_num);
             continue; // Skip this block on error.
        }
        // Iterate through entry slots within the current block.
        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
             if (entries[j].inode_num == 0) { // Found an empty slot!
                 target_block_num = block_num;
                 target_entry_index = j;
                 found_slot = 1;
                 goto slot_found; // Exit the search loops.
             }
        }
    }

    // --- Search Single Indirect Block ---
    if (!found_slot && dir_inode.single_indirect != 0 && dir_inode.single_indirect != UINT32_MAX) {
        uint32_t pointers[POINTERS_PER_BLOCK];
        char indirect_block_buffer[BLOCK_SIZE];
        if (read_data_block(dir_inode.single_indirect, indirect_block_buffer) == 0) {
            memcpy(pointers, indirect_block_buffer, BLOCK_SIZE);
            for (int i = 0; i < POINTERS_PER_BLOCK; ++i) {
                uint32_t block_num = pointers[i];
                if (block_num == 0 || block_num == UINT32_MAX) { // Found unused pointer in single indirect block
                    if (first_free_block_level == -1) {
                        first_free_block_level = 1;
                        first_free_block_ptr_index = i;
                        single_indirect_block_for_new_ptr = dir_inode.single_indirect;
                    }
                    continue;
                }
                if (read_data_block(block_num, block_buffer) != 0) {
                    fprintf(stderr, "Warning: Error reading dir block %u (from single indirect) while searching for empty slot.\n", block_num);
                    continue;
                }
                for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                    if (entries[j].inode_num == 0) { // Found empty slot
                        target_block_num = block_num;
                        target_entry_index = j;
                        found_slot = 1;
                        goto slot_found;
                    }
                }
            }
        } else {
             fprintf(stderr, "Warning: Error reading single indirect block %u while searching for empty slot.\n", dir_inode.single_indirect);
        }
    } else if (!found_slot && first_free_block_level == -1) { // Check if inode needs single indirect block itself
         first_free_block_level = 1;
         first_free_block_ptr_index = 0; // Will allocate the single indirect block and use index 0 within it
         single_indirect_block_for_new_ptr = 0; // Indicate allocation needed
    }


    // --- Search Double Indirect Block ---
    if (!found_slot && dir_inode.double_indirect != 0 && dir_inode.double_indirect != UINT32_MAX) {
        uint32_t pointers1[POINTERS_PER_BLOCK]; // Pointers in double indirect block
        char block_buffer1[BLOCK_SIZE];       // Buffer for double indirect block
        if (read_data_block(dir_inode.double_indirect, block_buffer1) == 0) {
            memcpy(pointers1, block_buffer1, BLOCK_SIZE);
            for (int idx1 = 0; idx1 < POINTERS_PER_BLOCK; ++idx1) {
                uint32_t single_indirect_block_num = pointers1[idx1];
                if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) { // Found unused pointer in double indirect block
                    if (first_free_block_level == -1) {
                        first_free_block_level = 2;
                        first_free_block_ptr_index = idx1 * POINTERS_PER_BLOCK; // Store combined index (idx1 for outer, 0 for inner)
                        double_indirect_block_for_new_ptr = dir_inode.double_indirect;
                        single_indirect_block_for_new_ptr = 0; // Indicate allocation needed for single indirect
                    }
                    continue;
                }

                uint32_t pointers2[POINTERS_PER_BLOCK]; // Pointers in single indirect block
                char block_buffer2[BLOCK_SIZE];       // Buffer for single indirect block
                if (read_data_block(single_indirect_block_num, block_buffer2) == 0) {
                    memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                    for (int i = 0; i < POINTERS_PER_BLOCK; ++i) {
                        uint32_t block_num = pointers2[i];
                         if (block_num == 0 || block_num == UINT32_MAX) { // Found unused pointer in single indirect block (via double)
                             if (first_free_block_level == -1) {
                                 first_free_block_level = 2;
                                 // Store combined index: idx1 for double indirect, i for single indirect
                                 first_free_block_ptr_index = idx1 * POINTERS_PER_BLOCK + i;
                                 double_indirect_block_for_new_ptr = dir_inode.double_indirect; // Outer block
                                 single_indirect_block_for_new_ptr = single_indirect_block_num; // Inner block
                             }
                             continue;
                         }
                        if (read_data_block(block_num, block_buffer) != 0) {
                             fprintf(stderr, "Warning: Error reading dir block %u (from double indirect) while searching for empty slot.\n", block_num);
                             continue;
                        }
                        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                            if (entries[j].inode_num == 0) { // Found empty slot
                                target_block_num = block_num;
                                target_entry_index = j;
                                found_slot = 1;
                                goto slot_found;
                            }
                        }
                    }
                } else {
                     fprintf(stderr, "Warning: Error reading single indirect block %u (from double) while searching for empty slot.\n", single_indirect_block_num);
                }
                 if (found_slot) goto slot_found; // Exit outer loop if found
            }
        } else {
             fprintf(stderr, "Warning: Error reading double indirect block %u while searching for empty slot.\n", dir_inode.double_indirect);
        }
    } else if (!found_slot && first_free_block_level == -1) { // Check if inode needs double indirect block itself
         first_free_block_level = 2;
         first_free_block_ptr_index = 0; // Will allocate double, single, and use index 0 in single
         double_indirect_block_for_new_ptr = 0; // Indicate allocation needed
         single_indirect_block_for_new_ptr = 0; // Indicate allocation needed
    }


slot_found: // Label for goto.
    // --- Phase 2: Handle the case where an empty slot was found ---
    if (found_slot) {
        // Re-read the target block (might be needed if indirect search modified block_buffer).
        if (read_data_block(target_block_num, block_buffer) != 0) {
            fprintf(stderr, "Error re-reading block %u to add entry.\n", target_block_num);
            return -1;
        }
        // Fill the empty slot with the new entry's data.
        strncpy(entries[target_entry_index].name, name, MAX_FILENAME_LEN);
        entries[target_entry_index].name[MAX_FILENAME_LEN] = '\0'; // Ensure null termination.
        entries[target_entry_index].inode_num = entry_inode_num;

        // Write the modified block back to the data segment.
        if (write_data_block(target_block_num, block_buffer) != 0) {
            fprintf(stderr, "Error writing updated directory block %u.\n", target_block_num);
            // Attempt to revert the change in memory (won't fix disk).
            entries[target_entry_index].inode_num = 0;
            return -1;
        }

        // Increment the directory inode's size (number of valid entries).
        dir_inode.size++;
        // Write the updated inode back to the inode segment.
        if (write_inode(dir_inode_num, &dir_inode) != 0) {
             fprintf(stderr, "Error updating directory inode %u size after adding entry.\n", dir_inode_num);
             // Inconsistency: block updated, but inode size not. Critical error.
             return -1;
        }
        return 0; // Success: added entry to an existing slot.

    } else {
        // --- Phase 3: No empty slot found, allocate a new block ---
        // Allocate a new data block for the directory entry.
        uint32_t new_block = find_free_data_block();
        if (new_block == UINT32_MAX) {
            fprintf(stderr, "Failed to allocate new data block for directory %u.\n", dir_inode_num);
            return -1;
        }

        // --- Store pointer to the new block in the inode ---
        int pointer_stored = 0;
        // Use the information gathered during the search (first_free_block_level, etc.)
        // This logic mirrors parts of get_block_for_offset's allocation path.
        if (first_free_block_level == 0) { // Store in direct block pointer
             dir_inode.direct_blocks[first_free_block_ptr_index] = new_block;
             pointer_stored = 1;
        } else if (first_free_block_level == 1) { // Store in single indirect block
            uint32_t single_indirect_num = single_indirect_block_for_new_ptr;
            // Allocate single indirect block itself if needed
            if (single_indirect_num == 0 || single_indirect_num == UINT32_MAX) {
                single_indirect_num = find_free_data_block();
                if(single_indirect_num == UINT32_MAX) { free_data_block(new_block); return -1; }
                char zero_buf[BLOCK_SIZE] = {0};
                if(write_data_block(single_indirect_num, zero_buf) != 0) { free_data_block(new_block); free_data_block(single_indirect_num); return -1; }
                dir_inode.single_indirect = single_indirect_num; // Update inode pointer
                // No need to set inode_dirty here, we write inode at the end anyway
            }
            // Read, modify, write the single indirect block
            uint32_t pointers[POINTERS_PER_BLOCK];
            char indirect_buf[BLOCK_SIZE];
            if(read_data_block(single_indirect_num, indirect_buf) == 0) {
                memcpy(pointers, indirect_buf, BLOCK_SIZE);
                pointers[first_free_block_ptr_index] = new_block; // Store new block pointer
                if(write_data_block(single_indirect_num, (char*)pointers) == 0) {
                    pointer_stored = 1;
                } else { // Failed writing indirect block
                     fprintf(stderr, "Error writing single indirect block %u when adding entry\n", single_indirect_num);
                }
            } else { // Failed reading indirect block
                 fprintf(stderr, "Error reading single indirect block %u when adding entry\n", single_indirect_num);
            }
            if (!pointer_stored) { free_data_block(new_block); /* Maybe free single_indirect_num if newly allocated? */ return -1; }

        } else if (first_free_block_level == 2) { // Store in double indirect block path
             uint32_t double_indirect_num = double_indirect_block_for_new_ptr;
             uint32_t single_indirect_num = single_indirect_block_for_new_ptr;
             uint32_t idx1 = first_free_block_ptr_index / POINTERS_PER_BLOCK;
             uint32_t idx2 = first_free_block_ptr_index % POINTERS_PER_BLOCK;

             // Allocate double indirect block itself if needed
             if (double_indirect_num == 0 || double_indirect_num == UINT32_MAX) {
                 double_indirect_num = find_free_data_block();
                 if(double_indirect_num == UINT32_MAX) { free_data_block(new_block); return -1; }
                 char zero_buf[BLOCK_SIZE] = {0};
                 if(write_data_block(double_indirect_num, zero_buf) != 0) { free_data_block(new_block); free_data_block(double_indirect_num); return -1; }
                 dir_inode.double_indirect = double_indirect_num;
             }

             // Read the double indirect block
             uint32_t pointers1[POINTERS_PER_BLOCK];
             char block_buffer1[BLOCK_SIZE];
             if(read_data_block(double_indirect_num, block_buffer1) != 0) { free_data_block(new_block); return -1; }
             memcpy(pointers1, block_buffer1, BLOCK_SIZE);

             // Allocate the single indirect block if needed
             if (single_indirect_num == 0 || single_indirect_num == UINT32_MAX) {
                 single_indirect_num = find_free_data_block();
                 if(single_indirect_num == UINT32_MAX) { free_data_block(new_block); /* Maybe free double if new? */ return -1; }
                 char zero_buf[BLOCK_SIZE] = {0};
                 if(write_data_block(single_indirect_num, zero_buf) != 0) { free_data_block(new_block); free_data_block(single_indirect_num); return -1; }
                 pointers1[idx1] = single_indirect_num; // Store pointer in double indirect block
                 // Write double indirect block back
                 if(write_data_block(double_indirect_num, (char*)pointers1) != 0) { free_data_block(new_block); free_data_block(single_indirect_num); return -1;}
             }

             // Read, modify, write the single indirect block
             uint32_t pointers2[POINTERS_PER_BLOCK];
             char block_buffer2[BLOCK_SIZE];
             if(read_data_block(single_indirect_num, block_buffer2) == 0) {
                 memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                 pointers2[idx2] = new_block; // Store new block pointer
                 if(write_data_block(single_indirect_num, (char*)pointers2) == 0) {
                     pointer_stored = 1;
                 } else { // Failed writing single indirect block
                     fprintf(stderr, "Error writing single indirect block %u (via double) when adding entry\n", single_indirect_num);
                 }
             } else { // Failed reading single indirect block
                 fprintf(stderr, "Error reading single indirect block %u (via double) when adding entry\n", single_indirect_num);
             }
             if (!pointer_stored) { free_data_block(new_block); /* Maybe free single/double if new? */ return -1; }
        }


        // If we couldn't find a place to store the pointer...
        if (!pointer_stored) {
            fprintf(stderr, "Directory inode %u is full (no place to store new block pointer).\n", dir_inode_num);
            free_data_block(new_block); // Free the data block we allocated.
            return -1;
        }

        // --- Initialize the new block with the first entry ---
        memset(block_buffer, 0, BLOCK_SIZE); // Zero out the entire block first.
        strncpy(entries[0].name, name, MAX_FILENAME_LEN);
        entries[0].name[MAX_FILENAME_LEN] = '\0';
        entries[0].inode_num = entry_inode_num;

        // Write the initialized block to the data segment.
        if (write_data_block(new_block, block_buffer) != 0) {
            fprintf(stderr, "Error writing newly allocated directory block %u.\n", new_block);
            // Attempt to rollback: clear the pointer in the inode and free the block.
            // This rollback is complex if indirect blocks were involved/allocated. Simplified rollback:
             if (first_free_block_level == 0) {
                 dir_inode.direct_blocks[first_free_block_ptr_index] = 0;
             } // TODO: Add rollback for indirect pointers
             write_inode(dir_inode_num, &dir_inode); // Attempt to write back inode change
             free_data_block(new_block); // Free the allocated block.
            return -1;
        }

        // --- Update directory inode size and write it back ---
        dir_inode.size++;
        if (write_inode(dir_inode_num, &dir_inode) != 0) {
            fprintf(stderr, "Error updating directory inode %u after allocating new block.\n", dir_inode_num);
            // Inconsistency: new block written and linked, but inode size incorrect. Critical error.
            return -1;
        }

        return 0; // Success: allocated new block and added entry.
    }
}


/**
 * @brief Removes an entry from a directory by marking its slot as free (inode_num=0).
 * Does not currently decrease inode size or free the block if it becomes empty.
 * @param dir_inode_num Inode number of the directory to modify.
 * @param name Name of the entry to remove.
 * @return 0 on success, -1 on error or if entry not found.
 */
int remove_entry_from_dir(uint32_t dir_inode_num, const char *name) {
    // Read the directory inode.
    Inode dir_inode;
    if (read_inode(dir_inode_num, &dir_inode) != 0 || !dir_inode.is_directory) {
        fprintf(stderr, "Cannot remove entry: Inode %u is not a valid directory.\n", dir_inode_num);
        return -1;
    }

    // Buffer for directory block data.
    char block_buffer[BLOCK_SIZE];
    DirectoryEntry *entries = (DirectoryEntry *)block_buffer; // Cast for easy access.
    size_t entries_searched = 0; // Count valid entries encountered.
    size_t total_entries_in_inode = dir_inode.size; // Expected number of valid entries.
    int found = 0; // Flag: 1 if entry is found and removed, 0 otherwise.

    // --- Search Direct Blocks ---
    for (int i = 0; i < MAX_DIRECT_POINTERS && entries_searched < total_entries_in_inode; ++i) {
        uint32_t block_num = dir_inode.direct_blocks[i];
        if (block_num == 0 || block_num == UINT32_MAX) continue; // Skip unused pointers.

        if (read_data_block(block_num, block_buffer) != 0) {
             fprintf(stderr, "Warning: Error reading dir block %u while removing '%s'.\n", block_num, name);
             continue; // Skip this block on error.
        }

        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
            if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) { // If valid entry
                entries_searched++; // Count valid entry.
                if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                    // Found the entry to remove.
                    uint32_t inode_to_remove = entries[j].inode_num; // Store for potential rollback.
                    entries[j].inode_num = 0; // Mark as free/invalid.
                    memset(entries[j].name, 0, MAX_FILENAME_LEN + 1); // Clear name.

                    if (write_data_block(block_num, block_buffer) != 0) {
                        fprintf(stderr, "Error writing directory block %u after removing entry '%s'.\n", block_num, name);
                        // Attempt rollback in memory.
                        entries[j].inode_num = inode_to_remove;
                        strncpy(entries[j].name, name, MAX_FILENAME_LEN);
                        return -1; // Indicate error.
                    }
                    found = 1;
                    goto entry_removed; // Exit loops.
                }
                if (entries_searched >= total_entries_in_inode) goto entry_removed; // Optimization
            }
        }
    }

    // --- Search Single Indirect Block ---
    if (!found && dir_inode.single_indirect != 0 && dir_inode.single_indirect != UINT32_MAX && entries_searched < total_entries_in_inode) {
        uint32_t pointers[POINTERS_PER_BLOCK];
        char indirect_block_buffer[BLOCK_SIZE];
        if (read_data_block(dir_inode.single_indirect, indirect_block_buffer) == 0) {
            memcpy(pointers, indirect_block_buffer, BLOCK_SIZE);
            for (int i = 0; i < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++i) {
                uint32_t block_num = pointers[i];
                if (block_num == 0 || block_num == UINT32_MAX) continue;

                if (read_data_block(block_num, block_buffer) != 0) {
                    fprintf(stderr, "Warning: Error reading dir block %u (from single indirect) while removing '%s'.\n", block_num, name);
                    continue;
                }
                for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                    if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                        entries_searched++;
                        if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                             uint32_t inode_to_remove = entries[j].inode_num;
                             entries[j].inode_num = 0;
                             memset(entries[j].name, 0, MAX_FILENAME_LEN + 1);
                             if (write_data_block(block_num, block_buffer) != 0) {
                                 fprintf(stderr, "Error writing directory block %u after removing entry '%s'.\n", block_num, name);
                                 entries[j].inode_num = inode_to_remove;
                                 strncpy(entries[j].name, name, MAX_FILENAME_LEN);
                                 return -1;
                             }
                             found = 1;
                             goto entry_removed;
                        }
                         if (entries_searched >= total_entries_in_inode) goto entry_removed; // Optimization
                    }
                }
            }
        } else {
             fprintf(stderr, "Warning: Error reading single indirect block %u while removing '%s'.\n", dir_inode.single_indirect, name);
        }
    }

    // --- Search Double Indirect Block ---
    if (!found && dir_inode.double_indirect != 0 && dir_inode.double_indirect != UINT32_MAX && entries_searched < total_entries_in_inode) {
        uint32_t pointers1[POINTERS_PER_BLOCK];
        char block_buffer1[BLOCK_SIZE];
        if (read_data_block(dir_inode.double_indirect, block_buffer1) == 0) {
            memcpy(pointers1, block_buffer1, BLOCK_SIZE);
            for (int idx1 = 0; idx1 < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++idx1) {
                uint32_t single_indirect_block_num = pointers1[idx1];
                if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) continue;

                uint32_t pointers2[POINTERS_PER_BLOCK];
                char block_buffer2[BLOCK_SIZE];
                if (read_data_block(single_indirect_block_num, block_buffer2) == 0) {
                    memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                    for (int i = 0; i < POINTERS_PER_BLOCK && entries_searched < total_entries_in_inode; ++i) {
                        uint32_t block_num = pointers2[i];
                        if (block_num == 0 || block_num == UINT32_MAX) continue;

                        if (read_data_block(block_num, block_buffer) != 0) {
                             fprintf(stderr, "Warning: Error reading dir block %u (from double indirect) while removing '%s'.\n", block_num, name);
                             continue;
                        }
                        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                            if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                                entries_searched++;
                                if (strncmp(entries[j].name, name, MAX_FILENAME_LEN) == 0) {
                                    uint32_t inode_to_remove = entries[j].inode_num;
                                    entries[j].inode_num = 0;
                                    memset(entries[j].name, 0, MAX_FILENAME_LEN + 1);
                                    if (write_data_block(block_num, block_buffer) != 0) {
                                        fprintf(stderr, "Error writing directory block %u after removing entry '%s'.\n", block_num, name);
                                        entries[j].inode_num = inode_to_remove;
                                        strncpy(entries[j].name, name, MAX_FILENAME_LEN);
                                        return -1;
                                    }
                                    found = 1;
                                    goto entry_removed;
                                }
                                if (entries_searched >= total_entries_in_inode) goto entry_removed; // Optimization
                            }
                        }
                    }
                } else {
                     fprintf(stderr, "Warning: Error reading single indirect block %u (from double) while removing '%s'.\n", single_indirect_block_num, name);
                }
                 if (found) goto entry_removed; // Exit outer loop if found
            }
        } else {
             fprintf(stderr, "Warning: Error reading double indirect block %u while removing '%s'.\n", dir_inode.double_indirect, name);
        }
    }

entry_removed: // Label for goto.
    // Check if the entry was actually found and removed.
    if (!found) {
        fprintf(stderr, "Entry '%s' not found in directory inode %u.\n", name, dir_inode_num);
        return -1; // Not found.
    }

    // Optional: Could add logic here to check if the block is now empty
    // and potentially free the block and remove its pointer from the inode.

    return 0; // Success.
}


/**
 * @brief Creates a new directory, including its "." and ".." entries.
 * @param parent_inode_num Inode number of the parent directory.
 * @param name Name of the new directory to create.
 * @return Inode number of the new directory, or UINT32_MAX on error.
 */
uint32_t create_directory(uint32_t parent_inode_num, const char *name) {
     // 1. Allocate an inode for the new directory.
     uint32_t new_dir_inode_num = find_free_inode();
     if (new_dir_inode_num == UINT32_MAX) {
         fprintf(stderr, "Failed to allocate inode for new directory '%s'\n", name);
         return UINT32_MAX;
     }

     // 2. Allocate a data block to store the directory's entries (initially "." and "..").
     uint32_t new_dir_data_block = find_free_data_block();
     if (new_dir_data_block == UINT32_MAX) {
         fprintf(stderr, "Failed to allocate data block for new directory '%s'\n", name);
         free_inode(new_dir_inode_num); // Rollback: free the allocated inode.
         return UINT32_MAX;
     }

     // 3. Initialize the Inode structure for the new directory.
     Inode new_dir_inode;
     memset(&new_dir_inode, 0, sizeof(Inode)); // Zero out the structure.
     new_dir_inode.is_directory = 1; // Mark as a directory.
     new_dir_inode.mode = S_IFDIR | 0755; // Set type to directory and default permissions.
     new_dir_inode.size = 2; // Initial size is 2 entries (".", "..").
     new_dir_inode.direct_blocks[0] = new_dir_data_block; // Point the first direct block pointer to the allocated data block.
     // Other block pointers are implicitly 0 from memset.

     // 4. Initialize the data block with "." and ".." entries.
     char block_buffer[BLOCK_SIZE];
     DirectoryEntry *entries = (DirectoryEntry *)block_buffer; // Cast buffer.
     memset(block_buffer, 0, BLOCK_SIZE); // Zero out the block.

     // Create "." entry, pointing to the new directory's own inode.
     strncpy(entries[0].name, ".", MAX_FILENAME_LEN);
     entries[0].inode_num = new_dir_inode_num;

     // Create ".." entry, pointing to the parent directory's inode.
     strncpy(entries[1].name, "..", MAX_FILENAME_LEN);
     entries[1].inode_num = parent_inode_num;

     // 5. Write the initialized inode and data block to their respective segments.
     if (write_inode(new_dir_inode_num, &new_dir_inode) != 0) {
         fprintf(stderr, "Failed to write inode for new directory '%s'\n", name);
         // Rollback: free the allocated data block and inode.
         free_data_block(new_dir_data_block);
         free_inode(new_dir_inode_num);
         return UINT32_MAX;
     }
     if (write_data_block(new_dir_data_block, block_buffer) != 0) {
         fprintf(stderr, "Failed to write data block for new directory '%s'\n", name);
         // Rollback attempt: Free the inode. Data block write failed, state might be inconsistent.
         free_inode(new_dir_inode_num);
         return UINT32_MAX;
     }

     // 6. Add an entry for the newly created directory into its parent directory.
     if (add_entry_to_dir(parent_inode_num, name, new_dir_inode_num) != 0) {
         fprintf(stderr, "Failed to add entry for new directory '%s' to parent inode %u\n", name, parent_inode_num);
         // Rollback: Free the data block and inode of the directory we just created.
         free_data_block(new_dir_data_block);
         free_inode(new_dir_inode_num);
         return UINT32_MAX;
     }

     return new_dir_inode_num; // Success: return the inode number of the new directory.
}


/**
 * @brief Gets the data block number for a given file offset, allocating if needed.
 * Handles direct, single, and double indirect blocks. Modifies inode if allocation occurs.
 * @param inode_num Inode number of the file.
 * @param offset Byte offset within the file.
 * @param allocate 1 to allocate blocks if needed, 0 to only retrieve existing ones.
 * @return Data block number, or UINT32_MAX on error or if block doesn't exist (and allocate=0).
 */
uint32_t get_block_for_offset(uint32_t inode_num, size_t offset, int allocate) {
    // Read the inode corresponding to the file.
    Inode inode;
    if (read_inode(inode_num, &inode) != 0) {
        fprintf(stderr, "get_block_for_offset: Failed to read inode %u\n", inode_num);
        return UINT32_MAX;
    }

    // Calculate the logical block index within the file (0-based).
    uint32_t block_index = offset / BLOCK_SIZE;
    // Variable to store the resulting data block number.
    uint32_t current_block_num = UINT32_MAX;
    // Flag to track if the inode was modified (needs writing back).
    int inode_dirty = 0;

    // --- 1. Check Direct Blocks ---
    // If the logical block index falls within the range of direct pointers...
    if (block_index < MAX_DIRECT_POINTERS) {
        // Get the block number from the direct pointer array.
        current_block_num = inode.direct_blocks[block_index];
        // Check if the pointer is unused (0 or UINT32_MAX).
        if (current_block_num == 0 || current_block_num == UINT32_MAX) {
            // If allocation is requested...
            if (allocate) {
                // Find and allocate a new free data block.
                uint32_t new_block = find_free_data_block();
                if (new_block == UINT32_MAX) return UINT32_MAX; // Allocation failed.
                // Store the new block number in the inode's direct pointer.
                inode.direct_blocks[block_index] = new_block;
                inode_dirty = 1; // Mark inode as modified.
                current_block_num = new_block; // Set the result.
            } else {
                // Block doesn't exist and allocation not requested.
                return UINT32_MAX;
            }
        }
        // Found or allocated the block via direct pointer. Go to end.
        goto end_get_block;
    }

    // --- Calculate indices relative to indirect blocks ---
    // Adjust block_index to be relative to the start of the single indirect range.
    block_index -= MAX_DIRECT_POINTERS;

    // --- 2. Check Single Indirect Block ---
    // Calculate the maximum index covered by the single indirect block.
    uint32_t single_indirect_limit = POINTERS_PER_BLOCK;
    // If the adjusted block index falls within this range...
    if (block_index < single_indirect_limit) {
        // Get the block number of the single indirect block itself from the inode.
        uint32_t single_indirect_block_num = inode.single_indirect;

        // Check if the single indirect block itself needs to be allocated.
        if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) {
            // If allocation is requested...
            if (allocate) {
                 // Allocate a block for the single indirect block.
                 single_indirect_block_num = find_free_data_block();
                 if (single_indirect_block_num == UINT32_MAX) return UINT32_MAX; // Allocation failed.
                 // Zero out the newly allocated indirect block before use.
                 char zero_buffer[BLOCK_SIZE] = {0};
                 if (write_data_block(single_indirect_block_num, zero_buffer) != 0) {
                     fprintf(stderr, "get_block_for_offset: Failed to zero single indirect block %u\n", single_indirect_block_num);
                     free_data_block(single_indirect_block_num); // Rollback allocation.
                     return UINT32_MAX;
                 }
                 // Store the new indirect block number in the inode.
                 inode.single_indirect = single_indirect_block_num;
                 inode_dirty = 1; // Mark inode as modified.
            } else {
                // Indirect block doesn't exist and allocation not requested.
                return UINT32_MAX;
            }
        }

        // Read the contents of the single indirect block (which contains direct block pointers).
        uint32_t pointers[POINTERS_PER_BLOCK]; // Array to hold pointers.
        char indirect_block_buffer[BLOCK_SIZE]; // Buffer for reading.
        if(read_data_block(single_indirect_block_num, indirect_block_buffer) != 0) {
             fprintf(stderr, "get_block_for_offset: Failed read single indirect block %u\n", single_indirect_block_num);
             return UINT32_MAX;
        }
        memcpy(pointers, indirect_block_buffer, BLOCK_SIZE); // Copy data to pointer array.

        // Get the target data block pointer from the indirect block using the adjusted block_index.
        current_block_num = pointers[block_index];
        // Check if the target data block needs to be allocated.
        if (current_block_num == 0 || current_block_num == UINT32_MAX) {
            // If allocation is requested...
            if (allocate) {
                // Allocate the actual data block.
                uint32_t new_block = find_free_data_block();
                if (new_block == UINT32_MAX) return UINT32_MAX; // Allocation failed.
                // Store the new data block number in the indirect block's pointer array.
                pointers[block_index] = new_block;
                 // Write the modified indirect block back to its data segment.
                 memcpy(indirect_block_buffer, pointers, BLOCK_SIZE);
                 if (write_data_block(single_indirect_block_num, indirect_block_buffer) != 0) {
                     fprintf(stderr, "get_block_for_offset: Failed write single indirect block %u after alloc data block\n", single_indirect_block_num);
                     free_data_block(new_block); // Rollback data block allocation.
                     return UINT32_MAX;
                 }
                 current_block_num = new_block; // Set the result.
            } else {
                // Data block doesn't exist and allocation not requested.
                return UINT32_MAX;
            }
        }
        // Found or allocated the block via single indirect pointer. Go to end.
        goto end_get_block;
    }

    // --- 3. Check Double Indirect Block ---
    // Adjust block_index to be relative to the start of the double indirect range.
    block_index -= single_indirect_limit;
    // Calculate the maximum index covered by the double indirect block.
    uint32_t double_indirect_limit = POINTERS_PER_BLOCK * POINTERS_PER_BLOCK;
    // If the further adjusted block index falls within this range...
    if (block_index < double_indirect_limit) {
        // Get the block number of the double indirect block itself from the inode.
        uint32_t double_indirect_block_num = inode.double_indirect;
        // Calculate the index within the double indirect block (points to a single indirect block).
        uint32_t idx1 = block_index / POINTERS_PER_BLOCK;
        // Calculate the index within the single indirect block (points to the data block).
        uint32_t idx2 = block_index % POINTERS_PER_BLOCK;

        // Check if the double indirect block itself needs to be allocated.
        if (double_indirect_block_num == 0 || double_indirect_block_num == UINT32_MAX) {
            if (!allocate) return UINT32_MAX; // Not allocating.
            // Allocate the double indirect block.
            double_indirect_block_num = find_free_data_block();
            if (double_indirect_block_num == UINT32_MAX) return UINT32_MAX; // Allocation failed.
            // Zero out the new double indirect block.
            char zero_buffer[BLOCK_SIZE] = {0};
            if (write_data_block(double_indirect_block_num, zero_buffer) != 0) {
                fprintf(stderr, "get_block_for_offset: Failed zero double indirect block %u\n", double_indirect_block_num);
                free_data_block(double_indirect_block_num); return UINT32_MAX; // Rollback.
            }
            // Store the new double indirect block number in the inode.
            inode.double_indirect = double_indirect_block_num;
            inode_dirty = 1; // Mark inode as modified.
        }

        // Read the double indirect block (contains pointers to single indirect blocks).
        uint32_t pointers1[POINTERS_PER_BLOCK]; // Array for level 1 pointers.
        char block_buffer1[BLOCK_SIZE]; // Buffer for reading level 1.
        if(read_data_block(double_indirect_block_num, block_buffer1) != 0) {
            fprintf(stderr, "get_block_for_offset: Failed read double indirect block %u\n", double_indirect_block_num);
            return UINT32_MAX;
        }
        memcpy(pointers1, block_buffer1, BLOCK_SIZE); // Copy data to pointer array.

        // Get the pointer to the relevant single indirect block from the double indirect block (using idx1).
        uint32_t single_indirect_block_num = pointers1[idx1];
        // Check if this single indirect block needs to be allocated.
        if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) {
             if (!allocate) return UINT32_MAX; // Not allocating.
             // Allocate the single indirect block.
             single_indirect_block_num = find_free_data_block();
             if (single_indirect_block_num == UINT32_MAX) return UINT32_MAX; // Allocation failed.
             // Zero out the new single indirect block.
             char zero_buffer[BLOCK_SIZE] = {0};
             if (write_data_block(single_indirect_block_num, zero_buffer) != 0) {
                 fprintf(stderr, "get_block_for_offset: Failed zero single indirect block %u (from double)\n", single_indirect_block_num);
                 free_data_block(single_indirect_block_num); return UINT32_MAX; // Rollback.
             }
             // Store the pointer to the new single indirect block in the double indirect block's array.
             pointers1[idx1] = single_indirect_block_num;
             // Write the modified double indirect block back.
             memcpy(block_buffer1, pointers1, BLOCK_SIZE);
             if (write_data_block(double_indirect_block_num, block_buffer1) != 0) {
                 fprintf(stderr, "get_block_for_offset: Failed write double indirect block %u after alloc single indirect\n", double_indirect_block_num);
                 free_data_block(single_indirect_block_num); // Rollback single indirect allocation.
                 return UINT32_MAX;
             }
        }

        // Read the single indirect block (contains pointers to data blocks).
        uint32_t pointers2[POINTERS_PER_BLOCK]; // Array for level 2 pointers.
        char block_buffer2[BLOCK_SIZE]; // Buffer for reading level 2.
         if(read_data_block(single_indirect_block_num, block_buffer2) != 0) {
             fprintf(stderr, "get_block_for_offset: Failed read single indirect block %u (from double)\n", single_indirect_block_num);
             return UINT32_MAX;
         }
         memcpy(pointers2, block_buffer2, BLOCK_SIZE); // Copy data to pointer array.

        // Get the pointer to the target data block from the single indirect block (using idx2).
        current_block_num = pointers2[idx2];
        // Check if the target data block needs to be allocated.
        if (current_block_num == 0 || current_block_num == UINT32_MAX) {
            if (!allocate) return UINT32_MAX; // Not allocating.
            // Allocate the actual data block.
            uint32_t new_block = find_free_data_block();
            if (new_block == UINT32_MAX) return UINT32_MAX; // Allocation failed.
            // Store the pointer to the new data block in the single indirect block's array.
            pointers2[idx2] = new_block;
            // Write the modified single indirect block back.
            memcpy(block_buffer2, pointers2, BLOCK_SIZE);
            if (write_data_block(single_indirect_block_num, block_buffer2) != 0) {
                fprintf(stderr, "get_block_for_offset: Failed write single indirect block %u after alloc data block (from double)\n", single_indirect_block_num);
                free_data_block(new_block); // Rollback data block allocation.
                return UINT32_MAX;
            }
            current_block_num = new_block; // Set the result.
        }
        // Found or allocated the block via double indirect pointer. Go to end.
        goto end_get_block;
    }


    // --- Offset Out of Bounds ---
    // If the offset calculation reaches here, it means the requested offset
    // is beyond the maximum file size supported by direct, single, and double indirect blocks.
    fprintf(stderr, "Offset %zu is beyond the maximum file size supported by direct, single, and double indirect blocks.\n", offset);
    return UINT32_MAX; // Indicate error.

end_get_block: // Label for goto statements.
    // --- Write Inode Back (if modified) ---
    // If the inode_dirty flag was set during allocation...
    if (inode_dirty) {
        // Write the modified inode structure back to the inode segment.
        if (write_inode(inode_num, &inode) != 0) {
             fprintf(stderr, "get_block_for_offset: Failed to write updated inode %u back to disk.\n", inode_num);
             // Critical error: Allocation succeeded, but inode update failed. Filesystem inconsistent.
             // Proper rollback is complex. Return error for now.
             return UINT32_MAX;
        }
    }
    // Return the found or allocated data block number.
    return current_block_num;
}


// --- Core File System Operations ---

/**
 * @brief Comparison function for qsort to sort DirectoryEntry structs by name.
 * @param a Pointer to the first DirectoryEntry.
 * @param b Pointer to the second DirectoryEntry.
 * @return Negative value if a < b, 0 if a == b, positive value if a > b.
 */
int compare_direntry_names(const void *a, const void *b) {
    // Cast void pointers to DirectoryEntry pointers.
    const DirectoryEntry *entry_a = (const DirectoryEntry *)a;
    const DirectoryEntry *entry_b = (const DirectoryEntry *)b;
    // Use strcmp for lexicographical comparison of names.
    return strcmp(entry_a->name, entry_b->name);
}


/**
 * @brief Recursively lists directory contents, sorted alphabetically.
 * @param dir_inode_num Inode number of the directory to list.
 * @param depth Current recursion depth (for indentation).
 * @param debug_mode 1 for detailed debug output, 0 for standard listing.
 */
void list_recursive(uint32_t dir_inode_num, int depth, int debug_mode) {
    // Read the directory inode.
    Inode dir_inode;
    if (read_inode(dir_inode_num, &dir_inode) != 0 || !dir_inode.is_directory) {
        // Fail silently if not a directory.
        return;
    }

    // --- Step 1: Collect all valid directory entries into a dynamic array ---
    DirectoryEntry *all_entries = NULL; // Pointer to the dynamic array.
    size_t entry_count = 0; // Number of entries currently in the array.
    size_t capacity = 0; // Current allocated capacity of the array.

    // Buffer for reading directory blocks.
    char block_buffer[BLOCK_SIZE];
    DirectoryEntry *entries_in_block = (DirectoryEntry *)block_buffer; // Cast buffer.
    size_t entries_processed_total = 0; // Count valid entries found across all blocks.
    size_t valid_entries_in_inode = dir_inode.size; // Expected number of valid entries.

    // --- Process Direct Blocks ---
    for (int i = 0; i < MAX_DIRECT_POINTERS && entries_processed_total < valid_entries_in_inode; ++i) {
        uint32_t block_num = dir_inode.direct_blocks[i];
        if (block_num == 0 || block_num == UINT32_MAX) continue; // Skip unused pointers.

        if (read_data_block(block_num, block_buffer) != 0) {
            fprintf(stderr, "Warning: Error reading directory data block %u during list.\n", block_num);
            continue; // Skip block on error.
        }

        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
            if (entries_in_block[j].inode_num != 0 && entries_in_block[j].inode_num != UINT32_MAX) { // Valid entry
                entries_processed_total++;
                if (strcmp(entries_in_block[j].name, ".") != 0 && strcmp(entries_in_block[j].name, "..") != 0) { // Exclude . and ..
                    if (entry_count >= capacity) { // Resize array if needed
                        capacity = (capacity == 0) ? 10 : capacity * 2;
                        DirectoryEntry *temp = realloc(all_entries, capacity * sizeof(DirectoryEntry));
                        if (!temp) { perror("Failed to allocate memory for directory listing"); free(all_entries); return; }
                        all_entries = temp;
                    }
                    all_entries[entry_count++] = entries_in_block[j]; // Add entry to array
                }
                 if (entries_processed_total >= valid_entries_in_inode) goto collect_end; // Optimization
            }
        }
    }

    // --- Process Single Indirect Block ---
    if (dir_inode.single_indirect != 0 && dir_inode.single_indirect != UINT32_MAX && entries_processed_total < valid_entries_in_inode) {
        uint32_t pointers[POINTERS_PER_BLOCK];
        char indirect_block_buffer[BLOCK_SIZE];
        if (read_data_block(dir_inode.single_indirect, indirect_block_buffer) == 0) {
            memcpy(pointers, indirect_block_buffer, BLOCK_SIZE);
            for (int i = 0; i < POINTERS_PER_BLOCK && entries_processed_total < valid_entries_in_inode; ++i) {
                uint32_t block_num = pointers[i];
                if (block_num == 0 || block_num == UINT32_MAX) continue;

                if (read_data_block(block_num, block_buffer) != 0) {
                    fprintf(stderr, "Warning: Error reading directory data block %u (from single indirect) during list.\n", block_num);
                    continue;
                }
                for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                     if (entries_in_block[j].inode_num != 0 && entries_in_block[j].inode_num != UINT32_MAX) {
                         entries_processed_total++;
                         if (strcmp(entries_in_block[j].name, ".") != 0 && strcmp(entries_in_block[j].name, "..") != 0) {
                             if (entry_count >= capacity) {
                                 capacity = (capacity == 0) ? 10 : capacity * 2;
                                 DirectoryEntry *temp = realloc(all_entries, capacity * sizeof(DirectoryEntry));
                                 if (!temp) { perror("Failed to allocate memory for directory listing"); free(all_entries); return; }
                                 all_entries = temp;
                             }
                             all_entries[entry_count++] = entries_in_block[j];
                         }
                         if (entries_processed_total >= valid_entries_in_inode) goto collect_end; // Optimization
                     }
                }
            }
        } else {
             fprintf(stderr, "Warning: Error reading single indirect block %u during list.\n", dir_inode.single_indirect);
        }
    }

    // --- Process Double Indirect Block ---
     if (dir_inode.double_indirect != 0 && dir_inode.double_indirect != UINT32_MAX && entries_processed_total < valid_entries_in_inode) {
        uint32_t pointers1[POINTERS_PER_BLOCK]; // Pointers in double indirect block
        char block_buffer1[BLOCK_SIZE];       // Buffer for double indirect block
        if (read_data_block(dir_inode.double_indirect, block_buffer1) == 0) {
            memcpy(pointers1, block_buffer1, BLOCK_SIZE);
            // Iterate through pointers in double indirect block (point to single indirect blocks)
            for (int idx1 = 0; idx1 < POINTERS_PER_BLOCK && entries_processed_total < valid_entries_in_inode; ++idx1) {
                uint32_t single_indirect_block_num = pointers1[idx1];
                if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) continue;

                uint32_t pointers2[POINTERS_PER_BLOCK]; // Pointers in single indirect block
                char block_buffer2[BLOCK_SIZE];       // Buffer for single indirect block
                if (read_data_block(single_indirect_block_num, block_buffer2) == 0) {
                    memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                    // Iterate through pointers in single indirect block (point to data blocks)
                    for (int i = 0; i < POINTERS_PER_BLOCK && entries_processed_total < valid_entries_in_inode; ++i) {
                        uint32_t block_num = pointers2[i];
                        if (block_num == 0 || block_num == UINT32_MAX) continue;

                        if (read_data_block(block_num, block_buffer) != 0) {
                             fprintf(stderr, "Warning: Error reading directory data block %u (from double indirect) during list.\n", block_num);
                             continue; // Skip block
                        }
                        for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                             if (entries_in_block[j].inode_num != 0 && entries_in_block[j].inode_num != UINT32_MAX) {
                                 entries_processed_total++;
                                 if (strcmp(entries_in_block[j].name, ".") != 0 && strcmp(entries_in_block[j].name, "..") != 0) {
                                     if (entry_count >= capacity) {
                                         capacity = (capacity == 0) ? 10 : capacity * 2;
                                         DirectoryEntry *temp = realloc(all_entries, capacity * sizeof(DirectoryEntry));
                                         if (!temp) { perror("Failed to allocate memory for directory listing"); free(all_entries); return; }
                                         all_entries = temp;
                                     }
                                     all_entries[entry_count++] = entries_in_block[j];
                                 }
                                 if (entries_processed_total >= valid_entries_in_inode) goto collect_end; // Optimization
                             }
                        }
                    }
                } else {
                     fprintf(stderr, "Warning: Error reading single indirect block %u (from double indirect) during list.\n", single_indirect_block_num);
                }
            }
        } else {
             fprintf(stderr, "Warning: Error reading double indirect block %u during list.\n", dir_inode.double_indirect);
        }
    }


collect_end: // Label for goto.

    // --- Step 2: Sort the collected entries alphabetically by name ---
    if (all_entries && entry_count > 0) {
        qsort(all_entries, entry_count, sizeof(DirectoryEntry), compare_direntry_names);
    }

    // --- Step 3: Print sorted entries and recurse into subdirectories ---
    for (size_t k = 0; k < entry_count; ++k) {
        // Print indentation based on recursion depth.
        for (int indent = 0; indent < depth; ++indent) {
             printf("  "); // Two spaces per level.
        }

        // Print the entry name (and inode number in debug mode).
        if (debug_mode) {
            printf("'%s' -> %u\n", all_entries[k].name, all_entries[k].inode_num);
        } else {
            printf("%s\n", all_entries[k].name);
        }

        // Check if the entry is a directory to recurse.
        Inode entry_inode;
        if (read_inode(all_entries[k].inode_num, &entry_inode) == 0) {
            if (entry_inode.is_directory) {
                // Recursively call list_recursive for the subdirectory.
                list_recursive(all_entries[k].inode_num, depth + 1, debug_mode);
            } else if (debug_mode) {
                 // If debug mode is on and it's a file, print its size.
                 for (int indent = 0; indent < depth + 1; ++indent) printf("  ");
                 printf("  (file, size %zu)\n", entry_inode.size);
            }
        } else {
             // Error reading the inode for the entry.
             fprintf(stderr, "Warning: Could not read inode %u for entry '%s' during list.\n", all_entries[k].inode_num, all_entries[k].name);
        }
    }

    // --- Step 4: Clean up the dynamically allocated memory ---
    free(all_entries);
}


/**
 * @brief Main function to handle the list operation (parses path, calls recursive list).
 * @param path The absolute path in ExFS2 to list (e.g., "/").
 * @param debug_mode Flag for enabling debug output.
 */
void list_fs(const char *path, int debug_mode) {
    uint32_t parent_inode_num = UINT32_MAX; // Parent inode (not strictly needed here).
    uint32_t target_inode_num = UINT32_MAX; // Inode of the directory to list.
    char target_name[MAX_FILENAME_LEN + 1]; // Name of the target (not strictly needed here).
    uint32_t start_inode_num = 0; // Default to root directory (inode 0).

    // If the path is not the root directory "/", parse it.
    if (strcmp(path, "/") != 0) {
        // Parse the path to find the inode of the target directory.
        if (parse_path(path, &parent_inode_num, &target_inode_num, target_name) != 0) {
            // Error during parsing (message printed by parse_path).
            return;
        }
        // Check if the target path was found.
        if (target_inode_num == UINT32_MAX) {
             fprintf(stderr, "Error: Path '%s' not found.\n", path);
             return;
        }
        // Read the target inode to verify it's a directory.
        Inode target_inode;
        if (read_inode(target_inode_num, &target_inode) != 0 || !target_inode.is_directory) {
             fprintf(stderr, "Error: Path '%s' is not a directory.\n", path);
             return;
        }
        // Set the starting inode number for listing to the target directory's inode.
        start_inode_num = target_inode_num;
    }

    // Call the recursive listing function starting from the determined inode at depth 0.
    list_recursive(start_inode_num, 0, debug_mode);
}


/**
 * @brief Adds a file from the local host filesystem to the ExFS2 filesystem.
 * Creates intermediate directories in ExFS2 path if they don't exist.
 * @param exfs_path The absolute destination path within ExFS2.
 * @param local_path The path of the source file on the local host system.
 */
void add_file_to_fs(const char *exfs_path, const char *local_path) {
    // 1. Open the local source file for reading in binary mode.
    FILE *local_fp = fopen(local_path, "rb");
    if (!local_fp) {
        perror("Failed to open local file for reading");
        return;
    }

    // 2. Parse the ExFS2 destination path. Create intermediate directories if needed.
    uint32_t parent_inode_num = 0; // Tracks the inode of the parent directory during traversal.
    uint32_t target_inode_num = UINT32_MAX; // Will hold inode of target if it exists (shouldn't for add).
    char target_name[MAX_FILENAME_LEN + 1]; // Buffer for the final filename component.
    target_name[0] = '\0'; // Initialize buffer.
    uint32_t current_dir_inode = 0; // Start traversal at the root directory (inode 0).

    // Create a mutable copy of the ExFS2 path for strtok_r.
    char exfs_path_copy[PATH_MAX];
    strncpy(exfs_path_copy, exfs_path, PATH_MAX - 1);
    exfs_path_copy[PATH_MAX - 1] = '\0';

    char *token; // Current path component.
    char *rest = exfs_path_copy; // Pointer for strtok_r state.
    if (rest[0] == '/') rest++; // Skip leading '/'.

    // Get the first path component.
    char *next_token = strtok_r(rest, "/", &rest);
    // Handle edge cases: adding "/" or empty filename.
    if (next_token == NULL && strcmp(exfs_path,"/") == 0) {
        fprintf(stderr, "Error: Cannot add a file named '/'.\n");
        fclose(local_fp);
        return;
    }
    if (next_token == NULL) {
        fprintf(stderr, "Error: Invalid target filename in path '%s'.\n", exfs_path);
        fclose(local_fp);
        return;
    }

    // Loop through path components.
    while (next_token != NULL) {
        char *current_token = next_token; // Process this token.
        next_token = strtok_r(rest, "/", &rest); // Look ahead to see if this is the last component.

        uint32_t found_inode;
        // Check if the current component exists in the current directory.
        if (find_entry_in_dir(current_dir_inode, current_token, &found_inode) != 0) {
            // Component not found.
            if (next_token != NULL) {
                // If it's an intermediate component, create the directory.
                 uint32_t new_dir = create_directory(current_dir_inode, current_token);
                 if (new_dir == UINT32_MAX) {
                     fprintf(stderr, "Failed to create intermediate directory '%s'\n", current_token);
                     fclose(local_fp);
                     return; // Stop if directory creation fails.
                 }
                 current_dir_inode = new_dir; // Move into the newly created directory.
            } else {
                // If it's the last component (the filename), store its name and parent inode.
                strncpy(target_name, current_token, MAX_FILENAME_LEN);
                target_name[MAX_FILENAME_LEN]='\0';
                parent_inode_num = current_dir_inode;
                target_inode_num = UINT32_MAX; // Mark as not existing yet.
                break; // Exit loop, ready to create the file.
            }
        } else {
            // Component found. Read its inode.
             Inode temp_inode;
             if(read_inode(found_inode, &temp_inode) != 0) {
                 fprintf(stderr, "Error reading inode %u for '%s'\n", found_inode, current_token);
                 fclose(local_fp);
                 return;
             }

            if (next_token != NULL) {
                // If it's an intermediate component, it must be a directory.
                 if (!temp_inode.is_directory) {
                     fprintf(stderr, "Error: '%s' in path '%s' exists but is not a directory.\n", current_token, exfs_path);
                     fclose(local_fp);
                     return;
                 }
                 current_dir_inode = found_inode; // Move into the existing directory.
            } else {
                // If it's the last component, it means the target file/dir already exists. Error out.
                strncpy(target_name, current_token, MAX_FILENAME_LEN);
                 target_name[MAX_FILENAME_LEN]='\0';
                parent_inode_num = current_dir_inode;
                 target_inode_num = found_inode; // Store the existing inode number.
                 fprintf(stderr, "Error: '%s' already exists at path '%s'. Remove it first.\n", target_name, exfs_path);
                 fclose(local_fp);
                 return;
            }
        }
    } // End path parsing loop.

     // Final checks after parsing.
     if (target_name[0] == '\0') { // Ensure filename was extracted.
         fprintf(stderr, "Error: Could not determine target filename from path '%s'.\n", exfs_path);
         fclose(local_fp);
         return;
     }
     if (target_inode_num != UINT32_MAX) { // Double-check that target doesn't exist.
          fprintf(stderr, "Error: Target '%s' already exists.\n", exfs_path);
          fclose(local_fp);
          return;
     }


    // 3. Allocate an inode for the new file.
    uint32_t new_file_inode_num = find_free_inode();
    if (new_file_inode_num == UINT32_MAX) {
        fprintf(stderr, "Failed to allocate inode for file '%s'\n", target_name);
        fclose(local_fp);
        return;
    }

    // 4. Initialize the new file's Inode structure.
    Inode new_file_inode;
    memset(&new_file_inode, 0, sizeof(Inode)); // Zero out the structure.
    new_file_inode.is_directory = 0; // Mark as a regular file.
    new_file_inode.size = 0; // Initial size is 0.
    new_file_inode.mode = S_IFREG | 0644; // Set type to file and default permissions.

    // 5. Write the initial inode (size 0) before writing data blocks.
    // This is helpful if get_block_for_offset needs to read the inode during allocation.
    if (write_inode(new_file_inode_num, &new_file_inode) != 0) {
         fprintf(stderr, "Failed to write initial inode %u for file '%s'\n", new_file_inode_num, target_name);
         free_inode(new_file_inode_num); // Rollback: free the allocated inode.
         fclose(local_fp);
         return;
    }

    // 6. Read data from the local file and write it to ExFS2 data blocks.
    char buffer[BLOCK_SIZE]; // Buffer for reading/writing blocks.
    size_t bytes_read; // Number of bytes read from local file in one iteration.
    size_t total_bytes_written = 0; // Track total bytes written to ExFS2 file.
    int error_occurred = 0; // Flag to indicate errors during the write loop.

    // Loop while reading data from the local file.
    while ((bytes_read = fread(buffer, 1, BLOCK_SIZE, local_fp)) > 0) {
        // Get the ExFS2 data block number for the current file offset.
        // Request allocation (allocate=1) if the block doesn't exist.
        uint32_t target_block = get_block_for_offset(new_file_inode_num, total_bytes_written, 1);
        if (target_block == UINT32_MAX) {
            // Failed to get/allocate a block.
            fprintf(stderr, "Failed to allocate/get data block for file '%s' at offset %zu\n", target_name, total_bytes_written);
            error_occurred = 1;
            break; // Exit the write loop.
        }

        // If this is the last block (bytes_read < BLOCK_SIZE), zero out the rest of the buffer.
        if (bytes_read < BLOCK_SIZE) {
            memset(buffer + bytes_read, 0, BLOCK_SIZE - bytes_read);
        }

        // Write the buffer content to the target ExFS2 data block.
        if (write_data_block(target_block, buffer) != 0) {
             fprintf(stderr, "Failed to write data block %u for file '%s'\n", target_block, target_name);
             error_occurred = 1;
             break; // Exit the write loop.
        }
        // Update the total bytes written.
        total_bytes_written += bytes_read;
    } // End read/write loop.

    // Check for read errors on the local file after the loop.
    if (ferror(local_fp)) {
        perror("Error reading from local file");
        error_occurred = 1;
    }
    fclose(local_fp); // Close the local file.

    // 7. Update the file inode with the final size.
    if (!error_occurred) {
        // Re-read the inode, as get_block_for_offset might have modified it (e.g., added indirect pointers).
        if (read_inode(new_file_inode_num, &new_file_inode) != 0) {
             fprintf(stderr, "Failed to re-read inode %u before final size update for '%s'\n", new_file_inode_num, target_name);
             error_occurred = 1;
        } else {
            // Set the final size in the inode structure.
            new_file_inode.size = total_bytes_written;
            // Write the updated inode back to the inode segment.
            if (write_inode(new_file_inode_num, &new_file_inode) != 0) {
                fprintf(stderr, "Failed to write final inode %u for file '%s'\n", new_file_inode_num, target_name);
                error_occurred = 1; // Critical error: data written, but size incorrect.
            }
        }
    }

    // 8. Add the entry for the new file to its parent directory.
    if (!error_occurred) {
        if (add_entry_to_dir(parent_inode_num, target_name, new_file_inode_num) != 0) {
             fprintf(stderr, "Failed to add entry for file '%s' to parent directory %u\n", target_name, parent_inode_num);
             error_occurred = 1; // Critical error: file created but not linked in directory (orphaned).
        }
    }

    // 9. Cleanup if any error occurred during the process.
    if (error_occurred) {
        fprintf(stderr, "An error occurred during add operation for '%s'. Attempting cleanup...\n", exfs_path);
        // Read the potentially partially written inode to find allocated blocks.
        Inode cleanup_inode;
         if (read_inode(new_file_inode_num, &cleanup_inode) == 0) {
            // Free all data blocks associated with the inode (direct and indirect).
            for(int i=0; i<MAX_DIRECT_POINTERS; ++i) free_data_block(cleanup_inode.direct_blocks[i]);
            free_indirect_blocks(cleanup_inode.single_indirect, 0); // Free single indirect level
            free_indirect_blocks(cleanup_inode.double_indirect, 1); // Free double indirect level
         } else {
              fprintf(stderr, "Cleanup warning: Could not read inode %u to free its blocks.\n", new_file_inode_num);
         }
        // Free the inode number itself.
        free_inode(new_file_inode_num);
        fprintf(stderr, "Cleanup attempted for failed add of '%s'. Filesystem might be inconsistent.\n", exfs_path);
    }
    // No explicit success message here.
}


/**
 * @brief Recursive helper function to remove files and directories.
 * Frees data blocks, removes entry from parent, frees inode.
 * @param inode_num Inode number of the item to remove.
 * @param parent_inode_num Inode number of the parent directory.
 * @param name Name of the item being removed.
 */
void remove_recursive(uint32_t inode_num, uint32_t parent_inode_num, const char* name) {
     // Read the inode of the item to be removed.
     Inode current_inode;
     if (read_inode(inode_num, &current_inode) != 0) {
         fprintf(stderr, "remove_recursive: Failed to read inode %u for '%s'\n", inode_num, name ? name : "(unknown)");
         return; // Cannot proceed without inode info.
     }

     // Check if it's a directory.
     if (current_inode.is_directory) {
         // --- Directory Removal ---
         // Buffer for reading directory blocks.
         char block_buffer[BLOCK_SIZE];
         DirectoryEntry *entries = (DirectoryEntry *)block_buffer; // Cast buffer.
         size_t entries_processed = 0; // Count valid entries found.
         size_t total_entries = current_inode.size; // Expected number of entries.

         // --- Iterate Direct Blocks ---
         for (int i = 0; i < MAX_DIRECT_POINTERS && entries_processed < total_entries; ++i) {
             uint32_t block_num = current_inode.direct_blocks[i];
             if (block_num == 0 || block_num == UINT32_MAX) continue; // Skip unused pointers.

             if (read_data_block(block_num, block_buffer) != 0) {
                 fprintf(stderr, "Warning: Failed read dir block %u during recursive remove of inode %u\n", block_num, inode_num);
                 continue; // Skip block on error.
             }

             for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                 if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) { // If valid entry
                     entries_processed++; // Count valid entry.
                     // Skip "." and ".." entries for recursion.
                     if (strcmp(entries[j].name, ".") != 0 && strcmp(entries[j].name, "..") != 0) {
                         // Recursively call remove_recursive for the entry found within this directory.
                         remove_recursive(entries[j].inode_num, inode_num, entries[j].name);
                     }
                     if (entries_processed >= total_entries) goto dir_scan_done_remove; // Optimization
                 }
             }
         }
         // --- Iterate Single Indirect Block ---
         if (current_inode.single_indirect != 0 && current_inode.single_indirect != UINT32_MAX && entries_processed < total_entries) {
             uint32_t pointers[POINTERS_PER_BLOCK];
             char indirect_block_buffer[BLOCK_SIZE];
             if(read_data_block(current_inode.single_indirect, indirect_block_buffer) == 0) {
                 memcpy(pointers, indirect_block_buffer, BLOCK_SIZE);
                 for(int i = 0; i < POINTERS_PER_BLOCK && entries_processed < total_entries; ++i) {
                     uint32_t block_num = pointers[i];
                     if (block_num == 0 || block_num == UINT32_MAX) continue;
                     if(read_data_block(block_num, block_buffer) == 0) {
                         for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                             if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                                 entries_processed++;
                                 if (strcmp(entries[j].name, ".") != 0 && strcmp(entries[j].name, "..") != 0) {
                                     remove_recursive(entries[j].inode_num, inode_num, entries[j].name);
                                 }
                                 if (entries_processed >= total_entries) goto dir_scan_done_remove;
                             }
                         }
                     } else { fprintf(stderr, "Warning: Failed read dir block %u (single indirect) during recursive remove\n", block_num); }
                 }
             } else { fprintf(stderr, "Warning: Failed read single indirect block %u during recursive remove\n", current_inode.single_indirect); }
         }
         // --- Iterate Double Indirect Block ---
          if (current_inode.double_indirect != 0 && current_inode.double_indirect != UINT32_MAX && entries_processed < total_entries) {
             uint32_t pointers1[POINTERS_PER_BLOCK];
             char block_buffer1[BLOCK_SIZE];
             if(read_data_block(current_inode.double_indirect, block_buffer1) == 0) {
                 memcpy(pointers1, block_buffer1, BLOCK_SIZE);
                 for(int idx1 = 0; idx1 < POINTERS_PER_BLOCK && entries_processed < total_entries; ++idx1) {
                     uint32_t single_indirect_block_num = pointers1[idx1];
                     if (single_indirect_block_num == 0 || single_indirect_block_num == UINT32_MAX) continue;

                     uint32_t pointers2[POINTERS_PER_BLOCK];
                     char block_buffer2[BLOCK_SIZE];
                     if(read_data_block(single_indirect_block_num, block_buffer2) == 0) {
                         memcpy(pointers2, block_buffer2, BLOCK_SIZE);
                         for(int i = 0; i < POINTERS_PER_BLOCK && entries_processed < total_entries; ++i) {
                             uint32_t block_num = pointers2[i];
                             if (block_num == 0 || block_num == UINT32_MAX) continue;
                             if(read_data_block(block_num, block_buffer) == 0) {
                                 for (int j = 0; j < DIRENTRIES_PER_BLOCK; ++j) {
                                     if (entries[j].inode_num != 0 && entries[j].inode_num != UINT32_MAX) {
                                         entries_processed++;
                                         if (strcmp(entries[j].name, ".") != 0 && strcmp(entries[j].name, "..") != 0) {
                                             remove_recursive(entries[j].inode_num, inode_num, entries[j].name);
                                         }
                                         if (entries_processed >= total_entries) goto dir_scan_done_remove;
                                     }
                                 }
                             } else { fprintf(stderr, "Warning: Failed read dir block %u (double indirect) during recursive remove\n", block_num); }
                         }
                     } else { fprintf(stderr, "Warning: Failed read single indirect block %u (from double) during recursive remove\n", single_indirect_block_num); }
                 }
             } else { fprintf(stderr, "Warning: Failed read double indirect block %u during recursive remove\n", current_inode.double_indirect); }
         }

dir_scan_done_remove: // Label for goto.

          // After removing all contents, free the directory's own data blocks.
          for(int i=0; i<MAX_DIRECT_POINTERS; ++i) free_data_block(current_inode.direct_blocks[i]);
          free_indirect_blocks(current_inode.single_indirect, 0); // Free single indirect level.
          free_indirect_blocks(current_inode.double_indirect, 1); // Free double indirect level.

     } else {
          // --- File Removal ---
          // Free the file's data blocks (direct and indirect).
          for(int i=0; i<MAX_DIRECT_POINTERS; ++i) free_data_block(current_inode.direct_blocks[i]);
          free_indirect_blocks(current_inode.single_indirect, 0); // Free single indirect level.
          free_indirect_blocks(current_inode.double_indirect, 1); // Free double indirect level.
     }

     // --- Common Removal Steps (File or Directory) ---
     // Remove the entry for this item from its parent directory.
     // Check if parent inode and name are valid (don't try to remove "/" from root's parent).
     if (parent_inode_num != UINT32_MAX && name != NULL && strcmp(name, "/") != 0 ) {
         // Call remove_entry_from_dir to mark the slot in the parent as free.
         if(remove_entry_from_dir(parent_inode_num, name) != 0){
             // Warning if removal from parent fails (might leave dangling entry).
             fprintf(stderr, "Warning: Failed to remove entry '%s' from parent %u during cleanup.\n", name, parent_inode_num);
         }
     }

     // Finally, free the inode number itself.
     free_inode(inode_num);
}


/**
 * @brief Main function to handle the remove operation.
 * Parses path and calls the recursive remove helper.
 * @param exfs_path Absolute path of the file or directory to remove.
 */
void remove_from_fs(const char *exfs_path) {
    uint32_t parent_inode_num = UINT32_MAX; // To store parent inode.
    uint32_t target_inode_num = UINT32_MAX; // To store target inode.
    char target_name[MAX_FILENAME_LEN + 1]; // To store target name.

    // Prevent removing the root directory.
    if (strcmp(exfs_path, "/") == 0) {
        fprintf(stderr, "Error: Cannot remove root directory '/'.\n");
        return;
    }

    // Parse the path to find the target inode and its parent.
    if (parse_path(exfs_path, &parent_inode_num, &target_inode_num, target_name) != 0) {
        // Error during parsing. Check if it was simply "not found".
        if (target_inode_num == UINT32_MAX) {
             fprintf(stderr, "Error: Path '%s' not found.\n", exfs_path);
        } // Otherwise, parse_path printed a more specific error.
        return;
    }

    // Check again if the target was found after successful parsing.
    if (target_inode_num == UINT32_MAX) {
        fprintf(stderr, "Error: '%s' not found.\n", exfs_path);
        return;
    }

    // Call the recursive removal function to delete the target and its contents (if directory).
    // Pass the target inode, its parent inode, and its name.
    remove_recursive(target_inode_num, parent_inode_num, target_name);
}

/**
 * @brief Extracts the content of a file from ExFS2 to standard output.
 * @param exfs_path Absolute path of the file to extract.
 */
void extract_file_from_fs(const char *exfs_path) {
    uint32_t parent_inode_num = UINT32_MAX; // To store parent inode.
    uint32_t target_inode_num = UINT32_MAX; // To store target inode.
    char target_name[MAX_FILENAME_LEN + 1]; // To store target name.

    // Parse the path to find the target file's inode.
    if (parse_path(exfs_path, &parent_inode_num, &target_inode_num, target_name) != 0) {
        // Error during parsing. Check if it was "not found".
        if (target_inode_num == UINT32_MAX) {
             fprintf(stderr, "Error: Path '%s' not found.\n", exfs_path);
        }
        return; // Exit on parsing error.
    }

    // Check if the target was actually found.
    if (target_inode_num == UINT32_MAX) {
        fprintf(stderr, "Error: File '%s' not found.\n", exfs_path);
        return;
    }

    // Read the target inode.
    Inode file_inode;
    if (read_inode(target_inode_num, &file_inode) != 0) {
        fprintf(stderr, "Error reading inode %u for extraction.\n", target_inode_num);
        return;
    }

    // Check if the target is a directory; cannot extract directories.
    if (file_inode.is_directory) {
        fprintf(stderr, "Error: '%s' is a directory. Please specify a regular file for extraction.\n", exfs_path);
        return;
    }

    // --- Read file data block by block and write to stdout ---
    char buffer[BLOCK_SIZE]; // Buffer for reading data blocks.
    size_t bytes_remaining = file_inode.size; // Total bytes to read based on inode size.
    size_t current_offset = 0; // Current byte offset within the file.

    // Loop while there are bytes remaining to be read.
    while (bytes_remaining > 0) {
        // Get the data block number for the current offset. Do not allocate (allocate=0).
        uint32_t block_num = get_block_for_offset(target_inode_num, current_offset, 0);
        if (block_num == UINT32_MAX) {
            // Failed to find the block for this offset (file might be corrupt).
            fprintf(stderr, "Error: Could not find data block for file '%s' at offset %zu. File possibly corrupt.\n", exfs_path, current_offset);
            return;
        }

        // Read the data block content into the buffer.
        if (read_data_block(block_num, buffer) != 0) {
             fprintf(stderr, "Error reading data block %u for file '%s'.\n", block_num, exfs_path);
             return;
        }

        // Determine how many bytes to write from this block (might be less than BLOCK_SIZE for the last block).
        size_t bytes_to_write = (bytes_remaining < BLOCK_SIZE) ? bytes_remaining : BLOCK_SIZE;
        // Write the relevant portion of the buffer to standard output.
        if (fwrite(buffer, 1, bytes_to_write, stdout) != bytes_to_write) {
             // Check if the write failed due to an actual error or just a closed pipe.
             if (ferror(stdout)) {
                 perror("Error writing file content to stdout");
             }
             // Stop writing if fwrite fails (e.g., pipe closed).
             clearerr(stdout); // Clear error state for stdout.
             return;
        }

        // Update remaining bytes and current offset.
        bytes_remaining -= bytes_to_write;
        current_offset += bytes_to_write;
    }
    // Flush stdout buffer to ensure all data is written.
    fflush(stdout);
}


// --- Initialization ---

/**
 * @brief Initializes the ExFS2 filesystem environment.
 * Checks for existing segment files (segment 0). If found, scans for the highest
 * existing segment indices to set global counters. If not found, creates
 * initial inode and data segments (index 0) and initializes the root directory.
 */
void init_exfs2() {
     // Attempt to open segment 0 of both types in read-only mode to check existence.
     FILE *fp_i0 = open_segment(INODE_SEGMENT_PREFIX, 0, "rb");
     FILE *fp_d0 = open_segment(DATA_SEGMENT_PREFIX, 0, "rb");

     // If both segment 0 files exist, assume filesystem is already initialized.
     if (fp_i0 && fp_d0) {
         fclose(fp_i0); // Close the opened files.
         fclose(fp_d0);

         // Scan for the highest existing inode segment index.
         int i = 0;
         char fname[100];
         while (1) {
             get_segment_filename(INODE_SEGMENT_PREFIX, i, fname, sizeof(fname));
             // Use access() to check if the file exists without opening it.
             if (access(fname, F_OK) == 0) {
                 i++; // Increment index if file exists.
             } else {
                 break; // Stop when a segment file is not found.
             }
         }
         // Set the global index to the first index that was *not* found.
         next_inode_segment_idx = i;

         // Scan for the highest existing data segment index (similar logic).
         i = 0;
          while (1) {
             get_segment_filename(DATA_SEGMENT_PREFIX, i, fname, sizeof(fname));
             if (access(fname, F_OK) == 0) {
                 i++;
             } else {
                 break;
             }
         }
         next_data_segment_idx = i;

     } else {
         // --- Filesystem Not Found - Initialize ---
         // Close any file that might have been opened if only one existed.
         if (fp_i0) fclose(fp_i0);
         if (fp_d0) fclose(fp_d0);

         // Create initial inode segment (index 0).
         if (create_inode_segment(0) != 0) {
             fprintf(stderr, "Fatal: Could not create initial inode segment.\n");
             exit(EXIT_FAILURE); // Exit if critical initialization fails.
         }
         // Create initial data segment (index 0).
         if (create_data_segment(0) != 0) {
             fprintf(stderr, "Fatal: Could not create initial data segment.\n");
             // Attempt cleanup: remove the inode segment that was created.
             remove(INODE_SEGMENT_PREFIX "0" SEGMENT_SUFFIX);
             exit(EXIT_FAILURE); // Exit.
         }

         // --- Initialize Root Directory ---
         // Allocate the first inode (should be inode 0).
         uint32_t root_inode_num = find_free_inode();
         if (root_inode_num != 0) {
              // This indicates a logic error in find_free_inode or segment creation.
              fprintf(stderr, "Fatal: First allocated inode is not 0! (%u)\n", root_inode_num);
              exit(EXIT_FAILURE);
         }
         // Allocate the first data block (should be block 0).
         uint32_t root_data_block = find_free_data_block();
         if (root_data_block != 0) {
              // This indicates a logic error.
              fprintf(stderr, "Fatal: First allocated data block is not 0! (%u)\n", root_data_block);
              exit(EXIT_FAILURE);
         }

         // Create and initialize the root Inode structure.
         Inode root_inode;
         memset(&root_inode, 0, sizeof(Inode));
         root_inode.is_directory = 1;
         root_inode.mode = S_IFDIR | 0755;
         root_inode.size = 2; // For "." and "..".
         root_inode.direct_blocks[0] = root_data_block; // Point to the allocated data block.
         // Write the root inode to segment 0.
         if (write_inode(root_inode_num, &root_inode) != 0) {
             fprintf(stderr, "Fatal: Failed to write root inode.\n");
             exit(EXIT_FAILURE);
         }

         // Create and initialize the root directory's data block content.
         char root_block_buffer[BLOCK_SIZE];
         DirectoryEntry *entries = (DirectoryEntry *)root_block_buffer; // Cast buffer.
         memset(root_block_buffer, 0, BLOCK_SIZE); // Zero out block.
         // Create "." entry pointing to root inode (0).
         strncpy(entries[0].name, ".", MAX_FILENAME_LEN);
         entries[0].inode_num = root_inode_num;
         // Create ".." entry pointing to root inode (0).
         strncpy(entries[1].name, "..", MAX_FILENAME_LEN);
         entries[1].inode_num = root_inode_num;
         // Write the root data block to segment 0.
         if (write_data_block(root_data_block, root_block_buffer) != 0) {
              fprintf(stderr, "Fatal: Failed to write root data block.\n");
              exit(EXIT_FAILURE);
         }
     }
}


// --- Main Function ---

/**
 * @brief Main entry point for the exfs2 command-line tool.
 * Parses command-line arguments and calls the appropriate filesystem operation function.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int main(int argc, char *argv[]) {
    int opt; // Variable to store the option character returned by getopt.
    char *operation = NULL; // String to store the requested operation (list, add, etc.).
    char *exfs_path = NULL; // String for the path within the ExFS2 filesystem.
    char *local_path = NULL; // String for the path on the local host filesystem (for add).
    int debug_mode = 0; // Flag for debug listing mode.

    // --- Argument Parsing ---
    // Use getopt to parse command-line options according to the specified format string.
    // Options: -l (list), -a <path> (add), -r <path> (remove), -e <path> (extract),
    //          -D <path> (debug list), -f <path> (local file for add).
    // Colons indicate options that require an argument (optarg).
    while ((opt = getopt(argc, argv, "la:r:e:D:f:")) != -1) {
        switch (opt) {
            case 'l': // List operation
                // Ensure no other operation was already specified.
                if (operation) { fprintf(stderr, "Error: Multiple operations specified (-l and -%c).\n", operation[0]); return EXIT_FAILURE; }
                operation = "list";
                // Default path for list is root "/" if not overridden by -D.
                if (!exfs_path) exfs_path = "/";
                break;
            case 'a': // Add operation
                if (operation) { fprintf(stderr, "Error: Multiple operations specified (-a and -%c).\n", operation[0]); return EXIT_FAILURE; }
                operation = "add";
                exfs_path = optarg; // Store the ExFS2 path argument.
                 break;
            case 'r': // Remove operation
                 if (operation) { fprintf(stderr, "Error: Multiple operations specified (-r and -%c).\n", operation[0]); return EXIT_FAILURE; }
                 operation = "remove";
                 exfs_path = optarg; // Store the ExFS2 path argument.
                 break;
            case 'e': // Extract operation
                 if (operation) { fprintf(stderr, "Error: Multiple operations specified (-e and -%c).\n", operation[0]); return EXIT_FAILURE; }
                 operation = "extract";
                 exfs_path = optarg; // Store the ExFS2 path argument.
                 break;
             case 'D': // Debug list operation
                 debug_mode = 1; // Set the debug flag.
                 exfs_path = optarg; // Store the ExFS2 path argument.
                 // If no operation was set before, default to "list".
                 if (!operation) {
                     operation = "list";
                 } else if (strcmp(operation, "list") != 0) {
                     // If another operation was already set, -D is ignored for operation type.
                     fprintf(stderr, "Warning: -D option ignored when combined with operation other than -l.\n");
                 }
                 break;
            case 'f': // Local file path (used with -a)
                 // Ensure the operation is already set to "add".
                 if (!operation || strcmp(operation, "add") != 0) {
                      fprintf(stderr, "Error: -f option requires -a operation.\n");
                      return EXIT_FAILURE;
                 }
                 local_path = optarg; // Store the local file path argument.
                 break;
            case '?': // Handle unknown options or missing arguments detected by getopt.
                 // getopt usually prints its own error message. Print usage.
                 fprintf(stderr, "Usage: %s [-l | -D /exfs/path | -a /exfs/path -f /local/path | -r /exfs/path | -e /exfs/path]\n", argv[0]);
                return EXIT_FAILURE;
            default:
                // Should not happen with the defined options string.
                abort();
        }
    }

    // Check for any remaining non-option arguments (should be none).
    if (optind < argc) {
        fprintf(stderr, "Error: Unexpected non-option argument: %s\n", argv[optind]);
         fprintf(stderr, "Usage: %s [-l | -D /exfs/path | -a /exfs/path -f /local/path | -r /exfs/path | -e /exfs/path]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // --- Validate Argument Combinations ---
    // Ensure an operation was specified.
    if (!operation) {
         fprintf(stderr, "No operation specified.\nUsage: %s [-l | -D /exfs/path | -a /exfs/path -f /local/path | -r /exfs/path | -e /exfs/path]\n", argv[0]);
         return EXIT_FAILURE;
    }
    // Ensure 'add' operation has both exfs_path (-a) and local_path (-f).
     if (strcmp(operation, "add") == 0 && (!exfs_path || !local_path)) {
         fprintf(stderr, "Error: -a operation requires both an exfs path (-a) and a local file path (-f).\n");
         return EXIT_FAILURE;
     }
    // Ensure list, remove, extract operations have an exfs_path (might be defaulted for list).
     if ((strcmp(operation, "list") == 0 || strcmp(operation, "remove") == 0 || strcmp(operation, "extract") == 0) && !exfs_path) {
          fprintf(stderr, "Error: -%c operation requires an exfs path.\n", operation[0]);
          return EXIT_FAILURE;
     }


    // --- Initialize Filesystem ---
    // Checks for existing segments or creates new ones and the root directory.
    init_exfs2();

    // --- Execute Operation ---
    // Call the appropriate function based on the parsed operation string.
    if (strcmp(operation, "list") == 0) {
        list_fs(exfs_path, debug_mode); // Pass debug flag.
    } else if (strcmp(operation, "add") == 0) {
        add_file_to_fs(exfs_path, local_path);
    } else if (strcmp(operation, "remove") == 0) {
        remove_from_fs(exfs_path);
    } else if (strcmp(operation, "extract") == 0) {
        extract_file_from_fs(exfs_path);
    } else {
        // Should not be reachable due to earlier validation.
        fprintf(stderr, "Internal error: Unknown operation '%s'\n", operation);
        return EXIT_FAILURE;
    }

    // Return success if the operation completed without fatal errors.
    return EXIT_SUCCESS;
}
