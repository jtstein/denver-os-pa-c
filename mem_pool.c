/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/* Constants */
#define MEM_FILL_FACTOR 0.75;
#define MEM_EXPAND_FACTOR 2;


static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = MEM_FILL_FACTOR;
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = MEM_EXPAND_FACTOR;

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = MEM_FILL_FACTOR;
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = MEM_EXPAND_FACTOR;

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = MEM_FILL_FACTOR;
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = MEM_EXPAND_FACTOR;



/* Type declarations */
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
    unsigned gap_ix_size;
} pool_mgr_t, *pool_mgr_pt;


/* Static global variables */
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;


/* Forward declarations of static functions */
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);


/* Definitions of user-facing functions */

/*
* Function Name: mem_init
* Passed Variables: None
* Return Type: alloc_status
* Purpose: To intialize the pool store. The pool store is an array,
* of all the pool managers being used within the program.
* This array is the size of the variable MEM_POOL_STORE_INIT_CAPACITY
* which is 20, giving us space for 20 pool_managers. In case this function
* is called when the pool store has already been intialized, thre is a check to
* not reintialize the pool_store. If the allocation fails then the function
* returns ALLOC_FAIL.
*/
alloc_status mem_init() {

	//If the pool store already has been initialized
	//Return the allocation status stating that it already has been initialized.
	if (pool_store != NULL){
		return ALLOC_CALLED_AGAIN;
	}
	//Allocate room for the initial amount pool store capacity
	pool_store = (pool_mgr_pt *)calloc(MEM_POOL_STORE_INIT_CAPACITY, sizeof(pool_mgr_t));
	//If our allocation went correctly
	if (pool_store != NULL){
		//Set the size and capacity of the pool store
		pool_store_size = MEM_POOL_STORE_INIT_CAPACITY;

		return ALLOC_OK;
	}
    //If we get to this point then we know an allocation failed
	return ALLOC_FAIL;
}

alloc_status mem_free() {
    
	/* If mem_init hasn't been called yell at things. */
	if (pool_store == NULL){
		return ALLOC_CALLED_AGAIN;
	}
	/* for all initialized pool managers */
	for (unsigned int i = 0; i < pool_store_capacity; ++i){
		/* delete the memory of the poolmgr */
		mem_pool_close(&pool_store[i]->pool);
	}
	/* free the memory allocated */
	free(pool_store);
    pool_store = NULL;
	if (pool_store != NULL){
		/* If the deallocation failed return a failure */
		return ALLOC_FAIL;
	}
	/* reset static variables */
	pool_store_capacity = 0;
	pool_store_size = 0;
	return ALLOC_OK;
}

/*
 * Function Name: mem_pool_open
 * Passed Variables: size_t size, alloc_policy policy
 * Return Type: pool_pt
 * Purpose: This function creates a new pool of memory of the passed size.
 * This is put into a new pool_mgr that has all of it's default values set.
 * The pool's default values are also set. These default values are set using
 * constant value specified at the start of the file.
 */
pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    // If the array of pool stores hasn't been allocated then allocate it.
	if (pool_store == NULL){
		//if the memory fails to allocate then return NULL.
		if (mem_init() == ALLOC_FAIL){
			return NULL;
		}
	}
    pool_store_capacity++;//Increase the amount of pools in the pool_store.
	//CHeck to see if we have the maximum amount of pool_stores or not.
	if (_mem_resize_pool_store() != ALLOC_OK){
        pool_store_capacity--;
		return NULL;//IF it fails then return NULL.
	}

    int bool = 0;
    pool_mgr_pt manager = NULL;
    /* Loop until allocation succeeds */
    while(bool == 0){
        manager = calloc(1, sizeof(pool_mgr_t));//Create a new pool.
        if (manager != NULL){
            /* If the allocation succeded escape the loop */
            bool = 1;
        }
    }

	pool_store[pool_store_capacity - 1] = manager;//Place the new pool in the next open place of the pool_store array.
	//Set pools values
	(*manager).pool.policy = policy;
	(*manager).pool.total_size = size;
	(*manager).pool.mem = malloc(size);

	if ((*manager).pool.mem == NULL){
		free((*manager).pool.mem);//delete the memory allocation
		free(manager);//delete the allocation of the pool store.
		//Restore these states to their pre function states.
		pool_store[pool_store_capacity - 1] = NULL;
		pool_store_capacity--;
		return NULL;
	}

	//Allocate the node heap and gap index
	(*manager).gap_ix = calloc(MEM_GAP_IX_INIT_CAPACITY, sizeof(gap_t));
	(*manager).node_heap = calloc(MEM_NODE_HEAP_INIT_CAPACITY, sizeof(node_t));
	if ((*manager).node_heap == NULL || (*manager).gap_ix == NULL){
		//Free all allocated memory
		free((*manager).node_heap);
		free((*manager).gap_ix);
		free((*manager).pool.mem);
		free(manager);
		//Restore these states to their pre function states.
		pool_store[pool_store_capacity - 1] = NULL;
		pool_store_capacity--;
		return NULL;
	}
	//Initialize all gap and node members.
	(*manager).total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;
	(*manager).used_nodes = 1;
    //call add to gap ix here once written for a gap the size of the pool
    (*manager).gap_ix_size = MEM_GAP_IX_INIT_CAPACITY;
    (*manager).node_heap[0].alloc_record.size = size;
    (*manager).node_heap[0].allocated = 0;
    (*manager).node_heap[0].used = 1;
    (*manager).node_heap[0].prev = NULL;
    (*manager).node_heap[0].next = NULL;
    if(_mem_add_to_gap_ix(manager, size, &(*manager).node_heap[0]) == ALLOC_FAIL){
        printf("Failed to add first node to gap index.");
        exit(0);
    }


    return (pool_pt) manager;
}

/*
 * Function Name: mem_pool_close
 * Passed Variables: pool_pt pool
 * Return Type: alloc_status
 * Purpose: This function deletes a pool from memory. Alongside the pool
 * all allocated memory is deleted and the pool's manager is removed
 * from the pool store array. If the pool is not aember of any of the pool
 * managers then the function returns ALLOC_NOT_FREED telling the program that
 * the pool was not deallocated.
 */
alloc_status mem_pool_close(pool_pt pool) {

    const pool_mgr_pt manager = (pool_mgr_pt) pool;
	//If the pool was not found then we cannot deallocate
	if (manager == NULL) {
        return ALLOC_FAIL;
    }
    if(manager->used_nodes > 1){
        return ALLOC_NOT_FREED;
    }
	//free all allocated memory
	free((*manager).pool.mem);
	free((*manager).node_heap);
	free((*manager).gap_ix);
	free(manager);
	pool_store_capacity--;

    return ALLOC_OK;
}

alloc_pt mem_new_alloc(pool_pt pool, size_t size) {

    /* Upcast the pool to access the manager */
    size_t remainSpace = 0;
    const pool_mgr_pt manager = (pool_mgr_pt) pool;
    /* If any of these cases are true then exit */
    if((*manager).gap_ix_capacity == 0 || _mem_resize_node_heap(manager) == ALLOC_FAIL ||
       (*manager).total_nodes <= (*manager).used_nodes){
        exit(0);
    }
    node_pt newNode = NULL;
    unsigned best_Position = 0;
    if(manager->pool.policy == BEST_FIT) {
        /*These values represent the location of the location of the current best gap
         * and it's size. They are set to the first gap (0) because the gap index is sorted
         * and that gap is going to have the largest amount of memory possible.*/
        size_t current_Best = (*manager).gap_ix[0].size;
        for (unsigned i = 0; i < (*manager).gap_ix_capacity; ++i) {
            /*Loop throught the array until we find a gap that is a better fit than the current one*/
            if ((*manager).gap_ix[i].size >= size && (*manager).gap_ix[i].size <= current_Best) {
                best_Position = i;
                newNode = (*manager).gap_ix[i].node;
                current_Best = (*manager).gap_ix[i].size;
                /*If the gaps size is the exact size of the requested size then break
                 * since there is no possible way to get a better gap.*/
                if ((*manager).gap_ix[i].size == size) {
                    break;
                }
            }
            if (i == (*manager).gap_ix_capacity - 1) {
                //If we did not find an optimal gap
                if (newNode == NULL) {
                    printf("No gap that has enough memory for allocation");
                    return NULL;
                }
                //Calculate the remaining gap space
                remainSpace = newNode->alloc_record.size - size;
            }
        }
    }


    /* First Fit allocation */
    if(manager->pool.policy == FIRST_FIT){
        for (unsigned int i = 0; i<(*manager).total_nodes; ++i){
            /* Find the first empty node in the array. Needs to be able to fit the size we're allocating */
            if((*manager).node_heap[i].allocated == 0 && manager->node_heap[i].used == 1 && (*manager).node_heap[i].alloc_record.size >= size){
                newNode = &(*manager).node_heap[i];//Set the new node to the found gap.
                remainSpace = newNode->alloc_record.size - size;//Place the remaining amount of memory into a holder for later
                (*manager).node_heap[i] = *newNode;
                best_Position = i;
                break;
            }
        }
    }
    /* if the node couldn't be allocated return null */
    if(newNode == NULL){
        return NULL;
    }
    /* remove the node from the gap index */
    if(_mem_remove_from_gap_ix(manager,size,newNode) != ALLOC_OK){
        return NULL;
    }
    manager->pool.num_allocs++;//Change the amount of allocations to the pool
    manager->pool.alloc_size += size;
    /* Alter the nodes values */
    newNode->used = 1;
    newNode->allocated = 1;
    newNode->alloc_record.size = size;
    newNode->alloc_record.mem = malloc(size);
    node_pt gap_Node = NULL; // Create a new node to hold the node that's going to become the gap.
    /* Check if we need a new node for the next gap or if we don't need a new gap. */
    if(_mem_resize_node_heap(manager)== ALLOC_FAIL && remainSpace != 0){
        exit(0);
    }
    if(remainSpace != 0) {
        for (unsigned i = best_Position; i < (*manager).total_nodes; ++i) {
            /*Find an unused node */
            if ((*manager).node_heap[i].used == 0) {
                gap_Node = &(*manager).node_heap[i];
                /* add this node to the gap index with the leftover size from the alloc. */
                if (_mem_add_to_gap_ix(manager, remainSpace, gap_Node) == ALLOC_FAIL) {
                    exit(0);
                }
                break;
            }
        }
        /* Increase the used nodes and have the nodes start to point to one another */
        manager->used_nodes++;
        if (newNode->next != NULL) {
            node_pt next = newNode->next;
            gap_Node->next = next;
            next->prev = gap_Node;
        }
        else {
            gap_Node->next = NULL;
        }

        newNode->next = gap_Node;
        gap_Node->prev = newNode;
    }
    newNode->allocated = 1;

    return (alloc_pt) newNode;
}

alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
    // get mgr from pool by casting the pointer to (pool_mgr_pt)
    pool_mgr_pt mgr = (pool_mgr_pt) pool;

    // get node from alloc by casting the pointer to (node_pt)
    node_pt node = (node_pt) alloc;

    node_pt del_node = NULL;
    // find the node in the node heap
    for(int i = 0; i< mgr->total_nodes; ++i){
        if(node == &mgr->node_heap[i]){
            del_node = &mgr->node_heap[i];
            break;
        }
    }
    // this is node-to-delete
    // make sure it's found
    if(del_node == NULL){
        return ALLOC_FAIL;
    }

    // convert to gap node
    del_node->allocated = 0;

    // update metadata (num_allocs, alloc_size)
    mgr->pool.num_allocs--;
    mgr->pool.alloc_size -= del_node->alloc_record.size;


    // if the next node in the list is also a gap, merge into node-to-delete
    if(del_node->next != NULL && del_node->next->allocated == 0) {
        node_pt next = del_node->next;
        //   remove the next node from gap index
        if(_mem_remove_from_gap_ix(mgr, 0, next) == ALLOC_FAIL)
            return ALLOC_FAIL;

        //   add the size to the node-to-delete
        del_node->alloc_record.size += next->alloc_record.size;
        //   update node as unused
        next->used = 0;
        //   update metadata (used nodes)
        mgr->used_nodes--;
        //   update linked list:
        if (next->next) {
            next->next->prev = del_node;
            del_node->next = next->next;
        } else {
            del_node->next = NULL;
        }
        next->next = NULL;
        next->prev = NULL;
    }
    // this merged node-to-delete might need to be added to the gap index
    // but one more thing to check...
    // if the previous node in the list is also a gap, merge into previous!
    if(del_node->prev!= NULL && del_node->prev->allocated == 0) {
        //   remove the previous node from gap index
        node_pt previous = del_node->prev;
        if(_mem_remove_from_gap_ix(mgr, 0, previous) == ALLOC_FAIL)
            return ALLOC_FAIL;

        //   add the size of node-to-delete to the previous
        previous->alloc_record.size += del_node->alloc_record.size;
        //   update node-to-delete as unused
        del_node->used = 0;
        //   update metadata (used_nodes)
        mgr->used_nodes--;
        //   update linked list
        if (del_node->next) {
            previous->next = del_node->next;
            del_node->next->prev = previous;
        } else {
            previous->next = NULL;
        }
        del_node->next = NULL;
        del_node->prev = NULL;

        //   change the node to add to the previous node!
        del_node = previous;
    }
    // add the resulting node to the gap index
    // check success
    if(_mem_add_to_gap_ix(mgr, del_node->alloc_record.size,del_node ) != ALLOC_OK)
        return ALLOC_FAIL;

    return ALLOC_OK;
}

/*
 * Function Name: mem_inspect_pool
 * Passed Variables: pool_pt pool, pool_segment_pt *segments, unsigned *num_segments
 * Return Type: void
 * Purpose: This function is called within main so the contents of the pool
 * may be displayed in the console. Segments and num_segments are used in
 * main and are passed back by reference. Segments is an array of all used 
 * nodes, while num_segments is the amount of used nodes.
 */
void mem_inspect_pool(pool_pt pool, pool_segment_pt *segments, unsigned *num_segments) {

    pool_mgr_pt pool_mgr = (pool_mgr_pt) pool;

    // allocate the segments array with size == used_nodes
    pool_segment_pt segs = (pool_segment_pt) calloc(pool_mgr->used_nodes, sizeof(pool_segment_t));

    // check successful
    assert(segs);
    node_pt current = pool_mgr->node_heap;

    // loop through the node heap and the segments array
    for(int i = 0; i < pool_mgr->used_nodes; ++i){
        //    for each node, write the size and allocated in the segment
        segs[i].size = current->alloc_record.size;
        segs[i].allocated = current->allocated;
        if(current->next != NULL) {
            current = current->next;
        }
    }

    // "return" the values:
    *segments = segs;
    *num_segments = pool_mgr->used_nodes;
    /*pass these values back */

    return;
    
}


/* Definitions of static functions */

/*
 * Function Name: _mem_resize_pool_store
 * Passed Variables: none
 * Return Type: alloc_status
 * Purpose: This function takes the current pool store array and determines
 * whether or not the pool store needs more pool managers. It resizes
 * this by using the realloc function to reallocate the amount of pool mangers
 * in the pool store. If this reallocation fails thn we return ALLOC_FAIL.
 */
static alloc_status _mem_resize_pool_store() {
    /* Check to see if we have too many pools. */
    if (((float) pool_store_capacity / pool_store_size)> MEM_POOL_STORE_FILL_FACTOR){
        /* Create a new pool manager that is a reallocated 'pool_store' */
        pool_mgr_pt* reallocated_store = (pool_mgr_pt *) realloc(pool_store, pool_store_capacity* MEM_POOL_STORE_EXPAND_FACTOR * sizeof(pool_mgr_pt));
        if(reallocated_store == NULL){
            /* If the allocation failed then we return a fail state. */
            return ALLOC_FAIL;
        }
        else{
            /* Set the pool_store to the newly allocated pool_store 'reallocated_store*/
            pool_store = reallocated_store;
        }
        return ALLOC_OK;
    }
    /* If we don't have too many pools then we can return that everything is okay */
    else{
        return ALLOC_OK;
    }
}

/*
 * Function Name: _mem_resize_node_heap
 * Passed Variables: pool_mgr_pt pool_mgr
 * Return Type: alloc_status
 * Purpose: This function works similarly to the function 
 * _mem_resize_pool_store. The ultimate difference comes from the fact
 * that the pool_store is not being resized but instead the node heap is being 
 * resized.
 */

static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {

    /* Check to see if we have too many nodes */
    if((*pool_mgr).used_nodes > (*pool_mgr).total_nodes * MEM_NODE_HEAP_FILL_FACTOR){
        /* Create a new node_pt that is a reallocated node heap. */
        /* We use the expand factor to increase the size. This is simply multiplying by 2. */
        node_pt reallocated_node = (node_pt) realloc((*pool_mgr).node_heap, (*pool_mgr).total_nodes * MEM_NODE_HEAP_EXPAND_FACTOR * sizeof(node_pt));
        if(reallocated_node == NULL){
            /* If the allocation failed then return ALLOC_FAIL. */
            return ALLOC_FAIL;
        }
        else{
            /* Set the node heap to the newly allocated 'reallocated_node' */
            (*pool_mgr).node_heap = reallocated_node;
            (*pool_mgr).total_nodes *= MEM_NODE_HEAP_EXPAND_FACTOR;
            return ALLOC_OK;
        }
    }
    /* If we are okay on nodes then return okay. */
    else{
        return ALLOC_OK;
    }
}

/*
 * Function Name: _mem_resize_gap_ix
 * Passed Variables: pool_mgr_pt pool_mgr
 * Return Type: alloc_status
 * Purpose: This function works similarly to the function
 * _mem_resize_pool_store. The ultimate difference comes from the fact
 * that the pool_store is not being resized but instead the gap index
 * is being resized.
 */
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {

    if((*pool_mgr).used_nodes > (*pool_mgr).total_nodes * MEM_NODE_HEAP_FILL_FACTOR){
        /* Create a new node_pt that is a reallocated gap index. */
        /* We use the expand factor to increase the size. This is simply multiplying by 2. */
        gap_pt reallocated_gap = (gap_pt) realloc((*pool_mgr).gap_ix, (*pool_mgr).gap_ix_size * MEM_GAP_IX_EXPAND_FACTOR * sizeof(gap_pt));
        if(reallocated_gap == NULL){
            /* If the allocation failed then return ALLOC_FAIL. */
            return ALLOC_FAIL;
        }
        else{
            /* Set the node heap to the newly allocated 'reallocated_gap' */
            (*pool_mgr).gap_ix = reallocated_gap;
            (*pool_mgr).gap_ix_capacity *= MEM_GAP_IX_EXPAND_FACTOR;
            return ALLOC_OK;
        }
    }
    /* If we are okay on gaps then return okay. */
    else{
        return ALLOC_OK;
    }
}

/*
 * Function Name: _mem_add_to_gap_ix
 * Passed Variables: pool_mgr_pt pool_mgr, size_t size, node_pt node
 * Return Type: alloc_status
 * Purpose: The purpose of this function is to add anew gap to the gap index.
 * This gap is a passed as a node from the node index. The first step of the 
 * function is to check to make sure that we have enough size in the gap index.
 * Once that is done we place the gap at the fiirst unused position of the 
 * gap index (gap_ix_capacity). Then any neccesary data members of the gap, node
 * and pool manager are set. The gap index is then sorted so the biggest gap is at
 * the top of the index.
 */
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                                       size_t size,
                                       node_pt node) {
    /* Check to see if we need to resize */
    if(pool_mgr->gap_ix_capacity != 0 || pool_mgr->gap_ix_capacity / pool_mgr->gap_ix_size > MEM_GAP_IX_FILL_FACTOR){
        if(_mem_resize_gap_ix(pool_mgr) == ALLOC_FAIL){
            return ALLOC_FAIL;
        }
    }
    /* Set the nodes values */
    (*node).allocated = 0;
    (*node).used = 1;
    /* Add the gap to the index and node heap */
    (*pool_mgr).gap_ix[pool_mgr->gap_ix_capacity].node = node;
    (*pool_mgr).gap_ix[pool_mgr->gap_ix_capacity].node->alloc_record.size = size;
    (*pool_mgr).gap_ix[pool_mgr->gap_ix_capacity].size = size;
    /*Increase the amount of gaps */
    (*pool_mgr).gap_ix_capacity++;
    (*pool_mgr).pool.num_gaps++;
    /* Sort the index */
    return _mem_sort_gap_ix(pool_mgr);

}

/*
 * Function Name: _mem_remove_from_gap_ix
 * Passed Variables: pool_mgr_pt pool_mgr, size_t size, node_pt node
 * Return Type: alloc_status
 * Purpose: This function removes a gap from the index, which is done
 * when a node needs to have memory allocated. To find the gap that we
 * are attempting to remove we use a node pointer and compare it to the
 * node thta the gap is pointing to. Once this gap is found, we swap it 
 * with the gap at the bottom of the index, overwrite it's data and then
 * resort the gap; ending with the gap capacity being decremented.
 */
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                            size_t size,
                                            node_pt node) {
    int gap_Location = -1;
    /* Loop throught the gap index until we find the gap that is that node. */
    for(unsigned i = 0; i< pool_mgr->gap_ix_capacity; ++i){
        /*Once the node is found set the index number to Gap location and break. */
        if(pool_mgr->gap_ix[i].node == node){
            gap_Location = i;
            break;
        }
    }
    /* If the node wasn't found return ALLOC_FAIL */
    if(gap_Location == -1){
        return ALLOC_FAIL;
    }
    /* Swap the node to be removed with the last filled gap in the index */
    pool_mgr->gap_ix[gap_Location] = pool_mgr->gap_ix[pool_mgr->gap_ix_capacity -1];
    /* Delete the node that is now in the last spot of the "filled index. */
    pool_mgr->gap_ix[pool_mgr->gap_ix_capacity-1].node = NULL;
    pool_mgr->gap_ix[pool_mgr->gap_ix_capacity-1].size = 0;
    /* Decrement the amount of used gaps */
    --pool_mgr->gap_ix_capacity;
    --pool_mgr->pool.num_gaps;

    return _mem_sort_gap_ix(pool_mgr);
}

/*
 * Function Name: _mem_sort_gap_ix
 * Passed Variables: pool_mgr_pt pool_mgr
 * Return Type: alloc_status
 * Purpose: This function performs a bubble sort on the gap index
 * sorting the index based on each gap's size. If the amount of gaps is
 * smaller than 2 then there is no need to sort the memory and just returns
 * ALLOC_OK.
 */
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
    /* This is just a bubble sort that sorts the gaps based on their size. */
    if(((*pool_mgr).gap_ix_capacity <=1)){
        return ALLOC_OK;
    }
    for(unsigned int i =0; i < (*pool_mgr).gap_ix_capacity-1; ++i){
        for(unsigned int j = 1; j < (*pool_mgr).gap_ix_capacity-2;++j){
            /* If the sizes are unordered. */
            if((*pool_mgr).gap_ix[j].size < (*pool_mgr).gap_ix[j+1].size){
                /* Swap them :D */
                gap_t swapper = (*pool_mgr).gap_ix[j];
                (*pool_mgr).gap_ix[j] = (*pool_mgr).gap_ix[i];
                (*pool_mgr).gap_ix[i] = swapper;
            }
            if(pool_mgr->gap_ix[i].size == pool_mgr->gap_ix[j].size){
                if(pool_mgr->gap_ix[i].node < pool_mgr->gap_ix[j].node){
                    gap_t swapper = (*pool_mgr).gap_ix[j];
                    (*pool_mgr).gap_ix[j] = (*pool_mgr).gap_ix[i];
                    (*pool_mgr).gap_ix[i] = swapper;
                }
            }
        }
    }

    return ALLOC_OK;
}


