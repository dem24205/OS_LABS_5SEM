#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node *node_create(const char *str) {
    Node *node = malloc(sizeof(Node));
    if (!node) {
        printf("Cannot allocate memory for a node\n");
        abort();
    }

    strncpy(node->value, str, MAX_LEN - 1);
    node->value[MAX_LEN - 1] = '\0';
    node->next = NULL;

    node_lock_init(node);

    return node;
}

static void random_string(char *buf, unsigned int *seed) {
    int len = 1 + rand_r(seed) % (MAX_LEN - 1); //1...99

	for (int i = 0; i < len; ++i)
		buf[i] = 'a' + rand_r(seed) % 26;  //a...z

	buf[len] = '\0';
}

Storage *storage_init(long n) {
    unsigned int seed = 12345;

    Storage *s = malloc(sizeof(Storage));
    if (!s) {
        printf("Cannot allocate memory for a storage\n");
        abort();
    }

    s->first = node_create(""); //фиктивная голова
    Node *last = s->first;

    for (long i = 0; i < n; ++i) {
        char buf[MAX_LEN];
        random_string(buf, &seed);

        Node *node = node_create(buf);
        last->next = node;
        last = node;
    }

    return s;
}

