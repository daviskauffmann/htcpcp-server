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

void add_kvp(struct kvp_list *kvp_list, const char *key, const char *value);
void remove_kvp(struct kvp_list *kvp_list, const char *key);
void free_kvp_list(struct kvp_list *kvp_list);

#endif
