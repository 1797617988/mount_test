#include <stdio.h>

int myqsort(int arr[], int len)
{
    int i, j, k;
    for(i = 0; i < len-1; i++) {
        k = i;
        for(j = i+1; j < len; j++) {
            if(arr[j] < arr[k]) {
                k = j;
            }
        }

        /*比较并交换*/
        if(k != i) {
            int temp  = arr[k];
            arr[k] = arr[i];
            arr[i] = temp;
        }
    }

    return 0;
}

/*
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
*/