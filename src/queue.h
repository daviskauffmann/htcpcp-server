#ifndef QUEUE_H
#define QUEUE_H

struct node
{
    void *data;
    struct node *next;
};

struct queue
{
    struct node *head;
    struct node *tail;
};

void enqueue(struct queue *queue, void *data);
void *dequeue(struct queue *queue);

#endif
