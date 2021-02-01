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

void queue_enqueue(struct queue *queue, void *data);
void *queue_dequeue(struct queue *queue);

#endif
