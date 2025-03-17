Here is a program that simulates splitting a network stream into a linked list of fixed-size buffers. <br>
This program processes incoming network data (simulated here as an array of bytes) and stores it in a linked list of fixed-size buffers.<br>
```c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define FIXED_BUFF_LEN 32

struct fixed_buff {
    uint8_t data[FIXED_BUFF_LEN];
    unsigned int data_len;
    struct fixed_buff *next;
};

struct stream {
    uint8_t *buffer;
    unsigned int length;
    unsigned int offset;
};

uint8_t* stream_get(struct stream *s, unsigned int *data_len) {
    if (s->offset >= s->length) {
        return NULL;
    }

    *data_len = (s->length - s->offset);
    uint8_t *data_ptr = s->buffer + s->offset;
    s->offset += *data_len;
    
    return data_ptr;
}

struct fixed_buff* build_fixed_buff_list(struct stream *s) {
    struct fixed_buff *head = NULL, *tail = NULL;
    uint8_t *curr_data;
    unsigned int curr_data_len;

    while ((curr_data = stream_get(s, &curr_data_len)) != NULL) {
        while (curr_data_len > 0) {
            struct fixed_buff *new_node = (struct fixed_buff *)malloc(sizeof(struct fixed_buff));
            if (new_node == NULL) {
                perror("Memory allocation failed");
                return NULL;
            }
            new_node->data_len = (curr_data_len > FIXED_BUFF_LEN) ? FIXED_BUFF_LEN : curr_data_len;
            memcpy(new_node->data, curr_data, new_node->data_len);
            new_node->next = NULL;
            if (head == NULL) {
                head = tail = new_node; // First node
            } else {
                tail->next = new_node;  // Append to the last node
                tail = new_node;        // Move tail
            }
            curr_data += new_node->data_len;
            curr_data_len -= new_node->data_len;
        }
    }
    return head;
}

void print_fixed_buff_list(struct fixed_buff *head) {
    struct fixed_buff *current = head;
    int index = 1;
    
    while (current != NULL) {
        printf("Buffer %d: data_len = %u, data = [", index++, current->data_len);
        for (unsigned int i = 0; i < current->data_len; i++) {
            printf(" %02X", current->data[i]); // Print as hex
        }
        printf(" ]\n");
        current = current->next;
    }
}

void free_fixed_buff_list(struct fixed_buff *head) {
    struct fixed_buff *current = head;
    while (current != NULL) {
        struct fixed_buff *temp = current;
        current = current->next;
        free(temp);
    }
}

int main() {
    // Simulated input data (50 bytes + 20 bytes)
    uint8_t sample_data[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
        0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
        0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
        0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C,
        0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46
    };

    struct stream s = { 
        .buffer = sample_data, 
        .length = sizeof(sample_data), 
        .offset = 0 
    };

    struct fixed_buff *head = build_fixed_buff_list(&s);
    print_fixed_buff_list(head);
    free_fixed_buff_list(head);

    return 0;
}
```
Output
```
Buffer 1: data_len = 32, data = [ 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 ]
Buffer 2: data_len = 32, data = [ 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F 40 ]
Buffer 3: data_len = 6, data = [ 41 42 43 44 45 46 ]
```
