#define HEADERS_IMPLEMENTATION
#include "headers.h"

int remainingtime;

int main(int agrc, char * argv[])
{
    initClk();
    
    remainingtime = atoi(argv[1]);
    while (remainingtime > 0)
    {
        remainingtime--;
        sleep(1);
    }
    
    destroyClk(false);
    
    return 0;
}
