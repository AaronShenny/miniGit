#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

/* run this program using the console pauser or add your own getch, system("pause") or input loop */

int  checkdir(){
	errno = 0;
	DIR* dir = opendir(".bit");
	if (dir) {
	    //printf("Exists\n");
	    
	    closedir(dir);
	    return 0;
	} else if (ENOENT == errno) {
	    //printf("Directory not exits\n");
	    return 1;
	} else {
	   //printf("Error");
	   return -1;
	}

}

int hash_file(char fileinput[]){
	//hash the file and return the hash value
	int hash = 5381;
	FILE *fp = fopen(fileinput, "rb");
	if (fp == NULL) {
		printf("Error opening file: %s\n", fileinput);
		return -1;
	}
	int c;
	while ((c = fgetc(fp)) != EOF) {
		hash =  hash * 33 + c ;
	}
	fclose(fp);
	return hash;
}
// need a hand
void store_blob(char fileinput[]){
	int hash = hash_file(fileinput);
	char path[100];
	sprintf(path, ".bit/objects/blobs/%d", hash);
	FILE *fp = fopen(path, "wb");
	if (fp == NULL) {
		printf("Error opening file: %s\n", path);
		return;
	}
	FILE *input_fp = fopen(fileinput, "rb");
	if (input_fp == NULL) {
		printf("Error opening file: %s\n", fileinput);
		fclose(fp);
		return;
	}
	int c;
	while ((c = fgetc(input_fp)) != EOF) {
		fputc(c, fp);
	}
	fclose(fp);
	fclose(input_fp);
}

int main(int argc, char *argv[]) {
	
	if(argc<2){
		printf("bit: <usage>");
		return 1;
	}
	if ( strcmp(argv[1], "init") == 0){
		int check = checkdir();
		if (check==1){
			mkdir(".bit", 0755);
			mkdir(".bit/objects", 0755);
			mkdir(".bit/objects/blobs", 0755);
			mkdir(".bit/objects/trees", 0755);
			mkdir(".bit/commits", 0755);
			
			FILE *fp = fopen(".bit/HEAD", "w");
			fclose(fp);
			FILE *fp2 = fopen(".bit/activity.log", "w");
			fclose(fp2);
			printf("bit: Initialized an empty repository.\n");

		}
		else if (check==0){
			printf("bit: Already initialized an empty repository\n");
		}
		else if (check==-1){
			printf("bit: Error in initializing..\n");
		}
		
	}
	else{
		printf("bit: Unknown Command! Please use bit help\n");
	}
	return 0;
}
