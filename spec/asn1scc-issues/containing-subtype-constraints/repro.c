#include <stdio.h>

#include "min.h"

int main(void)
{
    byte invalid_wire[] = {1, 1};
    byte valid_wire[] = {1, 0};
    BitStream stream;
    W value;
    int err = 0;
    flag validity;
    flag invalid_decode;
    flag valid_decode;

    W_Initialize(&value);
    value.body.field = 1;
    validity = W_IsConstraintValid(&value, &err);

    W_Initialize(&value);
    BitStream_AttachBuffer(&stream, invalid_wire, sizeof invalid_wire);
    err = 0;
    invalid_decode = W_ACN_Decode(&value, &stream, &err);

    W_Initialize(&value);
    BitStream_AttachBuffer(&stream, valid_wire, sizeof valid_wire);
    err = 0;
    valid_decode = W_ACN_Decode(&value, &stream, &err);

    printf("invalid validity=%d decode=%d; valid decode=%d\n",
           validity, invalid_decode, valid_decode);
    return validity || invalid_decode || !valid_decode;
}
