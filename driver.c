/*************************************************************************
> File Name: driver.c
> Author: TTc
> Mail: liutianshxkernel@gmail.com
> Created Time: 三 10/19 22:51:09 2016
************************************************************************/

#include <stdio.h>

extern int mymain(void);

int
sum2(int a, int b) {
    return a + b;
}

int
sum5(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}


int
main(int argc, char **argv) {
    printf("%d\n", mymain());
    return 0;
}

