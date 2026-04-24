#include "MMU.h"
#include <stdio.h>
#include <unistd.h>

Frame memory[FRAME_COUNT];

void initMemory()
{
    for (int i = 0; i < FRAME_COUNT; i++)
    {
        memory[i].occupied = 0;
        memory[i].R = 0;
        memory[i].M = 0;
        memory[i].is_page_table = 0;
    }
}

void init_page_table(PageTable *pt)
{
    for (int i = 0; i < MAX_PAGES; i++)
    {
        pt->pages[i].valid = 0;
        pt->pages[i].frame_number = -1;
        pt->pages[i].R = 0;
        pt->pages[i].M = 0;
    }
}

// find free frame
int allocateFrame()
{
    for (int i = 0; i < FRAME_COUNT; i++)
        if (!memory[i].occupied)
            return i;

    return -1;
}

// NRU replacement
int nru_replace()
{
    for (int class = 0; class < 4; class++)
    {
        for (int i = 0; i < FRAME_COUNT; i++)
        {
            if (memory[i].is_page_table)
                continue;

            int current_class = 2 * memory[i].R + memory[i].M;

            if (current_class == class)
            {
                printf("[MMU] NRU chose frame %d (class %d)\n", i, class);
                return i;
            }
        }
    }
    return 0;
}

void clear_R_bits()
{
    printf("[MMU] Clearing all R bits\n");
    for (int i = 0; i < FRAME_COUNT; i++)
        memory[i].R = 0;
}