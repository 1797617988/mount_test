
#include <stdio.h>
#include "mymath.h"
#include "myqsort.h"

int main()
{
    int i = 0;
    int arr[] = {5 , 9, 25, 10, 6, 2, 36, 75, 20, 3};

    printf("call myqsort-------------------------\n");
    myqsort(arr, sizeof(arr) / sizeof(int));
    for(i = 0; i <  sizeof(arr) / sizeof(int); i++) {
        printf("%d  ", arr[i]);
    }
    printf("\n");

    printf("call mymath-------------------------\n");

    printf("%d + %d = %d\n", 5 , 6 , add(5,6));
    printf("%d * %d = %d\n", 5 , 6 , mul(5,6));

    return 0;
}