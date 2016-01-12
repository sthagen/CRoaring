/*
 * array.c
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <x86intrin.h>

#include "array.h"


enum{DEFAULT_INIT_SIZE = 16};




/* Create a new array. Return NULL in case of failure. */
array_container_t *array_container_create() {
	array_container_t * arr;
	/* Allocate the array container itself. */

	if ((arr = malloc(sizeof(array_container_t))) == NULL) {
				return NULL;
	}
	if ((arr->array = malloc(sizeof(uint16_t) * DEFAULT_INIT_SIZE)) == NULL) {
		        free(arr);
				return NULL;
	}
	arr->capacity = DEFAULT_INIT_SIZE;
	arr->cardinality = 0;
	return arr;
}

/* Free memory. */
void array_container_free(array_container_t *arr) {
	free(arr->array);
	arr->array = NULL;
	free(arr);
}


#define BRANCHLESSBINSEARCH // optimization (branchless with prefetching tends to be fast!)

#ifdef BRANCHLESSBINSEARCH

// could potentially use SIMD-based bin. search
static int32_t binarySearch(uint16_t* source, int32_t n, uint16_t target) {
	uint16_t * base = source;
    if(n == 0) return -1;
    while(n>1) {
    	int32_t half = n >> 1;
        __builtin_prefetch(base+(half>>1),0,0);
        __builtin_prefetch(base+half+(half>>1),0,0);
        base = (base[half] < target) ? &base[half] : base;
        n -= half;
    }
    // todo: over last cache line, you can just scan or use SIMD instructions
    base += *base < target;
    return *base == target ? base - source : source - base -1;
}


#else

// good old bin. search ending with a sequential search
// could potentially use SIMD-based bin. search
static int32_t binarySearch(uint16_t * array, int32_t lenarray, uint16_t ikey )  {
	int32_t low = 0;
	int32_t high = lenarray - 1;
	while( low+16 <= high) {
		int32_t middleIndex = (low+high) >> 1;
		int32_t middleValue = array[middleIndex];
		if (middleValue < ikey) {
			low = middleIndex + 1;
		} else if (middleValue > ikey) {
			high = middleIndex - 1;
		} else {
			return middleIndex;
		}
	}
	for (; low <= high; low++) {
		uint16_t val = array[low];
		if (val >= ikey) {
			if (val == ikey) {
				return low;
			}
			break;
		}
	}
	return -(low + 1);
}

#endif

/**
 * increase capacity to at least min, and to no more than max. Whether the
 * existing data needs to be copied over depends on copy. If copy is false,
 * then the new content might be uninitialized.
 */
static void increaseCapacity(array_container_t *arr, int32_t min, int32_t max, bool copy) {
    int32_t newCapacity = (arr->capacity == 0) ? DEFAULT_INIT_SIZE :
    		arr->capacity < 64 ? arr->capacity * 2
            : arr->capacity < 1024 ? arr->capacity * 3 / 2
            : arr->capacity * 5 / 4;
    if(newCapacity < min) newCapacity = min;
    // never allocate more than we will ever need
    if (newCapacity > max)
        newCapacity = max;
    // if we are within 1/16th of the max, go to max
    if( newCapacity < max - max/16)
        newCapacity = max;
    arr->capacity = newCapacity;
    if(copy)
      arr->array = realloc(arr->array,arr->capacity) ;
    else {
      free(arr->array);
      arr->array = malloc(arr->capacity) ;
    }
    // TODO: handle the case where realloc fails
    if(arr->array == NULL) {
    	printf("Well, that's unfortunate. Did I say you could use this code in production?\n");
    }
}

static void append(array_container_t *arr, uint16_t i) {
	if(arr->cardinality == arr->capacity) increaseCapacity(arr,arr->capacity+1,INT32_MAX, true);
	arr->array[arr->cardinality] = i;
	arr->cardinality++;
}

/* Add x to the set. Returns true if x was not already present.  */
bool array_container_add(array_container_t *arr, uint16_t x) {
	if (( (arr->cardinality > 0) && (arr->array[arr->cardinality-1] < x)) || (arr->cardinality == 0)) {
		append(arr, x);
		return true;
	}

	int32_t loc = binarySearch(arr->array,arr->cardinality, x);
	if (loc < 0) {// not already present
		if(arr->capacity == arr->capacity) increaseCapacity(arr,arr->capacity+1,INT32_MAX,true);
		int32_t i = -loc - 1;
		memmove(arr->array + i + 1,arr->array + i,(arr->cardinality - i)*sizeof(uint16_t));
		arr->array[i] = x;
		arr->cardinality ++;
		return true;
	} else return false;
}

/* Remove x from the set. Returns true if x was present.  */
bool array_container_remove(array_container_t *arr, uint16_t x) {
	int32_t loc = binarySearch(arr->array,arr->cardinality, x);
	if (loc >= 0) {
		memmove(arr->array + loc ,arr->array + loc + 1,(arr->cardinality - loc) * sizeof(uint16_t));
		arr->cardinality --;
		return true;
	} else return false;
}

/* Check whether x is present.  */
bool array_container_contains(const array_container_t *arr, uint16_t x) {
	int32_t loc = binarySearch(arr->array,arr->cardinality,x);
	return loc >= 0; // could possibly be faster...
}



// TODO: can one vectorize the computation of the union?
static int32_t union2by2(uint16_t * set1, int32_t lenset1,
		uint16_t * set2, int32_t lenset2, uint16_t * buffer){
	int32_t pos = 0;
	int32_t k1 = 0;
	int32_t k2 = 0;
	if (0 == lenset2) {
		memcpy(buffer,set1,lenset1*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
		return lenset1;
	}
	if (0 == lenset1) {
		memcpy(buffer,set2,lenset2*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
		return lenset2;
	}
	uint16_t s1 = set1[k1];
	uint16_t s2 = set2[k2];
	while(1) {
		if (s1 < s2) {
			buffer[pos] = s1;
			pos++;
			k1++;
			if (k1 >= lenset1) {
				memcpy(buffer + pos,set2 + k2,(lenset2-k2)*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
				pos += lenset2 - k2;
				break;
			}
			s1 = set1[k1];
		} else if (s1 == s2) {
			buffer[pos] = s1;
			pos++;
			k1++;
			k2++;
			if (k1 >= lenset1) {
				memcpy(buffer + pos,set2 + k2,(lenset2-k2)*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
				pos += lenset2 - k2;
				break;
			}
			if (k2 >= lenset2) {
				memcpy(buffer + pos,set1 + k1,(lenset1-k1)*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
				pos += lenset1 - k1;
				break;
			}
			s1 = set1[k1];
			s2 = set2[k2];
		} else { // if (set1[k1]>set2[k2])
			buffer[pos] = s2;
			pos++;
			k2++;
			if (k2 >= lenset2) {
				// should be memcpy
				memcpy(buffer + pos,set1 + k1,(lenset1-k1)*sizeof(uint16_t));// is this safe if buffer is set1 or set2?
				pos += lenset1 - k1;
				break;
			}
			s2 = set2[k2];
		}
	}
	return pos;
}





/* Computes the union of array1 and array2 and write the result to arrayout.
 * It is assumed that arrayout is distinct from both array1 and array2.
 */
void array_container_union(const array_container_t *array1,
                        const array_container_t *array2,
                        array_container_t *arrayout) {
	int32_t totc = array1->cardinality +  array2->cardinality ;
	if(arrayout->capacity < totc)
		increaseCapacity(arrayout,totc,INT32_MAX,false);
	arrayout->cardinality =  union2by2(array1->array, array1->cardinality,
			array2->array, array2->cardinality, arrayout->array);
}







#ifdef USEAVX

// used by intersect_vector16
static const uint8_t shuffle_mask16[] __attribute__((aligned(0x1000))) = { -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 2, 3, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 2, 3, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 6, 7, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 6,
		7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, -1,
		-1, -1, -1, -1, -1, -1, -1, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 2, 3, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 2, 3, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 8, 9, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 8, 9, -1, -1, -1, -1, -1, -1, -1,
		-1, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6,
		7, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, -1, -1, -1,
		-1, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4,
		5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, -1, -1, -1, -1, -1, -1, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, 2, 3, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 2, 3, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,
		5, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5,
		10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 10, 11, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 10, 11, -1, -1,
		-1, -1, -1, -1, -1, -1, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, 0, 1, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, 2, 3, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2,
		3, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 10, 11, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6, 7, 10, 11, -1, -1,
		-1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 10, 11, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, -1, -1, -1, -1, -1, -1, 8,
		9, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 8, 9,
		10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 8, 9, 10, 11, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 8, 9, 10, 11, -1, -1,
		-1, -1, -1, -1, -1, -1, 4, 5, 8, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 4, 5, 8, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, 2,
		3, 4, 5, 8, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
		8, 9, 10, 11, -1, -1, -1, -1, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1,
		-1, -1, -1, -1, 2, 3, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1,
		-1, 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1, -1, -1, 4, 5, 6, 7,
		8, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6, 7, 8, 9,
		10, 11, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1,
		-1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 12, 13,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 12, 13, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 12, 13, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 12, 13, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, 2, 3, 4, 5, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, 0, 1, 2, 3, 4, 5, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 6, 7, 12,
		13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 12, 13,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 12, 13, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 12, 13, -1, -1, -1, -1,
		-1, -1, -1, -1, 4, 5, 6, 7, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, 0, 1, 4, 5, 6, 7, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4,
		5, 6, 7, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7,
		12, 13, -1, -1, -1, -1, -1, -1, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 2, 3, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 2, 3, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 12,
		13, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 8, 9, 12, 13, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1,
		-1, -1, 6, 7, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 6, 7, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 12, 13,
		-1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, 12, 13, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 4, 5, 6, 7, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, 2,
		3, 4, 5, 6, 7, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 12, 13, -1, -1, -1, -1, 10, 11, 12, 13, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 10, 11, 12, 13, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 2, 3, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 2, 3, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1,
		4, 5, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4,
		5, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 10, 11,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 10, 11, 12,
		13, -1, -1, -1, -1, -1, -1, 6, 7, 10, 11, 12, 13, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 6, 7, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1,
		-1, -1, 2, 3, 6, 7, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 2, 3, 6, 7, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 10,
		11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6, 7, 10, 11,
		12, 13, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, -1,
		-1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, -1, -1, -1,
		-1, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1,
		8, 9, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 8, 9, 10,
		11, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 8, 9, 10, 11,
		12, 13, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1,
		-1, -1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, 0, 1,
		2, 3, 4, 5, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, 6, 7, 8, 9, 10, 11,
		12, 13, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 8, 9, 10, 11, 12,
		13, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, -1, -1,
		-1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1,
		4, 5, 6, 7, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6,
		7, 8, 9, 10, 11, 12, 13, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
		12, 13, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
		-1, -1, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 14,
		15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 14, 15,
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 14, 15, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 2, 3, 4, 5, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 2, 3, 4, 5, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 6, 7,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 14,
		15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 14, 15, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, 4, 5, 6, 7, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 4, 5, 6, 7, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3,
		4, 5, 6, 7, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6,
		7, 14, 15, -1, -1, -1, -1, -1, -1, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 2, 3, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 2, 3, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 14,
		15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 8, 9, 14, 15, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 8, 9, 14, 15, -1, -1, -1, -1,
		-1, -1, 6, 7, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 6, 7, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 14, 15,
		-1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 4, 5, 6, 7, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, 2,
		3, 4, 5, 6, 7, 8, 9, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 14, 15, -1, -1, -1, -1, 10, 11, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 10, 11, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 2, 3, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 2, 3, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		4, 5, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4,
		5, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 10, 11,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 10, 11, 14,
		15, -1, -1, -1, -1, -1, -1, 6, 7, 10, 11, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 6, 7, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1,
		-1, -1, 2, 3, 6, 7, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 2, 3, 6, 7, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, 4, 5, 6, 7, 10,
		11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6, 7, 10, 11,
		14, 15, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 10, 11, 14, 15, -1,
		-1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 14, 15, -1, -1, -1,
		-1, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1,
		8, 9, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 8, 9, 10,
		11, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 8, 9, 10, 11,
		14, 15, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9, 10, 11, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1,
		-1, -1, 2, 3, 4, 5, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1,
		2, 3, 4, 5, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1, 6, 7, 8, 9, 10, 11,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 8, 9, 10, 11, 14,
		15, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9, 10, 11, 14, 15, -1, -1,
		-1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1,
		4, 5, 6, 7, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6,
		7, 8, 9, 10, 11, 14, 15, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
		14, 15, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15,
		-1, -1, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 12,
		13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 12, 13,
		14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 4, 5, 12, 13, 14, 15, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 12, 13, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, 2, 3, 4, 5, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 2, 3, 4, 5, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 6, 7,
		12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 12,
		13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 12, 13, 14, 15,
		-1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 12, 13, 14, 15, -1,
		-1, -1, -1, -1, -1, 4, 5, 6, 7, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1,
		-1, -1, 0, 1, 4, 5, 6, 7, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 2, 3,
		4, 5, 6, 7, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6,
		7, 12, 13, 14, 15, -1, -1, -1, -1, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, 0, 1, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, 2, 3, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
		0, 1, 2, 3, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 4, 5, 8, 9,
		12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 12,
		13, 14, 15, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 8, 9, 12, 13, 14, 15,
		-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 8, 9, 12, 13, 14, 15, -1, -1,
		-1, -1, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0,
		1, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9,
		12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 12, 13,
		14, 15, -1, -1, -1, -1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1,
		-1, -1, -1, 0, 1, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, 2,
		3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 12, 13, 14, 15, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, 0, 1, 10, 11, 12, 13, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, 2, 3, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1,
		-1, -1, -1, 0, 1, 2, 3, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1,
		4, 5, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1, 4,
		5, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 2, 3, 4, 5, 10, 11,
		12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 10, 11, 12,
		13, 14, 15, -1, -1, -1, -1, 6, 7, 10, 11, 12, 13, 14, 15, -1, -1, -1,
		-1, -1, -1, -1, -1, 0, 1, 6, 7, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1,
		-1, -1, 2, 3, 6, 7, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0,
		1, 2, 3, 6, 7, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, 4, 5, 6, 7, 10,
		11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 4, 5, 6, 7, 10, 11,
		12, 13, 14, 15, -1, -1, -1, -1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14,
		15, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, -1,
		-1, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1,
		8, 9, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 2, 3, 8, 9, 10,
		11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 8, 9, 10, 11,
		12, 13, 14, 15, -1, -1, -1, -1, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, -1,
		-1, -1, -1, -1, -1, 0, 1, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1,
		-1, -1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, 0, 1,
		2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, 6, 7, 8, 9, 10, 11,
		12, 13, 14, 15, -1, -1, -1, -1, -1, -1, 0, 1, 6, 7, 8, 9, 10, 11, 12,
		13, 14, 15, -1, -1, -1, -1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		-1, -1, -1, -1, 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1,
		4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, 0, 1, 4, 5, 6,
		7, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
		12, 13, 14, 15, -1, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
		14, 15 };

/**
 * From Schlegel et al., Fast Sorted-Set Intersection using SIMD Instructions
 *
 * Optimized by D. Lemire on May 3rd 2013
 *
 * Question: Can this benefit from AVX?
 */
static int32_t intersect_vector16(const uint16_t *A, int32_t s_a,
		const uint16_t *B, int32_t s_b, uint16_t * C) {
	int32_t count = 0;
	int32_t i_a = 0, i_b = 0;

	const int32_t st_a = (s_a / 8) * 8;
	const int32_t st_b = (s_b / 8) * 8;
	__m128i v_a, v_b;
	if ((i_a < st_a) && (i_b < st_b)) {
		v_a = _mm_lddqu_si128((__m128i *) &A[i_a]);
		v_b = _mm_lddqu_si128((__m128i *) &B[i_b]);
		while ((A[i_a] == 0) || (B[i_b] == 0)) {
			const __m128i res_v = _mm_cmpestrm(v_b, 8, v_a, 8,
					_SIDD_UWORD_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK);
			const int r = _mm_extract_epi32(res_v, 0);
			__m128i sm16 = _mm_load_si128((const __m128i *) shuffle_mask16 + r);
			__m128i p = _mm_shuffle_epi8(v_a, sm16);
			_mm_storeu_si128((__m128i *) &C[count], p);
			count += _mm_popcnt_u32(r);
			const uint16_t a_max = A[i_a + 7];
			const uint16_t b_max = B[i_b + 7];
			if (a_max <= b_max) {
				i_a += 8;
				if (i_a == st_a)
					break;
				v_a = _mm_lddqu_si128((__m128i *) &A[i_a]);

			}
			if (b_max <= a_max) {
				i_b += 8;
				if (i_b == st_b)
					break;
				v_b = _mm_lddqu_si128((__m128i *) &B[i_b]);

			}
		}
		if ((i_a < st_a) && (i_b < st_b))
			while (true) {
				const __m128i res_v = _mm_cmpistrm(v_b, v_a,
						_SIDD_UWORD_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK);
				const int r = _mm_extract_epi32(res_v, 0);
				__m128i sm16 = _mm_load_si128(
						(const __m128i *) shuffle_mask16 + r);
				__m128i p = _mm_shuffle_epi8(v_a, sm16);
				_mm_storeu_si128((__m128i *) &C[count], p);
				count += _mm_popcnt_u32(r);
				const uint16_t a_max = A[i_a + 7];
				const uint16_t b_max = B[i_b + 7];
				if (a_max <= b_max) {
					i_a += 8;
					if (i_a == st_a)
						break;
					v_a = _mm_lddqu_si128((__m128i *) &A[i_a]);

				}
				if (b_max <= a_max) {
					i_b += 8;
					if (i_b == st_b)
						break;
					v_b = _mm_lddqu_si128((__m128i *) &B[i_b]);

				}
			}
	}
	// intersect the tail using scalar intersection
	while (i_a < s_a && i_b < s_b) {
		int16_t a = A[i_a];
		int16_t b = B[i_b];
		if (a < b) {
			i_a++;
		} else if (b < a) {
			i_b++;
		} else {
			C[count] = a;		//==b;
			count++;
			i_a++;
			i_b++;
		}
	}

	return count;
}




int32_t match_scalar(const uint16_t *A, const int32_t lenA,
                    const uint16_t *B, const int32_t lenB,
                    uint16_t *out) {

    const uint16_t *initout = out;
    if (lenA == 0 || lenB == 0) return 0;

    const uint16_t *endA = A + lenA;
    const uint16_t *endB = B + lenB;

    while (1) {
        while (*A < *B) {
SKIP_FIRST_COMPARE:
            if (++A == endA) goto FINISH;
        }
        while (*A > *B) {
            if (++B == endB) goto FINISH;
        }
        if (*A == *B) {
            *out++ = *A;
            if (++A == endA || ++B == endB) goto FINISH;
        } else {
            goto SKIP_FIRST_COMPARE;
        }
    }

FINISH:
    return (out - initout);
}

/**
 * Intersections scheme designed by N. Kurz that works very
 * well when intersecting an array with another where the density
 * differential is small (between 2 to 10).
 *
 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The matchOut pointer can safely be equal to the rare pointer.
 *
 */
int32_t v1(const uint16_t *rare, int32_t lenRare, const uint16_t *freq,
		int32_t lenFreq, uint16_t *matchOut) {
	assert(lenRare <= lenFreq);
	const uint16_t *matchOrig = matchOut;
	if (lenFreq == 0 || lenRare == 0)
	return 0;
	const int32_t numberofintspervec = sizeof(__m128i)/ sizeof(uint16_t);

	const uint64_t kFreqSpace = numberofintspervec - 1;

	const uint16_t *stopFreq = &freq[lenFreq] - kFreqSpace;
	const uint16_t *stopRare = &rare[lenRare];

	__m128i Rare;

	__m128i F0;

	if (((rare >= stopRare) || (freq >= stopFreq))) goto FINISH_SCALAR;
	uint16_t valRare;
	valRare = rare[0];
	Rare = _mm_set1_epi16(valRare);

	uint16_t maxFreq;
	maxFreq = freq[kFreqSpace];
	F0 = _mm_lddqu_si128((const __m128i *)(freq));

	if ((maxFreq < valRare))
	goto ADVANCE_FREQ;

	ADVANCE_RARE: do {
		*matchOut = valRare;
		valRare = rare[1]; // for next iteration
		rare += 1;
		if ((rare >= stopRare)) {
			rare -= 1;
			goto FINISH_SCALAR;
		}
		F0 = _mm_cmpeq_epi16(F0, Rare);
		Rare = _mm_set1_epi16(valRare);
		//F0 = _mm_or_si128(F0, F1);
		if(_mm_testz_si128(F0,F0) == 0)
		matchOut++;
		F0 = _mm_lddqu_si128((const __m128i *)(freq));

	}while (maxFreq >= valRare);

	uint16_t maxProbe;

	ADVANCE_FREQ: do {
		const uint16_t *probeFreq = freq + numberofintspervec;
		maxProbe = freq[kFreqSpace];

		if ((probeFreq >= stopFreq)) {
			goto FINISH_SCALAR;
		}

		freq = probeFreq;

	}while (maxProbe < valRare);

	maxFreq = maxProbe;

	F0 = _mm_lddqu_si128((const __m128i *)(freq));

	goto ADVANCE_RARE;
	int32_t count;

	FINISH_SCALAR: count = matchOut - matchOrig;

	lenFreq = stopFreq + kFreqSpace - freq;
	lenRare = stopRare  - rare;

	size_t tail = match_scalar(freq, lenFreq, rare, lenRare, matchOut);

	return count + tail;
}


static int32_t intersectV1avx_vector16(const uint16_t *A, const int32_t s_a, const uint16_t *B,
		 const int32_t s_b, uint16_t *C) {
	if (s_a > s_b)
		return intersectV1avx_vector16(B, s_b, A, s_a, C);
	int32_t count = 0;
	int32_t i_a = 0, i_b = 0;
	const int32_t st_a = s_a;
	const int32_t howmanyvec = 2;
	const int32_t numberofintspervec = howmanyvec* sizeof(__m256i)/ sizeof(uint16_t);

	const int32_t st_b = (s_b / numberofintspervec) * numberofintspervec;
	__m256i v_b1,v_b2;
	if ((i_a < st_a) && (i_b < st_b)) {
		while(i_a < st_a) {
			uint16_t a = A[i_a];
			const __m256i v_a = _mm256_set1_epi16(a);
			while (B[i_b + numberofintspervec - 1] < a) {
				i_b += numberofintspervec;
				if (i_b == st_b)
					goto FINISH_SCALAR;
			}
			v_b1 = _mm256_lddqu_si256((const __m256i *) &B[i_b]);
			v_b2 = _mm256_lddqu_si256((const __m256i *) &B[i_b] + 1);

			__m256i F0 = _mm256_cmpeq_epi16(v_a, v_b1);
			__m256i F1 = _mm256_cmpeq_epi16(v_a, v_b2);
			F0 = _mm256_or_si256(F0,F1);
			count += !_mm256_testz_si256(F0, F0);
			C[count] = a;
			++i_a;
		}
	}
	FINISH_SCALAR:
	// intersect the tail using scalar intersection
	while (i_a < s_a && i_b < s_b) {
		if (A[i_a] < B[i_b]) {
			i_a++;
		} else if (B[i_b] < A[i_a]) {
			i_b++;
		} else {
			C[count] = A[i_a];
			count++;
			i_a++;
			i_b++;
		}
	}
	return count;
}


static int32_t intersectV2avx_vector16(const uint16_t *A, const int32_t s_a, const uint16_t *B,
		 const int32_t s_b, uint16_t *C) {
	if (s_a > s_b)
		return intersectV1avx_vector16(B, s_b, A, s_a, C);
	int32_t count = 0;
	int32_t i_a = 0, i_b = 0;
	const int32_t st_a = s_a;
	const int32_t howmanyvec = 4;
	const int32_t numberofintspervec = howmanyvec* sizeof(__m256i)/ sizeof(uint16_t);

	const int32_t st_b = (s_b / numberofintspervec) * numberofintspervec;
	__m256i v_b1,v_b2,v_b3,v_b4;
	if ((i_a < st_a) && (i_b < st_b)) {
		while(i_a < st_a) {
			uint16_t a = A[i_a];
			const __m256i v_a = _mm256_set1_epi16(a);
			while (B[i_b + numberofintspervec - 1] < a) {
				i_b += numberofintspervec;
				if (i_b == st_b)
					goto FINISH_SCALAR;
			}
			v_b1 = _mm256_lddqu_si256((const __m256i *) &B[i_b]);
			v_b2 = _mm256_lddqu_si256((const __m256i *) &B[i_b] + 1);
			v_b3 = _mm256_lddqu_si256((const __m256i *) &B[i_b] + 2);
			v_b4 = _mm256_lddqu_si256((const __m256i *) &B[i_b] + 3);

			__m256i F0 = _mm256_cmpeq_epi16(v_a, v_b1);
			__m256i F1 = _mm256_cmpeq_epi16(v_a, v_b2);
			__m256i F2 = _mm256_cmpeq_epi16(v_a, v_b3);
			__m256i F3 = _mm256_cmpeq_epi16(v_a, v_b4);

			F0 = _mm256_or_si256(F0,F1);
			F2 = _mm256_or_si256(F2,F3);

			F0 = _mm256_or_si256(F0,F2);
			count += !_mm256_testz_si256(F0, F0);
			C[count] = a;
			++i_a;
		}
	}
	FINISH_SCALAR:
	// intersect the tail using scalar intersection
	while (i_a < s_a && i_b < s_b) {
		if (A[i_a] < B[i_b]) {
			i_a++;
		} else if (B[i_b] < A[i_a]) {
			i_b++;
		} else {
			C[count] = A[i_a];
			count++;
			i_a++;
			i_b++;
		}
	}
	return count;
}

int32_t intersection2by2(
		uint16_t * set1, int32_t lenset1,
		uint16_t * set2, int32_t lenset2,
		uint16_t * buffer) {
	const int32_t bigthres = 32; // TODO: adjust this threshold
	const int32_t thres = 4; // TODO: adjust this threshold
	if (lenset1 * bigthres < lenset2) {
        return intersectV2avx_vector16(set1, lenset1, set2, lenset2, buffer);
	} else if( lenset2 * bigthres < lenset1) {
		return intersectV2avx_vector16(set2, lenset2, set1, lenset1, buffer);
	} else if (lenset1 * thres < lenset2) {
        return intersectV1avx_vector16(set1, lenset1, set2, lenset2, buffer);
	} else if( lenset2 * thres < lenset1) {
		return intersectV1avx_vector16(set2, lenset2, set1, lenset1, buffer);
	} else {
		return intersect_vector16(set1,lenset1,set2,lenset2,buffer);
	}
}

#else


int32_t localintersect2by2(
	uint16_t * set1, int32_t lenset1,
	uint16_t * set2, int32_t lenset2,
	uint16_t * buffer)  {
	if ((0 == lenset1) || (0 == lenset2)) {
		return 0;
	}
	int32_t k1 = 0;
	int32_t k2 = 0;
	int32_t pos = 0;
	uint16_t s1 = set1[k1];
	uint16_t s2 = set2[k2];
	while(true) {
		if (s2 < s1) {
			while(true) {
				k2++;
				if (k2 == lenset2) {
					return pos;
				}
				s2 = set2[k2];
				if (s2 >= s1) {
					break;
				}
			}
		}
		if (s1 < s2) {
			while(true) {
				k1++;
				if (k1 == lenset1) {
					return pos;
				}
				s1 = set1[k1];
				if (s1 >= s2) {
					break;
				}
			}

		} else {
			// (set2[k2] == set1[k1])
			buffer[pos] = s1;
			pos++;
			k1++;
			if (k1 == lenset1) {
				break;
			}
			s1 = set1[k1];
			k2++;
			if (k2 == lenset2) {
				break;
			}
			s2 = set2[k2];
		}
	}
	return pos;
}

int32_t advanceUntil(
		uint16_t * array,
		int32_t pos,
		int32_t length,
		uint16_t min)  {
	int32_t lower = pos + 1;

	if ((lower >= length) || (array[lower] >= min)) {
		return lower;
	}

	int32_t spansize = 1;

	while ((lower+spansize < length) && (array[lower+spansize] < min)) {
		spansize <<= 1;
	}
	int32_t upper = (lower+spansize < length )?lower + spansize:length - 1;

	if (array[upper] == min) {
		return upper;
	}

	if (array[upper] < min) {
		// means
		// array
		// has no
		// item
		// >= min
		// pos = array.length;
		return length;
	}

	// we know that the next-smallest span was too small
	lower += (spansize >> 1);

	int32_t mid = 0;
	while( lower+1 != upper) {
		mid = (lower + upper) >> 1;
		if (array[mid] == min) {
			return mid;
		} else if (array[mid] < min) {
			lower = mid;
		} else {
			upper = mid;
		}
	}
	return upper;

}

int32_t onesidedgallopingintersect2by2(
		uint16_t * smallset, int32_t lensmallset,
		uint16_t * largeset, int32_t lenlargeset,	uint16_t * buffer)  {

	if( 0 == lensmallset) {
		return 0;
	}
	int32_t k1 = 0;
	int32_t k2 = 0;
	int32_t pos = 0;
	uint16_t s1 = largeset[k1];
	uint16_t s2 = smallset[k2];
    while(true) {
		if (s1 < s2) {
			k1 = advanceUntil(largeset, k1, lenlargeset, s2);
			if (k1 == lenlargeset) {
				break;
			}
			s1 = largeset[k1];
		}
		if (s2 < s1) {
			k2++;
			if (k2 == lensmallset) {
				break;
			}
			s2 = smallset[k2];
		} else {

			buffer[pos] = s2;
			pos++;
			k2++;
			if (k2 == lensmallset) {
				break;
			}
			s2 = smallset[k2];
			k1 = advanceUntil(largeset, k1, lenlargeset, s2);
			if (k1 == lenlargeset) {
				break;
			}
			s1 = largeset[k1];
		}

	}
	return pos;
}


int32_t intersection2by2(
	uint16_t * set1, int32_t lenset1,
	uint16_t * set2, int32_t lenset2,
	uint16_t * buffer)  {
	int32_t thres = 4; // TODO: adjust this threshold
	if (lenset1 * thres < lenset2) {
		return onesidedgallopingintersect2by2(set1, lenset1, set2, lenset2, buffer);
	} else if( lenset2 * thres < lenset1) {
		return onesidedgallopingintersect2by2(set2, lenset2, set1, lenset1, buffer);
	} else {
		return localintersect2by2(set1, lenset1, set2, lenset2, buffer);
	}
}

#endif // #ifdef USEAVX


/* computes the intersection of array1 and array2 and write the result to
 * arrayout.
 * It is assumed that arrayout is distinct from both array1 and array2.
 * */
void array_container_intersection(const array_container_t *array1,
                         const array_container_t *array2,
                         array_container_t *arrayout) {
	int32_t minc = array1->cardinality < array2->cardinality ? array1->cardinality : array2->cardinality;
	if(arrayout->capacity < minc)
		increaseCapacity(arrayout,minc,INT32_MAX,false);
	arrayout->cardinality =  intersection2by2(array1->array, array1->cardinality,
			array2->array, array2->cardinality, arrayout->array);
}
