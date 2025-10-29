
#include <stdio.h>

extern int myqsort(int arr[], int len);

int main()
{
    int i = 0;
    int arr[] = {5 , 9, 25, 10, 6, 2, 36, 75, 20, 3};

    myqsort(arr, sizeof(arr) / sizeof(int));
    for(i = 0; i <  sizeof(arr) / sizeof(int); i++) {
        printf("%d  ", arr[i]);
    }
    printf("\n");
    return 0; 
}