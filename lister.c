#include <stdio.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
  
void getls() {
    DIR *d;
    struct dirent *dir;
    char buffer[1024] = {0};
    char tmpbuf[1024] = {0};
    d = opendir(".");

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            snprintf(tmpbuf, 1024, "%s\n", dir->d_name);
            strcat(buffer, tmpbuf);
        }
        closedir(d);
    }
    else
    {
        sprintf(buffer, "[-] permission or directory error\n");
    }

    printf("%s", buffer);

    return;
}

int main() {
	getls();
	
    return 0;
}