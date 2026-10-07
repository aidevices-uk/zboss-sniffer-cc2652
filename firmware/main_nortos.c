#include <stdint.h>
#include <stddef.h>

#include <NoRTOS.h>
#include <ti/drivers/Board.h>

extern void *mainThread(void *arg0);

int main(void)
{
    Board_init();
    NoRTOS_start();
    mainThread(0);
    while (1)
    {
    }
}
