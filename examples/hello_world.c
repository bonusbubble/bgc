#include <stdio.h>
#include "../include/bgc.h"

bgc_Array * int_array(int length)
{
    bgc_Array *array = bgcx_array(int, length);

    for (int i = 0; i < length; i++) {
        bgcx_array_set(array, i, int, i);
    }

    return array;
}

void print_int_array(bgc_Array *array)
{
    int length = array->slot_count;

    for (int i = 0; i < length; i++) {
        int value = bgcx_array_get(array, i, int);

        printf("%i\n", value);
    }
}

int main()
{
    bgcx_start();

    int length = 10;

    bgc_Array *array = int_array(length);

    print_int_array(array);

    bgcx_stop();
}
