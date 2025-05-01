#ifndef EXFS2_H // Start of include guard to prevent multiple inclusions
#define EXFS2_H // Define the guard macro

// --- Standard Library Includes ---
#include <stdio.h>      // For standard I/O functions (FILE, fopen, printf, etc.)
#include <stdlib.h>     // For general utility functions (malloc, exit, etc.)
#include <string.h>     // For string manipulation functions (strcpy, strcmp, memset, etc.)
#include <stdint.h>     // For fixed-width integer types (uint32_t, uint8_t)
#include <unistd.h>     // For POSIX operating system API (getopt, access, ftruncate, fileno)
#include <sys/stat.h>   // For file status constants and functions (mode_t, S_IFDIR, S_IFREG)
#include <sys/types.h>  // For basic system data types (size_t, mode_t)
#include <math.h>       // For mathematical functions (e.g., ceil - though not strictly needed with current integer math)
#include <errno.h>      // For error number definitions (errno)
#include <limits.h>     // For implementation-defined constants (PATH_MAX, UINT32_MAX)
#include <assert.h>     // For static_assert macro (compile-time assertions)

// --- Filesystem Constants ---
#define SEGMENT_SIZE (1024 * 1024) // Define segment size as 1MB (1024 * 1024 bytes)
#define BLOCK_SIZE 4096            // Define the size of a data block (4KB)
#define MAX_FILENAME_LEN 251       // Define maximum length for filenames in directory entries
#define INODE_SIZE BLOCK_SIZE      // Define the size of an inode structure to be exactly one block

// --- Segment Filename Components ---
#define INODE_SEGMENT_PREFIX "inode_segment_" // Prefix for inode segment filenames
#define DATA_SEGMENT_PREFIX "data_segment_"   // Prefix for data segment filenames
#define SEGMENT_SUFFIX ".exfs"                // Suffix for all segment filenames

// --- Inode Size Calculation Macros ---
// These macros calculate the number of direct block pointers that can fit
// in an Inode struct after accounting for other fixed-size fields, ensuring
// the total struct size equals INODE_SIZE (BLOCK_SIZE).

// Helper struct to measure the size of fixed fields in the Inode struct.
typedef struct {
    uint8_t is_directory;       // Flag indicating if it's a directory
    mode_t mode;                // File type and permissions
    size_t size;                // Size of the file or number of directory entries
    uint32_t single_indirect;   // Block number of the single indirect block
    uint32_t double_indirect;   // Block number of the double indirect block
} InodeFixedPart;

// Calculate the actual memory size occupied by the fixed part, including padding.
#define INODE_FIXED_PART_ACTUAL_SIZE (sizeof(InodeFixedPart))

// Calculate the remaining space within the INODE_SIZE for direct pointers.
#define INODE_REMAINING_SPACE (INODE_SIZE - INODE_FIXED_PART_ACTUAL_SIZE)

// Calculate the maximum number of direct block pointers that fit in the remaining space.
#define MAX_DIRECT_POINTERS (INODE_REMAINING_SPACE / sizeof(uint32_t))

// Calculate the exact size occupied by the array of direct pointers.
#define DIRECT_POINTERS_ARRAY_SIZE (MAX_DIRECT_POINTERS * sizeof(uint32_t))

// Calculate the size of any final padding needed to make the struct exactly INODE_SIZE.
#define INODE_PADDING_SIZE (INODE_REMAINING_SPACE - DIRECT_POINTERS_ARRAY_SIZE)

// --- Data Structures ---

/**
 * @brief Structure representing an Inode in the ExFS2 filesystem.
 *
 * Stores metadata for a file or directory, including its type, size,
 * and pointers to the data blocks that hold its content. Designed to
 * fit exactly within one BLOCK_SIZE.
 */
typedef struct {
    // --- Metadata Fields ---
    uint8_t is_directory;       // Flag: 1 if this inode represents a directory, 0 for a file.
    mode_t mode;                // Stores file type (S_IFDIR/S_IFREG) and potentially permissions.
    size_t size;                // For files: size in bytes. For directories: number of valid DirectoryEntry structs.
    uint32_t single_indirect;   // Block number containing an array of direct block pointers. 0 if unused.
    uint32_t double_indirect;   // Block number containing an array of single indirect block numbers. 0 if unused.

    // --- Block Pointers ---
    // Array of direct block pointers. The size is calculated by MAX_DIRECT_POINTERS.
    uint32_t direct_blocks[MAX_DIRECT_POINTERS];

    // --- Padding ---
    // Ensures the total size of the struct matches INODE_SIZE (BLOCK_SIZE).
    // The size is calculated by INODE_PADDING_SIZE. May be zero bytes.
    char padding[INODE_PADDING_SIZE];
} Inode;

// Compile-time check to ensure the Inode struct size is exactly BLOCK_SIZE.
static_assert(sizeof(Inode) == BLOCK_SIZE, "Inode size does not match BLOCK_SIZE");


// --- Derived Constants ---
// Calculate the number of block pointers (uint32_t) that fit in a single block.
#define POINTERS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))

// --- Segment Layout Constants ---
// Define the maximum number of inodes that can be stored per inode segment.
// (This is an estimate; the exact number depends on bitmap size overhead).
#define MAX_INODES_PER_SEGMENT 255
// Calculate the size (in bytes) needed for the inode allocation bitmap.
// Uses ceiling division: (MAX_INODES_PER_SEGMENT + 7) / 8
#define INODE_BITMAP_SIZE ((MAX_INODES_PER_SEGMENT + 7) / 8)

// Define the maximum number of data blocks that can be stored per data segment.
// (Estimate, depends on bitmap size overhead).
#define MAX_BLOCKS_PER_DATA_SEGMENT 255
// Calculate the size (in bytes) needed for the data block allocation bitmap.
// Uses ceiling division: (MAX_BLOCKS_PER_DATA_SEGMENT + 7) / 8
#define DATA_BITMAP_SIZE ((MAX_BLOCKS_PER_DATA_SEGMENT + 7) / 8)
// Define the byte offset within a data segment where the actual data blocks begin (after the bitmap).
#define DATA_AREA_OFFSET (DATA_BITMAP_SIZE)

// --- Global Variable Declarations ---
// These variables track the index of the next segment file to be created.
// They are defined in exfs2.c.
extern int next_inode_segment_idx; // Index for the next inode segment
extern int next_data_segment_idx;  // Index for the next data segment

/**
 * @brief Structure representing a directory entry.
 *
 * Stores the name of a file or subdirectory and the inode number
 * that corresponds to it. These entries are stored within data blocks
 * allocated to directory inodes.
 */
typedef struct {
    char name[MAX_FILENAME_LEN + 1]; // Filename (+1 for null terminator).
    uint32_t inode_num;              // Inode number for this entry. 0 indicates an unused/deleted entry.
} DirectoryEntry;

// Calculate the size of a single directory entry struct.
#define DIRENTRY_SIZE sizeof(DirectoryEntry)
// Calculate how many directory entries fit within a single data block.
#define DIRENTRIES_PER_BLOCK (BLOCK_SIZE / DIRENTRY_SIZE)


// --- Function Prototypes ---
// Function declarations, providing the signature for functions defined in exfs2.c.

// --- Initialization ---
/**
 * @brief Initializes the ExFS2 filesystem.
 * Checks if segment files exist; if not, creates initial segments and root directory.
 * Scans existing segments to determine the next available segment indices.
 */
void init_exfs2();

// --- Bitmap Operations ---
/**
 * @brief Gets the value of a specific bit in a bitmap.
 * @param bitmap Pointer to the bitmap data.
 * @param index The zero-based index of the bit to retrieve.
 * @return 1 if the bit is set (used), 0 if the bit is clear (free).
 */
int get_bit(const uint8_t *bitmap, uint32_t index);

/**
 * @brief Sets a specific bit in a bitmap to 1 (marks as used).
 * @param bitmap Pointer to the bitmap data (will be modified).
 * @param index The zero-based index of the bit to set.
 */
void set_bit(uint8_t *bitmap, uint32_t index);

/**
 * @brief Clears a specific bit in a bitmap to 0 (marks as free).
 * @param bitmap Pointer to the bitmap data (will be modified).
 * @param index The zero-based index of the bit to clear.
 */
void clear_bit(uint8_t *bitmap, uint32_t index);

// --- Segment Management ---
/**
 * @brief Opens (or creates if necessary) a segment file.
 * @param prefix The segment type prefix (INODE_SEGMENT_PREFIX or DATA_SEGMENT_PREFIX).
 * @param index The numerical index of the segment.
 * @param mode The file access mode string (e.g., "rb", "rb+", "wb+").
 * @return A FILE pointer to the opened segment, or NULL on error. If opened with "rb+" and file doesn't exist, attempts creation with "wb+".
 */
FILE* open_segment(const char* prefix, int index, const char* mode);

/**
 * @brief Constructs the full filename for a segment file.
 * @param prefix The segment type prefix.
 * @param index The numerical index of the segment.
 * @param buffer Character buffer to store the resulting filename.
 * @param buffer_size Size of the provided buffer.
 */
void get_segment_filename(const char* prefix, int index, char* buffer, size_t buffer_size);

/**
 * @brief Creates and initializes a new inode segment file.
 * Creates the file, truncates it to SEGMENT_SIZE, and writes an empty bitmap.
 * Updates next_inode_segment_idx if a higher index is created.
 * @param index The numerical index of the inode segment to create.
 * @return 0 on success, -1 on error.
 */
int create_inode_segment(int index);

/**
 * @brief Creates and initializes a new data segment file.
 * Creates the file, truncates it to SEGMENT_SIZE, and writes an empty bitmap.
 * Updates next_data_segment_idx if a higher index is created.
 * @param index The numerical index of the data segment to create.
 * @return 0 on success, -1 on error.
 */
int create_data_segment(int index);

// --- Inode Operations ---
/**
 * @brief Finds the first available (free) inode number.
 * Scans inode bitmaps in existing segments. If none found, creates a new inode segment
 * and allocates the first inode (index 0) within that new segment. Marks the found inode as used in the bitmap.
 * @return The allocated inode number, or UINT32_MAX on error (e.g., failed segment creation).
 */
uint32_t find_free_inode();

/**
 * @brief Reads an inode structure from the appropriate inode segment file.
 * @param inode_num The inode number to read.
 * @param inode_buffer Pointer to an Inode struct where the data will be stored.
 * @return 0 on success, -1 on error (e.g., segment not found, read error). Returns 0 with a zeroed buffer on EOF.
 */
int read_inode(uint32_t inode_num, Inode *inode_buffer);

/**
 * @brief Writes an inode structure to the appropriate inode segment file.
 * @param inode_num The inode number to write.
 * @param inode_buffer Pointer to the Inode struct containing the data to write.
 * @return 0 on success, -1 on error (e.g., segment not found, write error).
 */
int write_inode(uint32_t inode_num, const Inode *inode_buffer);

/**
 * @brief Frees an inode number by clearing its bit in the inode bitmap.
 * Does NOT free the associated data blocks. Prevents freeing the root inode (inode 0).
 * @param inode_num The inode number to mark as free.
 */
void free_inode(uint32_t inode_num);

// --- Data Block Operations ---
/**
 * @brief Finds the first available (free) data block number.
 * Scans data bitmaps in existing segments. If none found, creates a new data segment
 * and allocates the first block (index 0) within that new segment. Marks the found block as used in the bitmap.
 * @return The allocated data block number, or UINT32_MAX on error (e.g., failed segment creation).
 */
uint32_t find_free_data_block();

/**
 * @brief Reads the contents of a data block from the appropriate data segment file.
 * @param block_num The data block number to read.
 * @param buffer Pointer to a buffer (at least BLOCK_SIZE bytes) where the data will be stored.
 * @return 0 on success, -1 on error. Returns 0 with potentially zero-padded buffer on EOF/short read.
 */
int read_data_block(uint32_t block_num, char *buffer);

/**
 * @brief Writes data to a specific data block in the appropriate data segment file.
 * @param block_num The data block number to write to.
 * @param buffer Pointer to a buffer (BLOCK_SIZE bytes) containing the data to write.
 * @return 0 on success, -1 on error.
 */
int write_data_block(uint32_t block_num, const char *buffer);

/**
 * @brief Frees a data block by clearing its bit in the data block bitmap.
 * @param block_num The data block number to mark as free. Handles UINT32_MAX gracefully.
 */
void free_data_block(uint32_t block_num);

/**
 * @brief Recursively frees data blocks pointed to by indirect blocks.
 * Used to free blocks associated with single (level 0) and double (level 1) indirect pointers.
 * Reads the indirect block, frees the blocks it points to (recursing if necessary), then frees the indirect block itself.
 * @param block_num The block number of the indirect block to start freeing from.
 * @param level The level of indirection (0 for single, 1 for double).
 */
void free_indirect_blocks(uint32_t block_num, int level);

// --- Path and Directory Operations ---
/**
 * @brief Parses an absolute ExFS2 path (e.g., "/a/b/c").
 * Traverses the directory structure starting from the root (inode 0).
 * @param path The absolute path string to parse.
 * @param parent_inode_num Output pointer to store the inode number of the parent directory (e.g., inode of "/a/b" for path "/a/b/c").
 * @param target_inode_num Output pointer to store the inode number of the final component (e.g., inode of "c"). Set to UINT32_MAX if the target doesn't exist.
 * @param target_name Output buffer to store the name of the final component (e.g., "c").
 * @return 0 on successful parsing (target existence indicated by target_inode_num), -1 on error (invalid path format, intermediate component not found or not a directory).
 */
int parse_path(const char *path, uint32_t *parent_inode_num, uint32_t *target_inode_num, char *target_name);

/**
 * @brief Searches for a specific entry within the data blocks of a directory inode.
 * Iterates through the directory's data blocks (direct pointers only in simplified version).
 * @param dir_inode_num The inode number of the directory to search within.
 * @param name The name of the entry to find.
 * @param entry_inode_num Output pointer to store the inode number of the found entry.
 * @return 0 if the entry is found (entry_inode_num is set), -1 if not found or if dir_inode_num is not a directory.
 */
int find_entry_in_dir(uint32_t dir_inode_num, const char *name, uint32_t *entry_inode_num);

/**
 * @brief Adds a new directory entry (name and inode number) to a directory.
 * Searches for an empty slot in the directory's existing data blocks.
 * If no empty slot is found, allocates a new data block for the directory, updates the directory's inode to point to it, and adds the entry there.
 * Increments the directory inode's size field.
 * @param dir_inode_num The inode number of the directory to add the entry to.
 * @param name The name for the new entry.
 * @param entry_inode_num The inode number that the new entry should point to.
 * @return 0 on success, -1 on error (e.g., directory inode invalid, filename too long, failed block allocation, directory full).
 */
int add_entry_to_dir(uint32_t dir_inode_num, const char *name, uint32_t entry_inode_num);

/**
 * @brief Removes a directory entry from a directory by marking its slot as unused.
 * Finds the entry by name within the directory's data blocks and sets its inode_num field to 0.
 * Does NOT currently decrement the directory inode's size field or free the data block if it becomes empty.
 * @param dir_inode_num The inode number of the directory to remove the entry from.
 * @param name The name of the entry to remove.
 * @return 0 on success, -1 if the entry is not found or an error occurs.
 */
int remove_entry_from_dir(uint32_t dir_inode_num, const char *name);

/**
 * @brief Creates a new, empty directory within the filesystem.
 * 1. Allocates a new inode for the directory.
 * 2. Allocates a new data block for the directory's contents.
 * 3. Initializes the new inode (marks as directory, sets size=2, points direct_blocks[0] to the new data block).
 * 4. Initializes the new data block with "." (pointing to itself) and ".." (pointing to parent) entries.
 * 5. Writes the new inode and data block to their respective segments.
 * 6. Adds an entry for the new directory to its parent directory.
 * @param parent_inode_num The inode number of the parent directory where the new directory will be created.
 * @param name The name for the new directory.
 * @return The inode number of the newly created directory, or UINT32_MAX on error.
 */
uint32_t create_directory(uint32_t parent_inode_num, const char *name);

// --- Core File System Operations ---
/**
 * @brief Lists the contents of a directory in the ExFS2 filesystem.
 * Calls list_recursive to perform the actual listing.
 * @param path The absolute path of the directory to list (e.g., "/", "/a/b").
 * @param debug_mode If 1, prints additional debug information (inode numbers, file sizes). If 0, prints only names.
 */
void list_fs(const char *path, int debug_mode);

/**
 * @brief Adds a file from the local filesystem into the ExFS2 filesystem.
 * Parses the target ExFS2 path, creating intermediate directories as needed.
 * Allocates an inode for the new file.
 * Reads the local file block by block, allocating data blocks in ExFS2 using get_block_for_offset and writing the data.
 * Updates the file inode's size.
 * Adds an entry for the new file to its parent directory in ExFS2.
 * Performs cleanup (freeing allocated blocks/inode) if an error occurs during the process.
 * @param exfs_path The absolute destination path within ExFS2 (e.g., "/a/b/file.txt").
 * @param local_path The path to the source file on the local filesystem.
 */
void add_file_to_fs(const char *exfs_path, const char *local_path);

/**
 * @brief Removes a file or directory (recursively) from the ExFS2 filesystem.
 * Parses the path to find the target inode. Calls remove_recursive to perform the deletion.
 * @param exfs_path The absolute path of the file or directory to remove.
 */
void remove_from_fs(const char *exfs_path);

/**
 * @brief Extracts the contents of a file from ExFS2 and writes it to standard output (stdout).
 * Parses the path to find the file's inode.
 * Reads the file's data blocks sequentially (using get_block_for_offset) and writes the content to stdout.
 * Handles partial final blocks correctly based on the inode's size.
 * @param exfs_path The absolute path of the file to extract.
 */
void extract_file_from_fs(const char *exfs_path);

// --- Helper Functions ---
/**
 * @brief Recursively lists the contents of a directory.
 * Reads the directory's data blocks, collects valid entries (excluding "." and ".."), sorts them alphabetically,
 * prints each entry with appropriate indentation, and recurses into subdirectories.
 * @param dir_inode_num The inode number of the directory to list.
 * @param depth The current recursion depth (for indentation).
 * @param debug_mode If 1, includes inode numbers and file sizes in the output.
 */
void list_recursive(uint32_t dir_inode_num, int depth, int debug_mode);

/**
 * @brief Recursively removes a file or directory and its contents.
 * If it's a file, frees its data blocks (direct and indirect).
 * If it's a directory, recursively calls itself on all entries (except "." and ".."), then frees the directory's own data blocks.
 * Removes the entry for the file/directory from its parent directory.
 * Finally, frees the inode itself.
 * @param inode_num The inode number of the item to remove.
 * @param parent_inode_num The inode number of the parent directory (needed to remove the entry). UINT32_MAX if no parent context (shouldn't happen normally).
 * @param name The name of the item being removed (used for removing from parent and debug messages). NULL if unknown.
 */
void remove_recursive(uint32_t inode_num, uint32_t parent_inode_num, const char* name);

/**
 * @brief Gets the data block number corresponding to a specific byte offset within a file.
 * Handles direct, single indirect, and double indirect block pointers.
 * If allocate=1, it will find_free_data_block() and update the inode's pointers
 * (including intermediate indirect blocks) if the required block(s) don't exist yet.
 * Writes the modified inode back to disk if allocations occurred.
 * @param inode_num The inode number of the file.
 * @param offset The byte offset within the file.
 * @param allocate 1 to allocate blocks if they don't exist, 0 to only retrieve existing block numbers.
 * @return The data block number corresponding to the offset, or UINT32_MAX if the block doesn't exist (and allocate=0) or an error occurs (e.g., allocation failure, offset out of bounds).
 */
uint32_t get_block_for_offset(uint32_t inode_num, size_t offset, int allocate);

#endif // EXFS2_H // End of include guard
