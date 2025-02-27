// #include "../src/bgc.h"

#include "../src/bgc.c"

struct Vector3 {
    float x;
    float y;
    float z;
};
typedef struct Vector3 Vector3;

struct String {
    size_t length;
    char *data;
};
typedef struct String String;

struct Entity {
    String *name;
    Vector3 position;
};
typedef struct Entity Entity;

void do_something()
{
    bgcx_var(Entity, x);
    // or:  var(Entity, x); // C only (C++ not supported)

    x->name = bgcx_new(String);
    // or:  x->name = new(String); // C only (C++ not supported)

    bgc_Array *some_data = bgcx_array(size_t, 1024 * 1024 * 100);

    ((int *) some_data)[0] = 10;
    ((int *) some_data)[1] = 42;

    // DEBUG: Uncomment the following lines to print.
    // printf("%i\n", ((int *) some_data)[0]);
    // printf("%i\n", ((int *) some_data)[1]);
    // exit(0);

    bgc_Array *input = bgcx_array(float, 2);
    bgc_Array *hidden = bgcx_array(float, 3);
    bgc_Array *output = bgcx_array(float, 1);
}

void do_lots_of_things()
{
    int total_iterations = 1000000;

    for (int i = 0; i < total_iterations; i++)
    {
        do_something();
    }
}

int main(int argc, char **argv) {
    bgcx_start();

    do_lots_of_things();

    bgcx_stop();
}
