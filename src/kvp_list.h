#ifndef KVP_LIST_H
#define KVP_LIST_H

struct kvp
{
    char *key;
    char *value;
};

struct kvp_list
{
    int count;
    struct kvp *items;
};

void kvp_list_add(struct kvp_list *kvp_list, const char *key, const char *value);
void kvp_list_remove(struct kvp_list *kvp_list, const char *key);
void kvp_list_free(struct kvp_list *kvp_list);

#endif
