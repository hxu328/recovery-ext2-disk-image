1. Invoking the program
	make
	./runScan path/to/disk/image output/directory/path
2. Possible problems
	the file-<inode number>.jpg and <actual filename>.jpg are hard linked to the same inode in the output directory
3. Removing the executable file and output directory (assume the output directory is named as: output)
	make clean

