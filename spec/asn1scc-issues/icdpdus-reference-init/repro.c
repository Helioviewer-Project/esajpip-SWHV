/* Initializes a Rec. Links only if Mask_Initialize, which Rec_Initialize
 * calls, was generated. */
#include <stdio.h>

#include "min.h"

int main(void) {
    Rec value;
    int error = 0;
    Rec_Initialize(&value);
    printf("Rec initialized, valid=%d\n", Rec_IsConstraintValid(&value, &error));
    return 0;
}
