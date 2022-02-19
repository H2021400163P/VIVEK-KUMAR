#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>              //open
#include <unistd.h>             //exit
#include <stdint.h>
#include <sys/ioctl.h>          //ioctl
#include "adcchardev1.h"


uint16_t str_data; // data to be stored after allignment conversion

int ioctl_sel_channel(int file_pram, int channel)
{
    int value;
    value = ioctl(file_pram, SEL_CHANNEL, channel);
    if (value < 0)  // request check
    {
        printf("ioctl channel selection failed:%d\n", value);
        exit(-1);
    }
    return 0;
}

int ioctl_sel_allignment(int file_pram, char allignment)  // allignment conversion
{
    int value;
    value = ioctl(file_pram, SEL_ALIGNMENT, allignment);
    if (value < 0)
    {
        printf("ioctl selction allignmentment failed:%d\n", value);

        exit(-1);
    }
    return 0;
}

int ioctl_sel_conv(int file_pram, int conv) //select coversion mode whether 1 shot or cont. coversion
{
    int value;
    value = ioctl(file_pram, SEL_CONV, conv ); // the request code and the file descriptor of the device file is passed through IOCL call

    if (value < 0)
 {
        printf("IOCTL coversion selection failed:%d\n", value);
        exit(-1);
    }
    return 0;
}
// decimal to binary conversion
void dectobinary(uint16_t n)
{
  int binary[16]; //16bit binary number to be taken
  int i = 0;
  while (n > 0) {
      binary[i] = n % 2;
      n = n / 2;
      i++;
  }
  printf("This number in binary is:");
  for (int j = i - 1; j >= 0; j--){
      printf("%d", binary[j]);
     }
    printf("\n");
}


/*
 * Main - Call the ioctl functions
 */
int main()
{
    int file_pram, value;
    int channel;
    char allignment;
    int conv;
    file_pram = open(DEVICE_FILE_NAME, 0);
    
    if (file_pram < 0) {
        printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
        exit(-1);
    }
    printf("Enter the Channel you want to select (0 to 7):");
    scanf("%d", &channel);
    printf("Enter the allignment r for right and l for left(r or l):");
    scanf(" %c", &allignment);
    if(channel <0 || channel>7 || (allignment != 'r' && allignment != 'l'))
{
      printf("oops invalid Selection of Channel or alignment\n"); 
      exit(-1);
    }

    printf("Enter type of covesrion( 0-1):");
    scanf("%d", &conv);

    ioctl_sel_channel(file_pram, channel);
    ioctl_sel_allignment(file_pram,allignment);
    ioctl_sel_conv(file_pram,conv);


    if(read(file_pram,&str_data,sizeof(str_data)))
{
      if(allignment == 'l')
      {
        printf("The allignmentment selected is left so the random number allignmented to left side is:- %u ", str_data);
        dectobinary(str_data);
        str_data = str_data/16;
        printf("The actual decimal number is: %u ", str_data);
        dectobinary(str_data);
      }
      else
      {
        printf("str_data read by the user is:%u ", str_data);
        dectobinary(str_data);

      }
 if (conv ==0)
{
printf("one shot execution");
}
else
{
 for(int i=0;i<50;i++)
 { printf("\ndata read by the user is:%u --", str_data);
}
}
}
    close(file_pram);
    return 0;
}
