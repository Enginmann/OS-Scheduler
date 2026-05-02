#include "MMU.h"
#include <stdio.h>
#include <unistd.h>

Frame memory[FRAME_COUNT];

static void formatBinary(unsigned int value, char *out, size_t outSize)
{
    if (outSize == 0)
        return;
    if (value == 0)
    {
        if (outSize >= 2)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    char tmp[64];
    int idx = 0;
    while (value && idx < (int)sizeof(tmp) - 1)
    {
        tmp[idx++] = (value & 1U) ? '1' : '0';
        value >>= 1U;
    }
    tmp[idx] = '\0';

    // reverse into out
    size_t n = (size_t)idx;
    if (n + 1 > outSize)
        n = outSize - 1;

    for (size_t i = 0; i < n; i++)
        out[i] = tmp[idx - 1 - (int)i];
    out[n] = '\0';
}

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

int handlePageFault(PCB *p, int va, char mode, FILE *memFile)
{
    int page = va / PAGE_SIZE;
    char vaBin[64];
    formatBinary((unsigned int)va, vaBin, sizeof(vaBin));

    fprintf(memFile, "PageFault upon VA %s from process %d\n", vaBin, p->id);
    fflush(memFile);

    int frame = allocateFrame();

    if (frame == -1)
    {
        frame = selectVictimNRU();
        swapOut(frame, memFile);
    }
    else
    {
        fprintf(memFile, "Free Physical page %d allocated\n", frame);
        fflush(memFile);
    }

    // Reserve the selected frame immediately so it can't be allocated again
    // before the scheduler completes swapIn at unblock time.
    memory[frame].occupied = 1;
    memory[frame].process_id = p->id;
    memory[frame].page_number = page;
    memory[frame].R = 0;
    memory[frame].M = 0;

    // Scheduler will perform swapIn at unblock time.
    return frame;
}

int handleMemoryRequest(PCB *p, int va, char mode, FILE *memFile, int *out_frame)
{
    int page = va / PAGE_SIZE;

    int frame = translateAddress(p, page);

    if (frame != -1)
    {
        // HIT
        memory[frame].R = 1;
        if (mode == 'w')
            memory[frame].M = 1;
        if (out_frame)
            *out_frame = frame;
        return 1;
    }

    // MISS → PAGE FAULT
    frame = handlePageFault(p, va, mode, memFile);
    if (out_frame)
        *out_frame = frame;
    return 0;
}

int translateAddress(PCB *p, int page)
{
    PageTableEntry *e = &p->page_table.pages[page];

    if (e->valid)
        return e->frame_number;

    return -1;
}

void swapOut(int frame, FILE *memFile)
{
    if (memory[frame].M)
    {
        fprintf(memFile, "Swapping out page %d to disk\n", frame);
        fflush(memFile);
    }

    // invalidate old page table
    PCB *victim = getPCB(memory[frame].process_id);
    int page = memory[frame].page_number;

    victim->page_table.pages[page].valid = 0;
}

void swapIn(PCB *p, int page, int frame, char mode, FILE *memFile)
{
    memory[frame].occupied = 1;
    memory[frame].process_id = p->id;
    memory[frame].page_number = page;
    memory[frame].R = 1;
    memory[frame].M = (mode == 'w');

    p->page_table.pages[page].valid = 1;
    p->page_table.pages[page].frame_number = frame;

    fprintf(memFile,
        "At time %d disk address %d for process %d is loaded into memory page %d.\n",
        getClk(), p->base + page, p->id, frame);
    fflush(memFile);
}

void createPageTable(PCB *p, FILE *memFile)
{
    int frame = allocateFrame();

    if (frame == -1)
    {
        frame = selectVictimNRU();
        swapOut(frame, memFile);
    }

    memory[frame].occupied = 1;
    memory[frame].is_page_table = 1;
    p->page_table.page_table_frame = frame;

    init_page_table(&p->page_table);
}

void loadFirstPage(PCB *p, FILE *memFile)
{
    handlePageFault(p, 0, 'r', memFile);
}

void freeProcessMemory(PCB *p)
{
    for (int i = 0; i < MAX_PAGES; i++)
    {
        if (p->page_table.pages[i].valid)
        {
            int f = p->page_table.pages[i].frame_number;
            memory[f].occupied = 0;
        }
    }

    // free page table frame
    memory[p->page_table.page_table_frame].occupied = 0;
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
int selectVictimNRU()
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