\# ExFS2 File System Implementation

**Authors:** Jagdish Ghimire, Simran Basnet

**CS514 Spring '25 Assignment Specification**

High-Level Overview

ExFS2 (Extensible File System 2\) is a user-space file system implementation written in C. Unlike traditional file systems that reside on dedicated disk partitions, ExFS2 uses regular files within the host operating system's file system for storage. It achieves this by organizing its data and metadata into two types of fixed-size (1MB) files called "segments":

1\.  \*Inode Segments (inode\_segment\_.exfs): Store file system metadata, primarily inodes (which describe files and directories) and a bitmap tracking which inode slots are free or in use.  
2\.  Data Segments (dat\_segment\_.exfs): Store the actual content of files and the data representing directory entries. Storage is managed using 4KB blocks, and a bitmap tracks which blocks are free or in use.

The "Extensible" nature comes from its ability to create new segment files dynamically when the existing ones run out of space (either for inodes or data blocks). This allows the file system to grow as needed.

The project provides a command-line tool (./exfs2) to interact with the file system, allowing users to perform standard file system operations:  
 Add files from the host system into ExFS2.  
 List the directory structure within ExFS2.  
 Extract files from ExFS2 back to the host system (via standard output).  
 Remove files and directories from ExFS2.

Low-Level Overview

Segment Structure

 **Inode Segments:**  
     Starts with an **Inode Bitmap**: A bit array where each bit corresponds to an inode slot within that segment. A set bit (1) means the inode is in use; a clear bit (0) means it's free.  
    Folowed by **Inode Structures**: Fixed-size (4KB, matching BLOCK\_SIZE) structures containing metadata for files or directories.  
     The maximum number of inodes per segment is limited (e.g., 255 in this implementation) based on fitting the bitmap and the inode structures within the 1MB segment size.

 **Data Segments:**  
     Starts with a **Data Block Bitmap**: A bit array where each bit corresponds to a 4KB data block within that segment. A set bit (1) means the block is in use; a clear bit (0) means it's free.  
     Followed by **Data Blocks**: 4KB blocks used to store file content or directory entries.  
     The maximum number of data blocks per segment is limited (e.g., 255 in this implementation) based on fitting the bitmap and the data blocks within the 1MB segment size.

Inode Structure (Inode)

Each inode occupies one 4KB block and contains crucial metadata:  
 is\_directory: Flag (1 for directory, 0 for file).  
 mode: Basic permissions (primarily used to distinguish file types, e.g., S\_IFDIR, S\_IFREG).  
 size: For files, this is the size in bytes. For directories, it's the count of valid entries within it.  
 direct\_blocks\[\]: An array of block numbers pointing directly to data blocks containing file content or directory entries. The number of direct pointers is calculated dynamically to maximize the use of space within the inode block after accounting for other fields.  
 single\_indirect: A block number pointing to an intermediate block. This intermediate block itself contains an array of data block numbers. Used when a file exceeds the capacity of direct pointers.  
 double\_indirect: A block number pointing to an intermediate block. This block contains an array of single indirect block numbers. Used when a file exceeds the capacity of direct and single indirect pointers.  
      
Free Space Management

 Free inodes and data blocks are tracked using the **bitmaps** located at the beginning of their respective segments.  
 **Allocation** (find\_free\_inode, find\_free\_data\_block): Involves scanning the bitmaps sequentially across all existing segments for the first clear bit (0). Once found, the bit is set (to 1), and the corresponding global inode/block number is returned.  
\* **Exensibility:** If no free slot is found in existing segments during allocation, a new segment file of the appropriate type (inode\_segment\_.exfs or data\_segment\_.exfs)is created, initialized (with an empty bitmap), and the first slot within the new segment is allocated.  
 **Deallocation** (free\_inode, free\_data\_block): Involves locating the correct bitmap and clearing the corresponding bit (setting it back to 0), making the inode/block available for reuse.

Directory Structure

\* Directories are represented internally by an inode with the is\_directory flag set to 1\.  
 The data blocks associated with a directory inode store a sequence of DirectoryEntry structures.  
 Each DirectoryEntry contains:  
     name: Null-terminated filename or subdirectory name (up to MAX\_FILENAME\_LEN characters).  
     inode\_num: The inode number of the corresponding file or subdirectory. An inode number of 0 signifies an unused or deleted entry slot within the data block.  
 When a new directory is created, standard "." (pointing to its own inode) and ".." (pointing to its parent's inode) entries are automatically added to its first data block.

\#\#\# Block Addressing (get\_block\_for\_offset)

This crucial helper function translates a logical byte offset within a file into the physical disk block number where that data resides:  
1\.  The byte offset is divided by BLOCK\_SIZE (4096) to determine the logical block index (0th block, 1st block, etc.) within the file.  
2\.  The function checks if this logical index falls within the range covered by the direct\_blocks\[\] array in the inode. If so, the corresponding block number is returned.  
3\.  If the index is beyond the direct pointers, the single\_indirect block number is read from the inode. The single indirect block itself is then read from disk, and the logical index (adjusted) is used as an index into this block to find the required data block number.  
4\.  If the index is beyond the range covered by single indirect pointers, the double\_indirect block number is read from the inode. This block is read, and the logical index (further adjusted) is used to find the appropriate single indirect\* block number within it. That single indirect block is then read to finally locate the required data block number.

5\.  **Allocation-on-Demand:** If the allocate flag passed to the function is 1, and any required block pointer (direct, indirect, or data) is found to be missing (i.e., 0 or UINT32\_MAX), the function automatically allocates the necessary block(s) using find\_free\_data\_block, initializes them (zeroes them out), updates the relevant inode or indirect block structure in memory, and writes the updated structure back to the segment file before returning the newly allocated block number.

 Building and Running

Building

Assuming a standard Makefile is provided (as required by the assignment):

1\.  **Clean (Optional):** Remove previous build artifacts.  
    make clean  
      
2\.  **Build:** Compile the source code and create the executable (exf2).  
    make

### **Usage**

Once built, the program takes command-line arguments to perform operations:

./exfs2 \[-l | \-D /exfs/path | \-a /exfs/path \-f /local/path | \-r /exfs/path | \-e /exfs/path\]

**Options:**

* \-l: List file system contents recursively starting from the root directory /. Output is sorted alphabetically within each directory level.  
* \-D /exfs/path: List contents starting from the specified /exfs/path with debug information (inode numbers are shown alongside names). Useful for tracing file system structure.  
* \-a /exfs/path \-f /local/path: Add the file located at /local/path on the host system into the ExFS2 file system at the target path /exfs/path. Any intermediate directories within /exfs/path that do not exist will be created automatically. Requires both \-a and \-f.  
* \-r /exfs/path: Remove the file or directory specified by /exfs/path from the ExFS2 file system. If the target is a directory, it and all its contents (files and subdirectories) will be removed recursively.  
* \-e /exfs/path: Extract the content of the file /exfs/path from ExFS2 and print it to standard output (stdout). This option only works for regular files, not directories.

## **Implementation Status**

### **What Works**

* **Core File System Structure:** Initialization (init\_exfs2), creation and management of separate inode and data segments, bitmap-based free space tracking for both inodes and data blocks.  
* **Basic File Operations:** Adding files (-a), listing directory contents (-l, \-D), removing files and directories (-r), and extracting file content (-e) are implemented and functional according to the specifications.  
* **Directory Management:** Creation of directories (including automatic creation of intermediate paths during \-a), handling of . and .. entries, searching for entries within directories (find\_entry\_in\_dir), adding/removing directory entries.  
* **Block Addressing:** Direct, single indirect, and double indirect block pointers are correctly implemented in the inode structure and utilized by get\_block\_for\_offset to read, write, and allocate blocks for files exceeding the capacity of direct pointers.  
* **Extensibility:** The file system correctly creates new inode or data segments when the existing ones are full, allowing the file system to grow beyond the initial 1MB+1MB size.  
* **Data Integrity:** Files added to ExFS2 using \-a and subsequently extracted using \-e should be identical to the original host file. Works for both text and binary files due to the use of fread/fwrite.


## **Testing**

It's crucial to test various scenarios to ensure the file system behaves correctly. Before running tests, ensure the program is compiled (make). It's often useful to remove old segment files (rm ./\*.exfs) before starting a new test sequence.

**1\. Basic File Operations & Data Integrity:**

* **Create a small test file:**  
  echo "Hello ExFS2 World" \> small\_test.txt

* **Add the file:**  
  ./exfs2 \-a /hello.txt \-f small\_test.txt

* **List the root directory:**  
  ./exfs2 \-l  
  \# Expected output: hello.txt

* **Extract the file:**  
  ./exfs2 \-e /hello.txt \> extracted\_hello.txt

* **Verify content:**  
  diff small\_test.txt extracted\_hello.txt  
  \# Expected output: (No output if files match)

* **Remove the file:**  
  ./exfs2 \-r /hello.txt

* **List again to confirm removal:**  
  ./exfs2 \-l  
  \# Expected output: (Empty line or nothing)

**2\. Directory Operations:**

* **Add a file in a new directory:**  
  echo "File in a directory" \> file\_in\_dir.txt  
  ./exfs2 \-a /mydir/myfile.txt \-f file\_in\_dir.txt  
  \# This should automatically create /mydir

* **List the root directory:**  
  ./exfs2 \-l  
  \# Expected output: mydir

* **Use debug listing:**  
  ./exfs2 \-D /  
  \# Expected output: Shows 'mydir' \-\> (inode number)  
  ./exfs2 \-D /mydir  
  \# Expected output: Shows 'myfile.txt' \-\> (inode number) and its size

* **Remove the directory recursively:**  
  ./exfs2 \-r /mydir

* **List root again:**  
  ./exfs2 \-l  
  \# Expected output: (Empty line or nothing)

**3\.** Testing **Indirect Blocks (Single & Double):**

* **Create a file requiring single indirect block:** (Size \> MAX\_DIRECT\_POINTERS \* 4096 bytes)  
  \# Assuming MAX\_DIRECT\_POINTERS is around 1016

  make \# Recompile just in case  
  dd if=/dev/zero of=single\_indirect\_test.dat bs=4096 count=1020  
  ./exfs2 \-a /single\_indirect.dat \-f single\_indirect\_test.dat  
  ./exfs2 \-e /single\_indirect.dat \> extracted\_single.dat  
  cmp single\_indirect\_test.dat extracted\_single.dat && echo "OK" || echo "FAILED"

* **Create a file requiring double indirect block:** (Size \> (MAX\_DIRECT\_POINTERS \+ POINTERS\_PER\_BLOCK) \* 4096 bytes)  
  \# Assuming MAX\_DIRECT\_POINTERS \~1016, POINTERS\_PER\_BLOCK \= 1024\. Threshold \~2040 blocks.  
  dd if=/dev/zero of=double\_indirect\_test.dat bs=4096 count=2050  
  ./exfs2 \-a /double\_indirect.dat \-f double\_indirect\_test.dat  
  ./exfs2 \-e /double\_indirect.dat \> extracted\_double.dat  
  cmp double\_indirect\_test.dat extracted\_double.dat && echo "Double Indirect OK" || echo "Double Indirect FAILED"  
  ./exfs2 \-r /double\_indirect.dat

**4\. Edge Cases & Error Handling:**

* **Add existing file:**  
  echo "First version" \> test.txt  
  ./exfs2 \-a /test.txt \-f test.txt  
  echo "Second version" \> test.txt  
  ./exfs2 \-a /test.txt \-f test.txt  
  \# Expected output: Error message indicating file already exists.

* **Remove non-existent file:**  
  ./exfs2 \-r /non\_existent\_file.txt  
  \# Expected output: Error message indicating file not found.

* **Extract non-existent file:**  
  ./exfs2 \-e /non\_existent\_file.txt  
  \# Expected output: Error message indicating file not found.

* **Invalid path:**  
  ./exfs2 \-a /dir1/../file.txt \-f test.txt  
  \# Behavior might vary, ideally should handle path parsing robustly or error out.  
  ./exfs2 \-l /dir1/nonexistent/..  
  \# Expected output: Error related to path parsing or component not found.

* **Add empty file:**  
  touch empty\_file.txt  
  ./exfs2 \-a /empty.txt \-f empty\_file.txt  
  ./exfs2 \-e /empty.txt \> extracted\_empty.txt  
  cmp empty\_file.txt extracted\_empty.txt && echo "Empty File OK" || echo "Empty File FAILED"  
  ./exfs2 \-l /  
  \# Expected output: Should show empty.txt

