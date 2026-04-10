#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>
#include <stdio.h>

typedef struct QNode {
    int id;
    int priority;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *front;
    QNode *rear;
    int size;
} Queue;

static inline Queue *createQueue() {
    Queue *q = (Queue *)malloc(sizeof(Queue));
    q->front = q->rear = NULL;
    q->size = 0;
    return q;
}

static inline int isEmpty(Queue *q) {
    return q->size == 0;
}

static inline void enqueue(Queue *q, int id) {
    QNode *node = (QNode *)malloc(sizeof(QNode));
    node->id = id;
    node->next = NULL;

    if (q->rear == NULL) {
        q->front = q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }

    q->size++;
}

static inline void enqueuePri(Queue *q, int id, int pri) {
    QNode *node = (QNode *)malloc(sizeof(QNode));
    node->id = id;
    node->priority = pri;
    node->next = NULL;
    if (q->front == NULL) 
    {
        q->front = q->rear = node;
        q->size++;
        return;
    }
    if (pri < q->front->priority) 
    {
        node->next = q->front;
        q->front = node;
        q->size++;
        return;
    }
    QNode *cur = q->front;
    while (cur->next != NULL && cur->next->priority <= pri) 
    {
        cur = cur->next;
    }
    node->next = cur->next;
    cur->next = node;
    if (node->next == NULL) 
    {
        q->rear = node;
    }
    q->size++;
}

static inline int dequeue(Queue *q) {
    if (q->front == NULL) return -1;

    QNode *temp = q->front;
    int id = temp->id;

    q->front = q->front->next;
    if (q->front == NULL)
        q->rear = NULL;

    free(temp);
    q->size--;
    return id;
}

static inline void printQueue(Queue *q)
{
    QNode *cur = q->front;
    printf("Queue: ");
    while (cur)
    {
        printf("P%d(%d) -> ", cur->id, cur->priority);
        cur = cur->next;
    }
    printf("NULL\n");
}

static inline void freeQueue(Queue *q) {
    while (!isEmpty(q)) dequeue(q);
    free(q);
}

#endif