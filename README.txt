-------------------------
MULTI-THREADED WORD-COUNT
-------------------------
Usage:

a)make
Above is for compiling the wordCount.c file into its object file.
Note: make clean attempts to remove the object file and wordCount.txt, if it exists. If the output text file does not exist, it will throw an error- this can be neglected.

b) ./wordCount <input file> <number of mapper threads> <number of reducer threads>
The input file contains a list of newline-separated words. There is no summarizer.

Example: ./wordCount file1.txt 3 4 
Means 3 mapper threads, 4 reducer threads, and 1 MPU thread and 1 wordcount writer.

Default size of buffers: Mapper Pool: 3; Reducer Pool: 3; Summarizer Pool: 4
These can be changed by changing their macros.
---
MPU
---
The "Mapper Pool Updater' opens the input file for reading, and puts words starting
with the same letter in a separate entry into the mapper pool. These entries are 
implemented as singly linked lists. The mapper pool is an array of pointers. They
can be configured to point to an entry. Once the MPU runs through the end of file,
it updates a shared flag that is shared between MPU and the Mapper (and protected
by a mutex) before exiting. 
------
MAPPER
------
The mapper threads are implemented as infinite while loops. At each iteration, the
thread checks and retrieves an entry from the mapper pool, and generates (word,1)
tuples and forwards them to the reducer pool. Access to the mapper pool is
serialized using mutexes. If the buffer is empty and the shared flag is set, the
mapper thread terminates.
-------
REDUCER
-------
The reducer gets an entry from the reducer pool- this contains (word,1) pairs. For
each unique word, it creates a separate node: having word and count as parameters.
The word, count of each word is hence stored in an entry in the summarizer pool. 
Both the reducer pool and the summarizer pool are protected with mutexes. If the
reducer is empty, and all the mappers have exited, the reducer exits. A shared flag
variable (mappers_flag) which is protected with a separate mutex, is used to keep
track of whether all mappers have exited. The separate mutex provides higher 
parallelism and less unnecessary blocking.
---
WCW
---
The "Word Count Writer" simply reads entries from the summarizer pool and writes 
them to a file wordCount.txt. It keeps track of whether all reducers have exited
with the help of the shared 'reducers_flag', which is protected with a separate
mutex.

The main thread simply initializes the mutexes and spawns these threads. It waits
for all of them to exit before terminating the program.
