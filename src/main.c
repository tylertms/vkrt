#include "app.h"
#include "vkrt.h"

#include <stdlib.h>

int main() {
    VKRT vkrt = {0};
    run(&vkrt);

    return EXIT_SUCCESS;
}
