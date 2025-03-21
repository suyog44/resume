```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node {
    char *data;
    struct node *next;
};

struct node *head = NULL;

void insert_front(const char *word, int len) {
    struct node *new_node = (struct node *)malloc(sizeof(struct node));
    new_node->data = (char *)malloc(len + 1);
    strncpy(new_node->data, word, len);
    new_node->data[len] = '\0';
    new_node->next = head;
    head = new_node;
}

void reverse_order(const char *string) {
    const char *start = string, *end = string;
    while (*end) {
        if (*end == ' ') {
            if (start != end) {
                insert_front(start, end - start);
            }
            start = end + 1;
        }
        end++;
    }
    if (start != end) {
        insert_front(start, end - start);
    }
}

void print_reverse() {
    struct node *temp = head;
    while (temp) {
        printf("%s ", temp->data);
        temp = temp->next;
    }
    printf("\n");
}

void free_list() {
    struct node *temp;
    while (head) {
        temp = head;
        head = head->next;
        free(temp->data);
        free(temp);
    }
}

int main() {
    const char input[] = "I am a C Programmer";
    reverse_order(input);
    print_reverse();
    free_list();
    return 0;
}
```
Output
```
I am a C Programmer
Programmer C a am I 
```

**Alternate Method**
```c
#include <stdio.h>
#include <string.h>

void reverse_order_print(const char *str) {
    int length = strlen(str);
    int end = length - 1;
    int first_word = 1;

    for (int i = length - 1; i >= 0; i--) {
        if (str[i] == ' ') {
            if (end != i) {
                if (!first_word) {
                    putchar(' ');
                }
                printf("%.*s", end - i, str + i + 1);
                first_word = 0;
            }
            end = i - 1;
        }
    }
    if (end >= 0) {
        if (!first_word) {
            putchar(' ');
        }
        printf("%.*s", end + 1, str);
    }
    printf("\n");
}

int main() {
    const char input[] = "I am a C Programmer";
    reverse_order_print(input);
    return 0;
}
```
