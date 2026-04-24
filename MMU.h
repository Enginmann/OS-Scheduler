#ifndef MMU_H
#define MMU_H

#define FRAME_COUNT 32
#define PAGE_SIZE 16
#define MAX_PAGES 1024

typedef struct
{
    int occupied;
    int process_id;
    int page_number;
    int R;
    int M;
    int is_page_table;
} Frame;

typedef struct
{
    int frame_number;
    int valid;
    int R;
    int M;
} PageTableEntry;

typedef struct
{
    PageTableEntry pages[MAX_PAGES];
    int page_table_frame;
} PageTable;

extern Frame memory[FRAME_COUNT];

void initMemory();
void init_page_table(PageTable *pt);
int allocateFrame();
int nru_replace();
void clear_R_bits();

#endif