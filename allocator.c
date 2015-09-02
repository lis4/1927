//
//  COMP1927 Assignment 1 - Vlad: the memory allocator
//  allocator.c ... implementation
//
//  Created by Liam O'Connor on 18/07/12.
//  Modified by John Shepherd in August 2014, August 2015
//  Copyright (c) 2012-2015 UNSW. All rights reserved.
//

#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>

#define HEADER_SIZE    sizeof(struct free_list_header)  
#define MAGIC_FREE     0xDEADBEEF
#define MAGIC_ALLOC    0xBEEFDEAD

typedef unsigned char byte;
typedef u_int32_t vlink_t;
typedef u_int32_t vsize_t;
typedef u_int32_t vaddr_t;

typedef struct free_list_header {
   u_int32_t magic;  // ought to contain MAGIC_FREE
   vsize_t size;     // # bytes in this block (including header)
   vlink_t next;     // memory[] index of next free block
   vlink_t prev;     // memory[] index of previous free block
} free_header_t;

// Global data

static byte *memory = NULL;   // pointer to start of allocator memory
static vaddr_t free_list_ptr; // index in memory[] of first block in free list
static vsize_t memory_size;   // number of bytes malloc'd in memory[]

// various assistant functions
free_header_t *itop(vlink_t index);
vaddr_t ptoi(free_header_t *node);
static void merge(void);
u_int32_t pivotFunction(u_int32_t count);

// convert index to pointer
free_header_t *itop(vlink_t index){
    free_header_t *converted;
    converted = (free_header_t *)((byte *)memory + index);
    return converted;
}

// convert pointer to index
vaddr_t ptoi(free_header_t *node){
    vaddr_t converted;
    converted = (vaddr_t)((vlink_t)node -(vlink_t)memory);
    return converted;
}

// iteration counter: helper function
u_int32_t pivotFunction(u_int32_t count){
    u_int32_t x = memory_size;
    u_int32_t i = 0;
    while(i < count) {
        x = (x /2);
        i++;
    }
    return x;
}

// Input: size - number of bytes to make available to the allocator
// Output: none              
// Precondition: Size is a power of two.
// Postcondition: `size` bytes are now available to the allocator
// 
// (If the allocator is already initialised, this function does nothing,
//  even if it was initialised with different size)


void vlad_init(u_int32_t size)
{ 
   int block_size = 512;
   if (size < 512){
     block_size = 512;
   } else {
      while (block_size < size) {
         block_size = block_size << 1;
      }
   }

   memory = malloc(block_size);
   if (memory == NULL) {
     fprintf(stderr, "vlad_init:insufficient memory");
     abort();
   }
   // create header of memory
   free_header_t *start = (void *)(memory);
   free_list_ptr = 0;
   start->magic = MAGIC_FREE;
   start->size = block_size;
   start->next = 0;
   start->prev = 0;
   memory_size = block_size;
}


// Input: n - number of bytes requested
// Output: p - a pointer, or NULL
// Precondition: n is < size of memory available to the allocator
// Postcondition: If a region of size n or greater cannot be found, p = NULL 
//                Else, p points to a location immediately after a header block
//                      for a newly-allocated region of some size >= 
//                      n + header size.


void *vlad_malloc(u_int32_t n)
{
   // find size of block requested
   int total_size = n + HEADER_SIZE;
   vsize_t block_size = 1;
   while (block_size < total_size) {
      block_size = block_size << 1;
   }
   free_header_t *node = itop(free_list_ptr);
   free_header_t *search = NULL;

   // search for smallest region in free list that can fit memory
   vsize_t smallest = 0;
   int found = 0;

   while (found == 0) {

      // check if node has memory allocated
      if (node->magic != MAGIC_FREE) {
         fprintf(stderr, "Memory corruption");
         abort();
      }
      // update to smallest possible region
      if (node->size >= block_size) {
         if (smallest > node->size) {
            smallest = node->size;
            search = node;
         }
      }

      // reached beginning, smallest region found
      if (node->next == free_list_ptr)
         found = 1;

      node = itop(node->next);
   }

   if (node->size >= block_size) {
      if (smallest == 0 || smallest > node->size) {
         smallest = node->size;
         search = node;
      }
   }

   // return NULL if no such size is found
   if (search == NULL)
      return NULL;

   // halve free region until it is as small as can be
   while (smallest / 2 >= block_size) {

      // new region halfway through block
      search->size = search->size / 2;
      free_header_t *new = itop(ptoi(search) + search->size);
      new->size = search->size;
      new->magic = MAGIC_FREE;
      new->prev = ptoi(search);
      new->next = search->next;

      // realigning pointers for new block
      free_header_t *after = itop(search->next);
      after->prev = ptoi(new);
      search->next = ptoi(new);
      smallest = smallest / 2;
   }

   // change next and prev pointers in free list
   free_header_t *before = itop(search->prev);
   free_header_t *after = itop(search->next);
   before->next = search->prev;
   after->prev = search->next;

   // update value of search node
   search->size = block_size;
   search->magic = MAGIC_ALLOC;

   // update free list ptr
   free_header_t *newfree = itop(free_list_ptr);

   // find next free region and point to it
   if (newfree->magic != MAGIC_FREE)
      free_list_ptr = ptoi(after);

   return ((void *)search + HEADER_SIZE);
}



// Input: object, a pointer.
// Output: none
// Precondition: object points to a location immediately after a header block
//               within the allocator's memory.
// Postcondition: The region pointed to by object can be re-allocated by 
//                vlad_malloc

void vlad_free(void *object)
{
   // points to beginning of header
   free_header_t *headptr = (void *)object - HEADER_SIZE;

   // check if valid
   if (headptr->magic != MAGIC_ALLOC) {
      fprintf(stderr, "Attempt to free non-allocated memory");
      abort();
   }

   // insert header into free list, create next node
   free_header_t *next = itop(ptoi(headptr) + headptr->size);
   vlink_t head = ptoi(headptr);

   // finds a free space
   while (next->magic != MAGIC_FREE) {
      next = itop(ptoi(next) + next->size);
   }

   // insert into free list, update nodes
   headptr->next = ptoi(next);
   headptr->prev = next->prev;
   headptr->magic = MAGIC_FREE;

   free_header_t *prev = itop(next->prev);
   prev->next = head;
   next->prev = head;

   merge();

}

void merge (void) {
   // 1. check if previous and next blocks are free
   // 2. move current to earliest node
   // 3. merge if current pointer equals pivot
   // 4. repeat

   free_header_t *curr = (void *)(memory + 0);
   vaddr_t ptr = (void *)curr - (void *)memory;

   // infinite loop - exit by break
   while (1) {

      while (curr->magic != MAGIC_FREE) {
         // traverse to first free header
         // use next pointer to traverse free list
         curr = (void *)(memory + ptr + curr->size);
         ptr = (void *)curr - (void *)memory;           
      } 

      if (curr->magic == MAGIC_FREE) {
         // if curr reaches the end of the free list, or one node exists
         if (curr->next <= ptr || curr->size == memory_size)
            break;

         if (ptr + curr->size == curr->next) {
            // if memory is corrupted
            if(curr->magic != MAGIC_FREE){
               fprintf(stderr, "Memory corruption\n");
               abort();
            }
            // curr and next block are adjacent to each other
            // determine if merge is possible
            vsize_t pivot = memory_size;
            vsize_t count = 0;
            while (1) {   

               if (pivot == ptr + curr->size) {
                  // not at a mergable block+
                  // move to next
                  curr = (void *)(memory + curr->next);
                  ptr= (void *)curr - (void *)memory;
                  break;
               } 

               if (ptr == 0 || ptr == pivot) {
                     // check if merge is possible - check if sizes are equal
                     free_header_t *next  = (void *)(memory + curr->next);

                  if (curr->size == next->size) {
                     // merge is possible
                     free_header_t *next2 = (void *)(memory + next->next);
                     next2->prev = ptr;
                     curr->next = (void *)next2 - (void *)memory;
                     // update size
                     curr->size = curr->size + next->size;
                     //restart loop
                     curr = (void *)(memory + 0);
                     ptr = (void *)curr - (void *)memory;

                  } else {
                     curr = (void *)(memory + curr->next);
                     ptr= (void *)curr - (void *)memory;
                  }
                  break;

               } else if (ptr > pivot) {
                  // move to next
                  curr = (void *)(memory + cur
                  // continue evlauation. change pivot
                  count++;
                  pivot = pivot + pivotFunction(count);
                  continue;

               } else if (ptr < pivot) {
                  // continue evaluation. change pivot
                  count++;
                  pivot = pivot - pivotFunction(count);
                  continue;
               } 
            } 

         } else {
            // not adjacent to a free block so find next free block
            curr = (void *)(memory + curr->next);
            ptr = (void *)curr - (void *)memory;
         }

      } else {
         fprintf(stderr, "Memory corruption\n");
         abort();
      }
   }

   // adjust free list ptr
   curr = (void *)(memory + 0);
   ptr = (void *)curr - (void *)memory;
   free_header_t *next = (void *)(memory + ptr + curr->size);

   while (ptr + curr->size < memory_size) {
      curr = (void *)(memory + ptr + curr->size);
      ptr = (void *)curr - (void *)memory;
      next = (void *)(memory + ptr + curr->size);
   }

   free_list_ptr = ptr;
}


// Stop the allocator, so that it can be init'ed again:
// Precondition: allocator memory was once allocated by vlad_init()
// Postcondition: allocator is unusable until vlad_int() executed again

void vlad_end(void)
{
   free(memory);
}


// Precondition: allocator has been vlad_init()'d
// Postcondition: allocator stats displayed on stdout

void vlad_stats(void)
{
   // TODO
   // put whatever code you think will help you
   // understand Vlad's current state in this function
   // REMOVE all pfthese statements when your vlad_malloc() is done

}


//
// All of the code below here was written by Alen Bou-Haidar, COMP1927 14s2
//

//
// Fancy allocator stats
// 2D diagram for your allocator.c ... implementation
// 
// Copyright (C) 2014 Alen Bou-Haidar <alencool@gmail.com>
// 
// FancyStat is free software: you can redistribute it and/or modify 
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or 
// (at your option) any later version.
// 
// FancyStat is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>


#include <string.h>

#define STAT_WIDTH  32
#define STAT_HEIGHT 16
#define BG_FREE      "\x1b[48;5;35m" 
#define BG_ALLOC     "\x1b[48;5;39m"
#define FG_FREE      "\x1b[38;5;35m" 
#define FG_ALLOC     "\x1b[38;5;39m"
#define CL_RESET     "\x1b[0m"


typedef struct point {int x, y;} point;

static point offset_to_point(int offset,  int size, int is_end);
static void fill_block(char graph[STAT_HEIGHT][STAT_WIDTH][20], 
                        int offset, char * label);



// Print fancy 2D view of memory
// Note, This is limited to memory_sizes of under 16MB
void vlad_reveal(void *alpha[26])
{
    int i, j;
    vlink_t offset;
    char graph[STAT_HEIGHT][STAT_WIDTH][20];
    char free_sizes[26][32];
    char alloc_sizes[26][32];
    char label[3]; // letters for used memory, numbers for free memory
    int free_count, alloc_count, max_count;
    free_header_t * block;

	// TODO
	// REMOVE these statements when your vlad_malloc() is done

    // initilise size lists
    for (i=0; i<26; i++) {
        free_sizes[i][0]= '\0';
        alloc_sizes[i][0]= '\0';
    }

    // Fill graph with free memory
    offset = 0;
    i = 1;
    free_count = 0;
    while (offset < memory_size){
        block = (free_header_t *)(memory + offset);
        if (block->magic == MAGIC_FREE) {
            snprintf(free_sizes[free_count++], 32, 
                "%d) %d bytes", i, block->size);
            snprintf(label, 3, "%d", i++);
            fill_block(graph, offset,label);
        }
        offset += block->size;
    }

    // Fill graph with allocated memory
    alloc_count = 0;
    for (i=0; i<26; i++) {
        if (alpha[i] != NULL) {
            offset = ((byte *) alpha[i] - (byte *) memory) - HEADER_SIZE;
            block = (free_header_t *)(memory + offset);
            snprintf(alloc_sizes[alloc_count++], 32, 
                "%c) %d bytes", 'a' + i, block->size);
            snprintf(label, 3, "%c", 'a' + i);
            fill_block(graph, offset,label);
        }
    }

    // Print all the memory!
    for (i=0; i<STAT_HEIGHT; i++) {
        for (j=0; j<STAT_WIDTH; j++) {
            printf("%s", graph[i][j]);
        }
        printf("\n");
    }

    //Print table of sizes
    max_count = (free_count > alloc_count)? free_count: alloc_count;
    printf(FG_FREE"%-32s"CL_RESET, "Free");
    if (alloc_count > 0){
        printf(FG_ALLOC"%s\n"CL_RESET, "Allocated");
    } else {
        printf("\n");
    }
    for (i=0; i<max_count;i++) {
        printf("%-32s%s\n", free_sizes[i], alloc_sizes[i]);
    }

}

// Fill block area
static void fill_block(char graph[STAT_HEIGHT][STAT_WIDTH][20], 
                        int offset, char * label)
{
    point start, end;
    free_header_t * block;
    char * color;
    char text[3];
    block = (free_header_t *)(memory + offset);
    start = offset_to_point(offset, memory_size, 0);
    end = offset_to_point(offset + block->size, memory_size, 1);
    color = (block->magic == MAGIC_FREE) ? BG_FREE: BG_ALLOC;

    int x, y;
    for (y=start.y; y < end.y; y++) {
        for (x=start.x; x < end.x; x++) {
            if (x == start.x && y == start.y) {
                // draw top left corner
                snprintf(text, 3, "|%s", label);
            } else if (x == start.x && y == end.y - 1) {
                // draw bottom left corner
                snprintf(text, 3, "|_");
            } else if (y == end.y - 1) {
                // draw bottom border
                snprintf(text, 3, "__");
            } else if (x == start.x) {
                // draw left border
                snprintf(text, 3, "| ");
            } else {
                snprintf(text, 3, "  ");
            }
            sprintf(graph[y][x], "%s%s"CL_RESET, color, text);            
        }
    }
}

// Converts offset to coordinate
static point offset_to_point(int offset,  int size, int is_end)
{
    int pot[2] = {STAT_WIDTH, STAT_HEIGHT}; // potential XY
    int crd[2] = {0};                       // coordinates
    int sign = 1;                           // Adding/Subtracting
    int inY = 0;                            // which axis context
    int curr = size >> 1;                   // first bit to check
    point c;                                // final coordinate
    if (is_end) {
        offset = size - offset;
        crd[0] = STAT_WIDTH;
        crd[1] = STAT_HEIGHT;
        sign = -1;
    }
    while (curr) {
        pot[inY] >>= 1;
        if (curr & offset) {
            crd[inY] += pot[inY]*sign; 
        }
        inY = !inY; // flip which axis to look at
        curr >>= 1; // shift to the right to advance
    }
    c.x = crd[0];
    c.y = crd[1];
    return c;
}
