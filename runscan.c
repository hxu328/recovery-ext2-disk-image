#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ext2_fs.h"
#include "read_ext2.h"

void fill_block_buffer(int fd, struct ext2_inode *inode, uint32_t* buffer, uint32_t num_block){

	uint32_t i = 0;
	while(i < num_block){
		// non-indirect
		if(i < EXT2_NDIR_BLOCKS){
			buffer[i] = (uint32_t)(inode->i_block[i]);
			//printf("Fill buffer: %u, with %u\n", i, buffer[i]);
		} else if (i < (EXT2_NDIR_BLOCKS + 256)){ // indirect pointer
			uint32_t indirect_block = inode->i_block[EXT2_IND_BLOCK];
			off_t indirect_block_address = BLOCK_OFFSET(indirect_block);
			char temp_buffer[1024];
			lseek(fd, indirect_block_address, SEEK_SET);
			read(fd, &temp_buffer, sizeof(char) * block_size);
			
			off_t target_address = (i - EXT2_NDIR_BLOCKS) * 4;
			uint32_t* pointer = (uint32_t*)((char*)temp_buffer + target_address);
			uint32_t block_no = *pointer;

			buffer[i] = block_no;
			//printf("Fill buffer: %u, with %u\n", i, buffer[i]);
		} else if (i < (EXT2_NDIR_BLOCKS + 256*256)){
			// get double indirect block
			uint32_t double_indirect_block = inode->i_block[EXT2_DIND_BLOCK];
			off_t double_indirect_block_address = BLOCK_OFFSET(double_indirect_block);
			char temp_buffer1[1024];
			lseek(fd, double_indirect_block_address, SEEK_SET);
			read(fd, &temp_buffer1, sizeof(char) * block_size);

			// get the entry in the double indirect block
			off_t offset1 = ((i - (EXT2_NDIR_BLOCKS + 256)) / 256) * 4;
			uint32_t* pointer1 = (uint32_t*)((char*)temp_buffer1 + offset1);
			uint32_t block_no1 = *pointer1;

			// get the block of the entry in the double indirect block
			off_t indirect_child_block_address = BLOCK_OFFSET(block_no1);
			char temp_buffer2[1024];
			lseek(fd, indirect_child_block_address, SEEK_SET);
			read(fd, &temp_buffer2, sizeof(char) * block_size);

			// get the entry in the indirect child block
			off_t offset2 = ((i - (EXT2_NDIR_BLOCKS + 256)) % 256) * 4;
			uint32_t* pointer2 = (uint32_t*)((char*)temp_buffer2 + offset2);
			uint32_t block_no2 = *pointer2;

			buffer[i] = block_no2;
			//printf("Fill buffer: %u, with %u\n", i, buffer[i]);

		}
		i++;
	}
}

void copy_content(FILE* file, int fd, uint32_t*buffer, uint32_t num_block, uint32_t total_bytes){

	uint32_t remain_byte = total_bytes - (num_block - 1) * block_size;

	for(uint32_t i = 0; i < num_block; i++){
		// fill the buffer with the content in data block
		uint32_t block_no = buffer[i];
		off_t block_address = BLOCK_OFFSET(block_no);
		char content[1024];
		lseek(fd, block_address, SEEK_SET);
		read(fd, &content, sizeof(char) * block_size);

		char c;
		if(i < num_block - 1){
			for(uint32_t k = 0; k < 1024; k++){
				c = content[k];
				putc(c, file);
			}
		} else if (i == num_block - 1){
			for(uint32_t k = 0; k < remain_byte; k++){
				c = content[k];
				putc(c, file);
			}
		}
		
		//fclose(file);
	}

}

void parse_entry(int fd, uint32_t block_no, char* out_dir){
	// get the block content
	printf("Parse Begin: begin scan block: %u\n", block_no);
	off_t block_address = BLOCK_OFFSET(block_no);
	char buffer[1024];
	lseek(fd, block_address, SEEK_SET);
	read(fd, &buffer, sizeof(char) * block_size);

	int i = 0; // index

    while(i < 1024){
		// if(buffer[i] == '\0'){
		// 	break;
		// }
		struct ext2_dir_entry* dentry = (struct ext2_dir_entry*) & ( buffer[i]);
		uint32_t inode_no = dentry->inode;
		int name_len = dentry->name_len & 0xFF;
		char name [EXT2_NAME_LEN];
		strncpy(name, dentry->name, name_len);
		name[name_len] = '\0';
		//printf("Parse Entry: inode number: %u\n", inode_no);
		//printf("Entry name is --%s--\n", name);

		// rename jpg files

		// old name
		char path_buffer[1024];
		sprintf(path_buffer, "./%s/file-%d.jpg", out_dir, (int)inode_no);

		//printf("Trying to find this file: %s\n", path_buffer);

		// check existence of the old name in the output directory
		FILE* old_file;
		if((old_file = fopen(path_buffer, "r")) != NULL){  // old file exists
			//printf("The file: %s exits\n", path_buffer);

			// create new file path
			char new_file_path[1024];
			sprintf(new_file_path, "./%s/%s", out_dir, name);
			//printf("Trying to create this file: %s\n", new_file_path);

			// hard link the new file name and old file name
			int l = link(path_buffer, new_file_path);
			if(l == 0) {
				printf("Hard link created for %s, %s\n", new_file_path, path_buffer);
			}
			
		}

		i = i + 8 + (name_len / 4) * 4;
		if((name_len % 4) != 0){
			i = i + 4;
		}

	}

}

int main(int argc, char **argv) {
	if (argc != 3) {
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}
	
	// open disk image
	int fd;
	fd = open(argv[1], O_RDONLY);

	// Check the existence of the output directory
	DIR *dp = opendir(argv[2]);
	if(dp != NULL){
		printf("Error: the output file: %s already exists\n", argv[2]);
		exit(1);
	}

	// Create the output directory
	mkdir(argv[2], 0777);

	// get disk overall infomation (number of groups)
	ext2_read_init(fd);
	//printf("Number of groups: %d\n", num_groups);

	if(num_groups == 0){  // if # of groups is 0, just exit
		exit(0);
	}

	
	// iterate each group
	unsigned int group_no;
	unsigned int i = 1; //inode counter
	int total_jpg_num = 0;
	int total_regular_file = 0;
	int total_directory = 0;

	for(group_no = 0; group_no < num_groups; group_no++){

		// get info of each group
		// printf("Read group: %d\n", group_no);
		// struct ext2_super_block super;
		struct ext2_group_desc group;
		// read_super_block(fd, group_no, &super);
		read_group_desc(fd, group_no, &group);
		off_t start_inode_table = locate_inode_table(group_no, &group);
		// printf("Inode table offset: %li\n", start_inode_table);

		// iterate the inode table for each group
		for (unsigned int k = 1; k <= inodes_per_block * itable_blocks; k++){

			// get inode info
			// printf("inode %u: \n", i);
			struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
            read_inode(fd, 0, start_inode_table, k, inode);

			// get inode type
			uint32_t file_size = inode->i_size;
			uint32_t file_block_num = file_size/1024;
			uint32_t file_byte_remainder = file_size % 1024;
			if(file_byte_remainder > 0){
				file_block_num++;
			}

			if (S_ISDIR(inode->i_mode)) {
				total_directory++;
				//printf("Found dict: %d\n", i);
				//printf("Size of dict: %u\n", file_size);
			}
			else if (S_ISREG(inode->i_mode)) {
				total_regular_file++;
				//printf("Found regular file: %d\n", i);

				char buffer[1024];
				uint32_t first_block = inode->i_block[0];
				off_t first_block_address = BLOCK_OFFSET(first_block);
				lseek(fd, first_block_address, SEEK_SET);
				read(fd, &buffer, sizeof(char) * block_size);
				int is_jpg = 0;
				if (buffer[0] == (char)0xff &&
					buffer[1] == (char)0xd8 &&
					buffer[2] == (char)0xff &&
					(buffer[3] == (char)0xe0 ||
					buffer[3] == (char)0xe1 ||
					buffer[3] == (char)0xe8)) {
					is_jpg = 1;
				}
				if(is_jpg == 1){
					total_jpg_num++;
					//printf("Found jpg: %d\n", i);

					// create and open file in the target folder
					char path_buffer[1024];
					sprintf(path_buffer, "./%s/file-%d.jpg", argv[2], i);
					//printf("Trying to create this file: %s\n", path_buffer);
					FILE* out_file;
					if((out_file = fopen(path_buffer, "wb")) == NULL){
						printf("Error: can't create jpg file: %s\n", path_buffer);
					}

					// populate a buffer with all data block numbers for the jpg file
					uint32_t* buffer = malloc(sizeof(uint32_t) * file_block_num);
					fill_block_buffer(fd, inode, buffer, file_block_num);
					
					// copy the content
					copy_content(out_file, fd, buffer, file_block_num, file_size);

					// close file
					fclose(out_file);
				}

			}
			else {
			// this inode represents other file types
			}
			
            free(inode);
			
			// increment the inode counter
			i++;
		}
	}

	printf("Total directory: %d\n", total_directory);
	printf("Total regular files: %d\n", total_regular_file);
	printf("Total jpg files: %d\n", total_jpg_num);

	// recover filenames
	i = 1;
	for(group_no = 0; group_no < num_groups; group_no++){

		// get info of each group
		//printf("Read group: %d\n", group_no);
		// struct ext2_super_block super;
		struct ext2_group_desc group;
		// read_super_block(fd, group_no, &super);
		read_group_desc(fd, group_no, &group);
		off_t start_inode_table = locate_inode_table(group_no, &group);
		//printf("Inode table offset: %li\n", start_inode_table);

		// iterate the inode table for each group
		for (unsigned int k = 1; k <= inodes_per_block * itable_blocks; k++){

			// get inode info
			// printf("inode %u: \n", i);
			struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
            read_inode(fd, 0, start_inode_table, k, inode);

			// get inode type
			uint32_t file_size = inode->i_size;
			uint32_t file_block_num = file_size/1024;
			uint32_t file_byte_remainder = file_size % 1024;
			if(file_byte_remainder > 0){
				file_block_num++;
			}

			if (S_ISDIR(inode->i_mode)) {
				total_directory++;
				//printf("Found dict: %d\n", i);
				//printf("Size of dict: %u\n", file_size);

				uint32_t block_no = inode->i_block[0];
				printf("Parse Begin: begin inode number: %u\n", i);
				parse_entry(fd, block_no, argv[2]);
			}
			
			
            free(inode);
			
			// increment the inode counter
			i++;
		}

	}

	close(fd);
	
}
