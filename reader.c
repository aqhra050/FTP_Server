#include <stdio.h>
#include <stdlib.h> // For exit()

//Will get the file length of the file with name, fileName, if said file is present and -1 otherwise
int getFileLength(char* fileName) {
	FILE *fptr;
    fptr = fopen(fileName,"r");
    if (fptr == NULL) {
        printf("%p\n", fptr);
        return -1;
    }
	fseek(fptr,0,SEEK_END);						//move the file pointer to the end of the file
	int file_size = ftell(fptr);				//read the position of the filepointer which will be equal to the size of the file
	fclose(fptr);
	return file_size;
}
  
int main()
{
    FILE *fptr;
  
    char filename[100], c;
  
    printf("Enter the filename to open \n");
    scanf("%s", filename);
  
    // Open file
    fptr = fopen(filename, "r");
    if (fptr == NULL)
    {
        printf("Cannot open file \n");
        exit(0);
    }
  
    // // Read contents from file
    // c = fgetc(fptr);
    // while (c != EOF)
    // {
    //     printf ("%c", c);
    //     c = fgetc(fptr);
    // }
  
    fclose(fptr);

    printf("%d\n", getFileLength(filename));
    
    return 0;
}