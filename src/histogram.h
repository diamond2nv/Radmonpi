#ifndef HISTOGRAM_H_
#define HISTOGRAM_H_ 1.0

#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define histo_value_t uint32_t

typedef struct {
	histo_value_t * value;
	histo_value_t size;
} Histogram_t;

#define DEFAULT_HISTO {0,0};

int histo_add(Histogram_t *histo, histo_value_t value)
{
    if (histo->value == NULL){
        histo->size=1;
        histo->value = (histo_value_t *)malloc(sizeof(histo_value_t));
        if (histo->value == NULL)  return -1;
        histo->value[0] = 0;
    }
    if (value >= histo->size){
        int i = histo->size;
        histo->size = value+1;
        histo->value = (histo_value_t *)realloc(histo->value, (value+1)*sizeof(histo_value_t));
        for(;i<histo->size;i++)
            histo->value[i] = 0;
    }
    (histo->value[value])++;

    return 0;
}

int fprint_histo(char * filename,Histogram_t histogram)
{
    FILE * file = fopen(filename,"w");

    if (file == NULL) return -1;

    histo_value_t i;
    for(i=0;i<histogram.size;i++){
        fprintf(file,"%u\t%u\n",i,histogram.value[i]);
    }
    fclose(file);
    return 0;
}

int print_all_histo(Histogram_t histogram)
{
    int i =0;
    printf("%u\t%u\n",i++,histogram.value[0]);
    while(i<histogram.size){
        printf("%u\t%u\n",i,histogram.value[i]);
        if (histogram.value[i]==0 && histogram.value[i-1]!=0 && histogram.value[i+1] != 0)
            //It's safe here because of the Short-Circuit-Evaluation. The last value is always non-null
            printf("%u\t%u\n",i,histogram.value[i]);
        printf("%u\t%u\n",i,0); // I'm going to put a nice null value at the end
        i++;
    }
    return 0;
}

int print_histo(Histogram_t histogram)
{
    int i;
    for(i=0;i<histogram.size;i++){
        printf("%u\t%u\n",i,histogram.value[i]);
    }
    return 0;
}


#undef type_t

#endif