/*
    
  Courtney McGill and Parker Reynolds


*/


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

int numfiles = 0;
/*
char *filenames[4000];
int startclusters[4000];
*/

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

//adapted from print_dirent
//used for our attempt at fixing the duplicates
uint16_t directory_name(char* new_name, struct direntry *dirent, char **filenames, int* startclusters)
{
    uint16_t followclust = 0;

    int i;
    char name[9];
    char extension[4];
    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);
    


    /* skip over deleted entries */
    if (((uint8_t)name[0]) == SLOT_DELETED)
    {
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
   
    new_name[0] = '\0';

    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) 
    {
        // don't deal with hidden directories; MacOS makes these
        // for trash directories and such; just ignore them.
	if ((dirent->deAttributes & ATTR_HIDDEN) != ATTR_HIDDEN)
        {
	    return -1;
        }
	strcat(new_name, name);
        strcat(new_name, ".");
        strcat(new_name, extension);
        return 0;
    }
   strcat(new_name, name);
   return 0;

    return followclust;
}


//attempt at fixing a duplicate file
void duplicateFixer(uint8_t *image_buf, struct bpb33* bpb, char **filenames, int* startclusters){
     for (int i = 0; i< numfiles; i ++){
	 for (int j = 0; j< numfiles; j ++){
	     if(i != j && strcasecmp(filenames[i], filenames[j]) == 0){
		 
                 { 
		     printf("duplicate found");	
		     char dupName[128];
		     struct direntry *dirent = (struct direntry*)cluster_to_addr(startclusters[j], image_buf, bpb);
		     directory_name(dupName, dirent, filenames, startclusters);
    		     char *p = strchr(dupName, '.');
    	             strcpy(p, "copy\0"); 
                     memcpy(dirent->deName, dupName, strlen(dupName));
		 }
	     }	
	 }
      }
}


//counts and returns the number of cluster used by given file
int num_of_clust(uint16_t start_cluster, uint8_t *image_buf, struct bpb33* bpb, int* DIRarr){
	int length = 1; 
	uint16_t previous = start_cluster;
	DIRarr[start_cluster] = 1;
	uint16_t nxt_clust = get_fat_entry(start_cluster, image_buf, bpb);
	while(!is_end_of_file(nxt_clust)){
		DIRarr[nxt_clust] = 1;
		nxt_clust = get_fat_entry(nxt_clust, image_buf, bpb);

		if (nxt_clust == (FAT12_MASK & CLUST_BAD)){
		    printf("Bad cluster detected! \n ");
		    set_fat_entry(previous, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
		    set_fat_entry(nxt_clust, (FAT12_MASK & CLUST_FREE), image_buf, bpb);
		    
		}

		length++;
	}
	DIRarr[nxt_clust] = 1;
	return length;
}

uint16_t print_dirent(struct direntry *dirent, int indent, char **filenames, int* startclusters )
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
	
	startclusters[numfiles] = dirent->deStartCluster;
	filenames[numfiles] = name;
	numfiles++;
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

//this fixes when the number of clusters in the fat -long exceeds the expected number of clusters 
void cluster_chain_long(uint16_t start_cluster, uint8_t *image_buf, struct bpb33* bpb,int* DIRarr, uint32_t expectedNumCluster){
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
	//update arrays and free any clusters passed expected number of clusters
	while(!is_end_of_file(get_fat_entry(next_clust, image_buf, bpb))){
	     uint16_t temp = next_clust;
	     DIRarr[next_clust] = 0;
	     next_clust = get_fat_entry(next_clust,image_buf, bpb);
	     set_fat_entry(temp, CLUST_FREE, image_buf, bpb);
	}
	set_fat_entry(prev_clust, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
	DIRarr[prev_clust] = 1;
	prev_clust = get_fat_entry(prev_clust, image_buf, bpb);
	DIRarr[next_clust] = 0;
	set_fat_entry(next_clust, CLUST_FREE, image_buf, bpb);
	DIRarr[start_cluster] = 1;
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
	sprintf(num_str, "%d", num_orphans);
	strcat(name, num_str);
	strcat(name, ".dat");
        struct direntry *dirent = (struct direntry*)cluster_to_addr(0, image_buf, bpb);
     //create the directory entry 
	create_dirent(dirent, name, i, 512, image_buf, bpb);
	DIRarr[i] = 1;
	DIRarr[getushort(dirent->deStartCluster)] = 1;
        //set_fat_entry(orphan, (FAT12_MASK & CLUST_EOFS), image_buf, bpb);
     //map the directory entry to this cluster
}

int isHead(int orphannum, int* orphans, int FAT_length, uint8_t *image_buf, struct bpb33* bpb){
	
	   for(int j = 0; j < FAT_length; j++){
		uint16_t clust = get_fat_entry(orphans[j], image_buf, bpb);
		if(clust == orphans[orphannum]){
		    return 0;
		}
	    }
	
   return 1;
}

void orphanChecker(int* DIRarr, uint8_t *image_buf, struct bpb33* bpb){
      int FAT_length =  bpb->bpbSectors - 1 - 9 - 9 - 14;
      int orphans[FAT_length];
      int j = 0;
      int i;
      for(i = 2; i < FAT_length; i++){
	   if ((is_valid_cluster(i, bpb) )&& (DIRarr[i] == 0)){
		//check that entry in the fat
		if((get_fat_entry(i, image_buf, bpb))!= CLUST_FREE){
                      orphans[j] = i;
		      j++;
		      printf("FAT entry: %d is an orphan \n", i);
		}
	   }
     }
     
     for (int k = 0; k < j; k++){
	if(isHead(k, orphans, FAT_length, image_buf, bpb)){
		adoption(bpb, image_buf, orphans[k], DIRarr);
	}
     }
     
}

void follow_dir(uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb, int* DIRarr, char **filenames, int* startclusters)
{
    while (is_valid_cluster(cluster, bpb))
    {
	
        struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);

        int numDirEntries = (bpb->bpbBytesPerSec * bpb->bpbSecPerClust) / sizeof(struct direntry);
        int i = 0;
	for ( ; i < numDirEntries; i++)
	{
	    uint16_t followclust = print_dirent(dirent, indent, filenames, startclusters);
	    if ((dirent->deAttributes & ATTR_DIRECTORY) != 0){
		    DIRarr[getushort(dirent->deStartCluster)] = 1;
		}
	    if(is_end_of_file(getushort(dirent->deStartCluster))){
		DIRarr[getushort(dirent->deStartCluster)] = 1; // file of size uno
	    }
	    if(file_checker(dirent)==1){
	        uint32_t fileSize = 0;
	        uint16_t start_cluster = 0;
	        int numClusters = 0;
		uint32_t expectedNumClusters = 0;
		fileSize = getulong(dirent->deFileSize);
		start_cluster = getushort(dirent->deStartCluster);

		DIRarr[start_cluster] = 1;
		
		numClusters = num_of_clust(start_cluster, image_buf, bpb, DIRarr);
		if (fileSize % 512 == 0){
		    expectedNumClusters = fileSize/512;
		}

		else{
		    expectedNumClusters = (fileSize/512)+1;
		}

		if(numClusters > expectedNumClusters){
		      printf("EROR MESSAGE: filesize in metadata is smaller than cluster chain length \n!");
		      cluster_chain_long(start_cluster, image_buf, bpb, DIRarr, expectedNumClusters);

		}

		else if(numClusters < expectedNumClusters){
		      printf("EROR MESSAGE: filesize in metadata is larger than cluster chain length \n!");
			clust_chain_short(dirent, numClusters);
		}
		else{
		      printf("correct size! \n");
		}
	    }
            if (followclust)
                follow_dir(followclust, indent+1, image_buf, bpb, DIRarr, filenames, startclusters);
            dirent++;
	}

	cluster = get_fat_entry(cluster, image_buf, bpb);
    }
}




void traverse_root(uint8_t *image_buf, struct bpb33* bpb, int* DIRarr, char **filenames, int* startclusters)
{
    uint16_t cluster = 0;
    struct direntry *dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int i = 0;
    for ( ; i < bpb->bpbRootDirEnts; i++)
    {
        uint16_t followclust = print_dirent(dirent, 0, filenames, startclusters);

	if (is_end_of_file(get_fat_entry(getushort(dirent->deStartCluster), image_buf, bpb))){
	    DIRarr[(getushort(dirent->deStartCluster))] = 1;
	}
        if (is_valid_cluster(followclust, bpb)){
            follow_dir(followclust, 1, image_buf, bpb, DIRarr, filenames, startclusters);
	}
	else if(is_valid_cluster(followclust-1, bpb)){
		DIRarr[getushort(dirent->deStartCluster)] =1;
	}
        dirent++;
    }
    orphanChecker(DIRarr, image_buf, bpb);
   // duplicateFixer(image_buf, bpb, filenames, startclusters);


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
    int FAT_length =  (bpb->bpbSectors/bpb->bpbSecPerClust)+1;	

    // your code should start here...

    int* DIRarr = malloc(FAT_length*sizeof(int)); //stores 1s and 0s to say if in directory
    char **filenames = malloc(1000*sizeof(char*)); // holds file names for duplicate checking
    int* startclusters = malloc(1000*sizeof(int)); // holds the starting clusters for 							      duplicate checking
    for (int i = 0; i < FAT_length; i++){	   // initialize all the spots to 0 for not in 								directory
	 DIRarr[i] = 0;

    }
    traverse_root(image_buf, bpb, DIRarr, filenames, startclusters);
    unmmap_file(image_buf, &fd);

   free(DIRarr);
    free(bpb);
    return 0;
}
