**1. Find the maximum elements using pointers**
```c
#include <stdio.h>

int find_max(const int *arr, int len) {
    if (len <= 0) return -1;

    const int *ptr = arr;
    const int *max_ptr = arr;

    for (int i = 0; i < len; i++) {
        if (*(ptr + i) > *max_ptr) {
            max_ptr = ptr + i;
        }
    }

    return *max_ptr;
}

int main() {
    int numbers[] = {5, 9, 3, 7, 2, 8, 66};
    int len = sizeof(numbers) / sizeof(numbers[0]);

    printf("Maximum element: %d\n", find_max(numbers, len));
    return 0;
}
```
**Output**
```
Maximum element: 66
```

**2. Access the nibble (4bits) of character variable without shift operator**
```c
#include <stdio.h>

union bytes{
    unsigned char data;
    struct{
        unsigned char byte0:4;
        unsigned char byte1:4;
    }bits;
};

int main()
{
    union bytes mydata;
    mydata.data=0x12;
    printf("Orig data: 0x%x\n", mydata.data);
    printf("Byte0: 0x%x Byte1: 0x%x\n", mydata.bits.byte0, mydata.bits.byte1);
    return 0;
}
```
**Output**
```c
Orig data: 0x12
Byte0: 0x2 Byte1: 0x1
```
