#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

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


int main(int argc, char *argv[]) {
	
	if(argc<2){
		printf("bit: <usage>");
		return;
	}
	if ( strcmp(argv[1], "init") == 0){
		int check = checkdir();
		if (check==1){
			mkdir(".bit");
			mkdir(".bit/objects");
			mkdir(".bit/objects/blobs");
			mkdir(".bit/objects/trees");
			mkdir(".bit/commits");
			
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
		printf("Unknown Command!");
	}
}
