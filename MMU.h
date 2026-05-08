#ifndef MMU_H
#define MMU_H
#include "headers.h"
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
    int loading; 
} Frame;

extern Frame memory[FRAME_COUNT];

void initMemory();
void init_page_table(PageTable *pt);
int allocateFrame();
int selectVictimNRU();
void clear_R_bits();
int handlePageFault(PCB *p, int va, char mode, FILE *memFile, int *out_disk_ticks);
void createPageTable(PCB *p, FILE *memFile);
void loadFirstPage(PCB *p, FILE *memFile);
void freeProcessMemory(PCB *p);
void swapOut(int frame, FILE *memFile);
void swapIn(PCB *p, int page, int frame, char mode, FILE *memFile);
int translateAddress(PCB *p, int page);
int handleMemoryRequest(PCB *p, int va, char mode, FILE *memFile, int *out_frame, int *out_disk_ticks);
int allocateAndLoadPageImmediate(PCB *p, int page, char mode, FILE *memFile);
PCB *getPCB(int id);

#endif