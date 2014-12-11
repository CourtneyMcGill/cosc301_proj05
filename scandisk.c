#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

int num_orphans = 0;

void usage(char *progname) {
    fprintf(stderr, "usage: %s <imagename>\n", progname);
    exit(1);
}
void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent*4; i++)
	printf(" ");
}
/*
uint8_t metadatasize(struct direntry direct){
	return direct.deFileSize(direct)[0];
	}
*/

//counts and returns the number of cluster used by given file
int num_of_clust(uint16_t start_cluster, uint8_t *image_buf, struct bpb33* bpb, int* DIRarr){
	int length = 1; 
	uint16_t nxt_clust = get_fat_entry(start_cluster, image_buf, bpb);
	while(!is_end_of_file(nxt_clust)){
		DIRarr[nxt_clust] = 1;
//		printf("nxt_clust is: %d\n", nxt_clust);
		nxt_clust = get_fat_entry(nxt_clust, image_buf, bpb);
		length++;
	}
//	printf("length: %d\n", length);
	return length;
}

uint16_t print_dirent(struct direntry *dirent, int indent)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    uint32_t size;
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	printf("Volume: %s\n", name);
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    print_indent(indent);
    	    printf("%s/ (directory)\n", name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
	int ro = (dirent->deAttributes & ATTR_READONLY) == ATTR_READONLY;
	int hidden = (dirent->deAttributes & ATTR_HIDDEN) == ATTR_HIDDEN;
	int sys = (dirent->deAttributes & ATTR_SYSTEM) == ATTR_SYSTEM;
	int arch = (dirent->deAttributes & ATTR_ARCHIVE) == ATTR_ARCHIVE;

	size = getulong(dirent->deFileSize);
	print_indent(indent);
	printf("%s.%s (%u bytes) (starting cluster %d) %c%c%c%c\n", 
	       name, extension, size, getushort(dirent->deStartCluster),
	       ro?'r':' ', 
               hidden?'h':' ', 
               sys?'s':' ', 
               arch?'a':' ');
    }

    return followclust;
}
int file_checker(struct direntry *dirent){
    // adapted from print dir
    int checker = 0;
    int i;
    char name[9];
    char extension[4];
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	checker = 0; //make sure still set to 0
	return checker;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	checker = 0;
	return checker;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
	checker = 0;
        return checker;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_VOLUME) != 0) 
    {
	// i dont think we have to do anything here
    } 
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    checker = 1;
        }
    }
    else 
    {
        checker = 1;
    }
   return checker;
}

//this fixes when the number of clusters in the fat chain exceeds the expected number of clusters 
void cluster_chain_long(uint16_t start_cluster, uint8_t *image_buf, struct bpb33* bpb,int* DIRarr, int* FATarr, int* VALIDarr, int* FULLarr, uint32_t expectedNumCluster){
//	printf("entered \n");
	int current_clust = 1;
	uint16_t prev_clust = start_cluster;
	uint16_t next_clust = get_fat_entry(start_cluster,image_buf, bpb);
//	printf("set clusters \n");
	//itterate through until the expected number of clusters is reached
	while(current_clust <expectedNumCluster){
	     prev_clust = next_clust;
	     next_clust = get_fat_entry(next_clust,image_buf, bpb);
	     current_clust++;
	}
//	printf("finished itterations \n");
	//update arrays and free any clusters passed expected number of clusters
	while(!is_end_of_file(get_fat_entry(next_clust, image_buf, bpb))){
	     uint16_t temp = next_clust;
	     DIRarr[next_clust] = 0;
	     next_clust = get_fat_entry(next_clust,image_buf, bpb);
	     set_fat_entry(temp, CLUST_FREE, image_buf, bpb);
	}
	set_fat_entry(prev_clust, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
//	printf("set new eof \n");
	DIRarr[prev_clust] = 0;
	prev_clust = get_fat_entry(prev_clust, image_buf, bpb);
	DIRarr[next_clust] = 0;
//	printf("freedstuff \n");
	set_fat_entry(next_clust, CLUST_FREE, image_buf, bpb);

}

// this was shorter than I expected when starting to make the helper function...
void clust_chain_short(struct direntry *dirent, int numClusters){
     putulong(dirent->deFileSize, numClusters *512);
}

// write the values into a directory entry

void write_dirent(struct direntry *dirent, char *filename, uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

  // clean out anything old that used to be here 
    memset(dirent, 0, sizeof(struct direntry));

 //extract just the filename part 
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) 
    {
	if (p2[i] == '/' || p2[i] == '\\') 
	{
	    uppername = p2+i+1;
	}
    }

 // convert filename to upper case 
    for (i = 0; i < strlen(uppername); i++) 
    {
	uppername[i] = toupper(uppername[i]);
    }

  //set the file name and extension 
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) 
    {
	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    }
    else 
    {
	*p = '\0';
	p++;
	len = strlen(p);
	if (len > 3) len = 3;
	memcpy(dirent->deExtension, p, len);
    }

    if (strlen(uppername)>8) 
    {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

//set the attributes and file size 
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    //could also set time and date here if we really cared...
}



void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    while (1) 
    {
	if (dirent->deName[0] == SLOT_EMPTY) 
	{
	    // we found an empty slot at the end of the directory 
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	   //  make sure the next dirent is set to be empty, just in case it wasn't before
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}

	if (dirent->deName[0] == SLOT_DELETED) 
	{
//	    we found a deleted entry - we can just overwrite it 
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}

void adoption(struct bpb33* bpb, uint8_t *image_buf, int i, int* DIRarr){
     //create the name of the directory entry
	num_orphans ++;
	char name[128];
	char num_str[32];
	strcpy(name, "found");
	sprintf(num_str, "%d\n", num_orphans);
	strcat(name, num_str);
	strcat(name, ".dat");
        struct direntry *dirent = (struct direntry*)cluster_to_addr(0, image_buf, bpb);
     //create the directory entry 
	create_dirent(dirent, name, i, 512, image_buf, bpb);
	DIRarr[i] = 1;
        set_fat_entry(i, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
     //map the directory entry to this cluster
}

void badFixer(int* VALIDarr,uint8_t *image_buf, struct bpb33* bpb){
      int FAT_length =  bpb->bpbSectors - 1 - 9 - 9 - 14;
      for(int i = 2; i < FAT_length; i++){
	if (get_fat_entry(i, image_buf, bpb) == CLUST_BAD){
	    
	}
      }
}

void orphanChecker(int* DIRarr, int* FATarr, uint8_t *image_buf, struct bpb33* bpb){
      int FAT_length =  bpb->bpbSectors - 1 - 9 - 9 - 14;
      for(int i = 2; i < FAT_length; i++){
	   if ((DIRarr[i] == 0) && (is_valid_cluster(i, bpb))){
		//check that entry in the fat
		if((get_fat_entry(i, image_buf, bpb))!= CLUST_FREE){ //is this how we test that it is free?
		      printf("FAT entry: %d is an orphan \n", i);
		      adoption(bpb, image_buf, i, DIRarr);
		}
	   }
     }
}



void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, int* DIRarr, int* FATarr, int* VALIDarr, int* FULLarr)
{
    while (is_valid_cluster(cluster, bpb))
    {
	
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
	    uint16_t followclust = print_dirent(dirent, indent);
	    if ((dirent->deAttributes & ATTR_DIRECTORY) != 0)
		{
		    DIRarr[getushort(dirent->deStartCluster)] = 1;
		}
	    if(file_checker(dirent)==1){
	        uint32_t fileSize = 0;
	        uint16_t start_cluster = 0;
	        int numClusters = 0;
		uint32_t expectedNumClusters = 0;
		fileSize = getulong(dirent->deFileSize);
//		printf("fileSize: %d\n", fileSize);
		start_cluster = getushort(dirent->deStartCluster);

		DIRarr[start_cluster] = 1;
	//	printf("start_cluster %d\n: ", start_cluster);
		
		numClusters = num_of_clust(start_cluster, image_buf, bpb, DIRarr);
		if (fileSize % 512 == 0){
		    expectedNumClusters = fileSize/512;
		}

		else{
//		    printf("added 1 \n");
		    expectedNumClusters = (fileSize/512)+1;
		}

		if(numClusters > expectedNumClusters){
	//	      printf("EROR MESSAGE: filesize in metadata is smaller than cluster chain length \n!");
		      cluster_chain_long(start_cluster, image_buf, bpb, DIRarr, FATarr, VALIDarr, FULLarr, expectedNumClusters);
			// do something
		      		
		}

		else if(numClusters < expectedNumClusters){
	//	      printf("EROR MESSAGE: filesize in metadata is larger than cluster chain length \n!");
			clust_chain_short(dirent, numClusters);
		      //do something
		}
		else{
	//	      printf("correct size! \n");
		}
	    }
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb, DIRarr, FATarr, VALIDarr, FULLarr);
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}




void traverse_root(uint8_t *image_buf, struct bpb33* bpb, int* DIRarr, int* FATarr, int* VALIDarr, int* FULLarr)
{
    uint16_t cluster = 0;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0);
        if (is_valid_cluster(followclust, bpb))
            follow_dir(followclust, 1, image_buf, bpb, DIRarr, FATarr, VALIDarr, FULLarr);

        dirent++;
    }
    orphanChecker(DIRarr, FATarr, image_buf, bpb);

}


uint16_t get_dirent(struct direntry *dirent, char *buffer)
{
    uint16_t followclust = 0;
    memset(buffer, 0, MAXFILENAME);

    int i;
    char name[9];
    char extension[4];
    uint16_t file_cluster;
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    if (name[0] == SLOT_EMPTY)
    {
	return followclust;
    }

    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
	return followclust;
    }

    if (((uint8_t)name[0]) == 0x2E)
    {
	// dot entry ("." or "..")
	// skip it
        return followclust;
    }

    /* names are space padded - remove the spaces */
    for (i = 8; i > 0; i--) 
    {
	if (name[i] == ' ') 
	    name[i] = '\0';
	else 
	    break;
    }

    /* remove the spaces from extensions */
    for (i = 3; i > 0; i--) 
    {
	if (extension[i] == ' ') 
	    extension[i] = '\0';
	else 
	    break;
    }

    if ((dirent->deAttributes & ATTR_WIN95LFN) == ATTR_WIN95LFN)
    {
	// ignore any long file name extension entries
	//
	// printf("Win95 long-filename entry seq 0x%0x\n", dirent->deName[0]);
    }
    else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
            strcpy(buffer, name);
            file_cluster = getushort(dirent->deStartCluster);
            followclust = file_cluster;
        }
    }
    else 
    {
        /*
         * a "regular" file entry
         * print attributes, size, starting cluster, etc.
         */
        strcpy(buffer, name);
        if (strlen(extension))  
        {
            strcat(buffer, ".");
            strcat(buffer, extension);
        }
    }

    return followclust;
}



int main(int argc, char** argv) {
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc < 2) {
	usage(argv[0]);
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    int FAT_length =  bpb->bpbSectors - 1 - 9 - 9 - 14;	

    // your code should start here...

    int* FATarr = malloc(FAT_length*sizeof(int));
    int* DIRarr = malloc(FAT_length*sizeof(int));
    int* VALIDarr = malloc(FAT_length*sizeof(int));
    int* FULLarr = malloc(FAT_length*sizeof(int));
    for (int i = 0; i < FAT_length; i++){
	 FATarr[i] = 0;
	 DIRarr[i] = 0;
	 VALIDarr[i] = 0;
	 FULLarr[i] = 0;
    } 

    traverse_root(image_buf, bpb, DIRarr, FATarr, VALIDarr, FULLarr);
    unmmap_file(image_buf, &fd);
    free(FATarr);
    free(DIRarr);
    free(VALIDarr);
    free(FULLarr);
    free(bpb);
    return 0;
}
