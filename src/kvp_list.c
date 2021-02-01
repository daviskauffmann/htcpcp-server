#include "kvp_list.h"

#include <malloc.h>
#include <string.h>

void kvp_list_add(struct kvp_list *kvp_list, const char *key, const char *value)
{
    struct kvp kvp;
    kvp.key = strdup(key);
    kvp.value = strdup(value);

    kvp_list->items = realloc(kvp_list->items, (kvp_list->count + 1) * sizeof(*kvp_list->items));
    kvp_list->items[kvp_list->count++] = kvp;
}

void kvp_list_remove(struct kvp_list *kvp_list, const char *key)
{
    for (int i = 0; i < kvp_list->count; i++)
    {
        if (strcmp(kvp_list->items[i].key, key) == 0)
        {
            // TODO: remove current
            free(kvp_list->items[i].key);
            free(kvp_list->items[i].value);
            // TODO: move last to current
            // TODO: realloc to count - 1
        }
    }
}

void kvp_list_free(struct kvp_list *kvp_list)
{
    for (int i = 0; i < kvp_list->count; i++)
    {
        free(kvp_list->items[i].key);
        free(kvp_list->items[i].value);
    }
    if (kvp_list->items)
    {
        free(kvp_list->items);
    }
}
