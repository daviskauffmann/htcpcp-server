#include "queue.h"

#include <malloc.h>
#include <stddef.h>

void enqueue(struct queue *queue, void *data)
{
    struct node *node = malloc(sizeof(*node));
    node->data = data;
    node->next = NULL;
    if (queue->tail)
    {
        queue->tail->next = node;
    }
    else
    {
        queue->head = node;
    }
    queue->tail = node;
}

void *dequeue(struct queue *queue)
{
    if (queue->head)
    {
        struct node *node = queue->head;
        queue->head = queue->head->next;
        if (!queue->head)
        {
            queue->tail = NULL;
        }
        void *data = node->data;
        free(node);
        return data;
    }

    return NULL;
}
