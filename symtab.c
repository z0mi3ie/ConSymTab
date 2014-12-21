/*
 * Kyle Vickers
 * prog4
 * cs520
 *
 * An implementation for using a single lock on the symbol tab, 
 * a group of mutexs, thin locks, and also the allowance of
 * concurrent reads depending on the concurrency support,
 * to be used with threaded applications. 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"
#include <pthread.h>

#define MUTEX_NUM 16

int thinLock( int * lock, int tryCount );
void pthread_yield();

/* Node for the linked list ( bucket )*/
struct Node
{
  char * symbol;
  void * data;
  struct Node * next;
};

/* Control for the backbone array */
struct Control
{
  int length;
  struct Node ** backbone;
  int concurrencySupport;
  pthread_mutex_t mu[MUTEX_NUM];
  pthread_mutex_t smu;
  int thins[MUTEX_NUM];
  struct InnerStruct ** inner;
};

/* Iterator */
struct Iterator
{
  struct Control * controlpointer;
  unsigned int index;
  struct Node * nextnode;
};

/* Inner struct for concurrent reads */
struct InnerStruct
{
  int readers;
  int writers;
  int waitingReaders;
  int waitingWriters;
  pthread_cond_t readerCv;
  pthread_cond_t writerCv;
};

//==================================================================//
/* hash( const char * )
 * This function creates returns a hash value from a given string */ 
static unsigned int hash( const char* str )
{
  const unsigned int p = 16777619;
  unsigned int hash = 2166136261u;
  while(*str)
  {
    hash = (hash^*str)*p;
    str += 1;
  }
  
  hash += hash << 13;
  hash ^= hash >> 7;
  hash += hash << 3;
  hash ^= hash >> 17;
  hash += hash << 5;

  return hash;
}



//==================================================================//
/* struct Node* lookup( const char* symbol, struct Control* array i)
 * looks up a node in the backbone of control given a hash value
 */
static struct Node* lookup( const char* symbol, struct Control* array )
{
  unsigned int hashed = hash( symbol );
  signed int level = hashed % array->length;
  struct Node * cur = array->backbone[level];
  
  while( cur != NULL )
  {
    if( strcmp( cur->symbol, (const char*) symbol ) == 0 )
    {
      return cur;
    }
    else
    {
      cur = cur->next;
    }
  } 

  return NULL;
}


//==================================================================//
/* void *symtabCreate(int sizeHint)
 * Sets up the symbol able control. Allocating the appropriate size
 * for the backbone, initialize the backbone, initializes
 * the appropriate threading structure depending on the 
 * concurrency support, and returns the 
 * control struct setup.
 */
void *symtabCreate(int sizeHint, int concurrencySupport )
{
  struct Control* control = malloc( sizeof( struct Control ) ); 
  if( control == NULL )
  {
    exit(-1);
  }

  control->length = sizeHint;
  control->backbone = malloc( sizeof(struct Node*) * sizeHint);
  control->inner = malloc(sizeof( struct InnerStruct*) * MUTEX_NUM );
  control->concurrencySupport = concurrencySupport;
  
  //fprintf( stderr, "Concurrency support saved: %d\n", control->concurrencySupport );
  
  // Initialize single  mutex
  if( concurrencySupport == SYMTAB_SINGLE_LOCK )
  {
    if( pthread_mutex_init(&control->smu, NULL ) != 0 )
    {
      //fprintf( stderr, "PThread mutex failed to initialize\n");
      exit(-1);
    }
  }
  
  // Initialize Lock Bucket Group Mutexs
  // Initialize Concurent Reads
  if( concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS || 
      concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
  {
    int i;
    for( i = 0; i < MUTEX_NUM; i++ )
    {
      // Initialize all mutexs
      if( pthread_mutex_init(&control->mu[i], NULL )!= 0 )
      {
        fprintf( stderr, "PThread mutex failed to initialize\n");
        exit(-1);
      }
      
      struct InnerStruct * is = malloc( sizeof( struct InnerStruct ) );
      // Concurrent Reading : Initialize inner struct
      is->readers = 0;
      is->writers = 0;
      is->waitingReaders = 0;
      is->waitingWriters = 0;
      
      if( pthread_cond_init(&is->readerCv, NULL ) != 0 )
      {
        fprintf( stderr, "Error initializing readerCv\n" );
        exit(-1);
      }
      
      if( pthread_cond_init(&is->writerCv, NULL ) != 0 )
      {
        fprintf( stderr, "Error initializing writer\n" );
        exit(-1);
      }

      control->inner[i] = is;
    }
  }
  
  // Initialize Thin Locks to unlocked
  if( concurrencySupport == SYMTAB_USE_THIN_LOCKS )
  {
    int i;
    for( i = 0; i < MUTEX_NUM; i++ )
    {
      control->thins[i] = 0;
    }
  }

  // initialize backbone
  int i;
  for( i = 0; i < control->length; i++)
  {
  	control->backbone[i]=NULL;
  }

  if( control->backbone == NULL )
  {
    free( control );
    return NULL;
  }
   
  return control;
}


//==================================================================//
/* void symtabDelete( void* )
 * Deletes a symbol table by first traversing the backbone until
 * a node is found, which then traverses the linked list at that
 * index, freeing behind as it goes, including the freeing of 
 * symbols. When that process completes over the backbone, the 
 * backbone structure itself is freed and then the final cstruct
 * is freed
 */
void symtabDelete(void *symtabHandle)
{
  struct Control * cstruct = symtabHandle;
  struct Node * curList = NULL;
  int i;
  for( i = 0; i  < cstruct->length; i++ )
  {
  	if( cstruct->backbone[i] != NULL )
  	{
  		curList = cstruct->backbone[i];
  		while(curList != NULL )
  		{
  			struct Node * temp = curList;
  			curList = curList->next;
  			free( temp->symbol );
  			free( temp );
  		}
  	}
  }
  
  free(cstruct->backbone);
  free(cstruct);
  return;
}


//==================================================================//
/*
 * Update or install a (symbol,data) pair in the table using the 
 * callback function passed in. 
 *
 *
 */
int symtabUpdate(void *symtabHandle, const char *symbol, void *(*func)(void *))
{ 
  struct Control * cstruct = symtabHandle;
	unsigned int hashed = hash( (const char*) symbol ) % cstruct->length;
  
  //SYMTAB_SINGLE_LOCK : Lock single mutex upon entering 
  if( cstruct->concurrencySupport == SYMTAB_SINGLE_LOCK )
  {
    if( pthread_mutex_lock( &cstruct->smu ) != 0 )
      exit(-1);
  }
  
  //SYMTAB_LOCK_BUCKET_GROUPS : lock mutex = hashed % MUTEX_NUM
  if( cstruct->concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS )
  {
    if( pthread_mutex_lock(&cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
    {
      exit(-1);
    }
  }

  //SYMTAB_USE_THIN_LOCKS
  if( cstruct->concurrencySupport == SYMTAB_USE_THIN_LOCKS )
  {
    int thinTest = 0;
   
    while( thinTest == 0)
    {
      thinTest= thinLock( &cstruct->thins[hashed % MUTEX_NUM], 10000 );
      if( thinTest == 0 )
      {
        // Failure!
        pthread_yield(NULL);
      }
    } 
  }
  
  //SYMTAB_ALLOW_CONCURRENT_READS - Writers algorithm
  if( cstruct->concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
  {
    if( pthread_mutex_lock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
    
    while( cstruct->inner[ hashed % MUTEX_NUM ]->readers > 0 ||
           cstruct->inner[ hashed % MUTEX_NUM ]->writers > 0 )
    {
      cstruct->inner[ hashed % MUTEX_NUM ]->waitingWriters += 1; 
      pthread_cond_wait( &cstruct->inner[hashed % MUTEX_NUM]->writerCv, &cstruct->mu[hashed % MUTEX_NUM] );
      cstruct->inner[ hashed % MUTEX_NUM ]->waitingWriters -= 1; 
    } 
    
    cstruct->inner[ hashed % MUTEX_NUM ]->writers += 1; 
    
    if( pthread_mutex_unlock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
  }

  void * passedFunction = malloc(sizeof(void*));  
  if( passedFunction == NULL )
    exit(-1);
  
  struct Node * looked = lookup( symbol, cstruct );   

  if( looked != NULL )
  {
    if( passedFunction == NULL )
    {
      return 0;
    }
    
    looked->data = (*func)(looked->data);
    
    // SYMTAB_SINGLE_LOCK : unlock the single mutex 
    if( cstruct->concurrencySupport == SYMTAB_SINGLE_LOCK )   
    {
      if( pthread_mutex_unlock(&cstruct->smu) != 0 )
        exit(-1);
    } 
    
    // SYMTAB_LOCK_BUCKET_GROUPS : unlock the specific group mutex
    if( cstruct->concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS )
    {
      if( pthread_mutex_unlock(&cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
      {
        exit(-1);
      }
    }
    
    // SYMTAB_USE_THIN_LOCKS : unlock the group using thin lock 
    if( cstruct->concurrencySupport == SYMTAB_USE_THIN_LOCKS )
      cstruct->thins[ hashed % MUTEX_NUM ] = 0;
    
    // SYMTAB_ALLOW_CONCURRENT_READS : Writer end algorithm
    if( cstruct->concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
    { 
      // Lock Mutex
      if( pthread_mutex_lock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
        exit(-1);
      // Writers -= 1
      cstruct->inner[ hashed % MUTEX_NUM ]->writers -= 1;
      // if( waitingWriters > 0 )
      if( cstruct->inner[ hashed % MUTEX_NUM ]->waitingWriters > 0 )
      {
      //  signal( cvWriters )  
        pthread_cond_signal( &cstruct->inner[hashed % MUTEX_NUM]->writerCv ); 
      }
      else if( cstruct->inner[ hashed % MUTEX_NUM ]->waitingReaders > 0 )
      {
        pthread_cond_signal( &cstruct->inner[hashed % MUTEX_NUM]->readerCv );
      }
      // unlock Mutex
      if( pthread_mutex_unlock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
        exit(-1);
    }
    
    return 1;
  }  
  else 
  {  
    passedFunction = func( NULL );
    if( passedFunction == NULL )
    {
      return 0;
    }
    
    // New Node - Linked Structure
    struct Node * newNode = malloc( sizeof( struct Node ) );
    if( sizeof( newNode ) == 0 )
      return 0;

		// New Char - Symbol
    char * newSymbol = malloc(sizeof(char) * ( strlen( symbol ) + 1) );
    if( sizeof( newSymbol ) == 0)
      return 0;
		
		// Copy String
    strcpy( newSymbol, symbol );

		// Fill new linked structure
    newNode->next = NULL;
    newNode->symbol = newSymbol;         
    newNode->data = (*func)(NULL);
   
    // Attach node to the front of the list
    struct Node * old = cstruct->backbone[hashed];
    
    if( old != NULL )
    {
      struct Node * temp = cstruct->backbone[hashed];
      newNode->next = temp;
      cstruct->backbone[hashed] = newNode;
    } 
    else if( old == NULL )
    { 
      newNode->next = NULL;
      cstruct->backbone[hashed] = newNode;
    }
  
   
    // SYMTAB_SINGLE_LOCK : unlock the single mutex 
    if( cstruct->concurrencySupport == SYMTAB_SINGLE_LOCK )
    {
      if( pthread_mutex_unlock(&cstruct->smu) != 0 )
        exit(-1);
    }
     
    // SYMTAB_LOCK_BUCKET_GROUPS : unlock the specific group mutex
    if( cstruct->concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS )
    {
      if( pthread_mutex_unlock(&cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
        exit(-1);
    }
  
    // SYMTAB_USE_THIN_LOCKS : unlock the group using thin lock 
    if( cstruct->concurrencySupport == SYMTAB_USE_THIN_LOCKS )
      cstruct->thins[ hashed % MUTEX_NUM ] = 0;
    
    // SYMTAB_ALLOW_CONCURRENT_READS : Writer end algorithm
    if( cstruct->concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
    { 
      if( pthread_mutex_lock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
        exit(-1);
      cstruct->inner[ hashed % MUTEX_NUM ]->writers -= 1;
      if( cstruct->inner[ hashed % MUTEX_NUM ]->waitingWriters > 0 )
      {
        pthread_cond_signal( &cstruct->inner[hashed % MUTEX_NUM]->writerCv ); 
      }
      else if( cstruct->inner[ hashed % MUTEX_NUM ]->waitingReaders > 0 )
      {
        pthread_cond_signal( &cstruct->inner[hashed % MUTEX_NUM]->readerCv );
      }
      if( pthread_mutex_unlock( &cstruct->mu[ hashed % MUTEX_NUM ] ) != 0 )
        exit(-1);
    }


    return 1;  
  }

  return 0;
}


//==================================================================//
/* void *symtabLookup( void *, const char * )
 * Looks up the symbol in the table.
 */
void *symtabLookup(void *symtabHandle, const char *symbol)
{
  //fprintf( stderr, "symtab lookup called\n" );
  struct Control * cont = symtabHandle;
	unsigned int hashed = hash( (const char*) symbol ) % cont->length;  
  
  //SYMTAB_SINGLE_LOCK : Lock single mutex upon entering 
  if( cont->concurrencySupport == SYMTAB_SINGLE_LOCK )
  {
    if( pthread_mutex_lock( &cont->smu ) != 0 )
      exit(-1);
  }
  
  //SYMTAB_LOCK_BUCKET_GROUPS : lock mutex = hashed % MUTEX_NUM
  if( cont->concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS )
  {
    if( pthread_mutex_lock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
  }
  
  //SYMTAB_USE_THIN_LOCKS
  if( cont->concurrencySupport == SYMTAB_USE_THIN_LOCKS )
  {
    int thinTest = 0; 
    while( thinTest == 0)
    {
      thinTest= thinLock( &cont->thins[hashed % MUTEX_NUM], 10000 );
      if( thinTest == 0 )
      {
        // Failure!
        pthread_yield(NULL);
      }
    } 
  }

  // Concurrent read support //
  // READERS START //
  if( cont->concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
  { 
    if( pthread_mutex_lock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
    while( cont->inner[hashed % MUTEX_NUM]->writers > 0 )
    {
      cont->inner[hashed % MUTEX_NUM]->waitingReaders += 1;
      pthread_cond_wait( &cont->inner[hashed % MUTEX_NUM]->readerCv, &cont->mu[hashed % MUTEX_NUM] );
      cont->inner[hashed % MUTEX_NUM]->waitingReaders -= 1;
    }
    cont->inner[hashed % MUTEX_NUM]->readers += 1;
    if( pthread_mutex_unlock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
  }

  struct Node *node = lookup( symbol, cont );
  
  // SYMTAB_SINGLE_LOCK : unlock the single mutex 
  if( cont->concurrencySupport == SYMTAB_SINGLE_LOCK )
  {
    if( pthread_mutex_unlock(&cont->smu) != 0 )
      exit(-1);
  }
     
  // SYMTAB_LOCK_BUCKET_GROUPS : unlock the specific group mutex
  if( cont->concurrencySupport == SYMTAB_LOCK_BUCKET_GROUPS )
  {
    if( pthread_mutex_unlock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
  } 
  // SYMTAB_USE_THIN_LOCKS : unlock the group using thin lock 
    if( cont->concurrencySupport == SYMTAB_USE_THIN_LOCKS )
      cont->thins[ hashed % MUTEX_NUM ] = 0;
  
  // READER END //  
  if( cont->concurrencySupport == SYMTAB_ALLOW_CONCURRENT_READS )
  { 
	  unsigned int hashed = hash( (const char*) symbol ) % cont->length;  
    // Lock Mutex 
    if( pthread_mutex_lock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
    // readers -= 1;
    cont->inner[hashed % MUTEX_NUM]->readers -= 1;
    // if( waitingWriteres > 0 && readers == 0 )
    if( cont->inner[hashed % MUTEX_NUM]->waitingWriters > 0 && 
        cont->inner[hashed % MUTEX_NUM]->readers == 0 )
    {
      //   signal( cvWriters )
      pthread_cond_signal( &cont->inner[hashed % MUTEX_NUM]->writerCv ); 
    }
    // Unlock Mutex 
    if( pthread_mutex_unlock(&cont->mu[ hashed % MUTEX_NUM ] ) != 0 )
      exit(-1);
  }
  
  
  if( node == NULL )
  {
    return NULL;
	}
 
 	
 	/* Found the node in backbone from lookup
 	 * return the looked up node's data */
  return node->data;
}


//==================================================================//
/* symtabCreateIterator(void *)
 * Creates and loads an iterator struct to use for the traversal
 * of the symbol table. 
 */
void *symtabCreateIterator(void *symtabHandle)
{
  struct Control * control = symtabHandle;
  struct Iterator * iter = malloc( sizeof( struct Iterator ) );
  unsigned int size = control->length;
  int index = 0;
  
  /* If iter is NULL after malloc, return NULL
   * Something went wrong */
  if( iter  == NULL )
  {
    return NULL;
  }
  
  iter->controlpointer = control;
  
  /* Find first iterable node in backbone */ 
  for( index = 0; index < size; index++ )
  {
  	struct Node * curBackBone = iter->controlpointer->backbone[index];
    if( curBackBone == NULL )
    {
    	continue;
    }
    else if( curBackBone != NULL )
    {
      iter->nextnode = curBackBone;
      iter->index = index;
      /* found first iterable and set info.
       * return this iterable.*/
      return iter;
    }
  }

  /* No iterable backbone node found
   * Returning NULL */
  return NULL;
}


//==================================================================//
/* symtableNext( void*, void** )
 * gets the next
 */
const char *symtabNext(void *iteratorHandle, void **returnData)
{
  struct Iterator * iter = iteratorHandle;

	// The iterator has no next node
  if( iter->nextnode == NULL )
  {
    return NULL;
  }
  
  char *symbol = iter->nextnode->symbol;
  struct Node * iternode =  iter->nextnode;
  *returnData = iternode->data;
  iter->nextnode = NULL;

  if( iternode->next != NULL)
  {
  	iter->nextnode = iternode->next;
  }
  else
  {
    // Find the next node on the backbone
    int i;
    int cplength = iter->controlpointer->length;
    for( i = (iter->index+1); i < cplength; i++ )
    {  
    	struct Node * curBackBone = iter->controlpointer->backbone[i];
      if( curBackBone != NULL )
      {
        iter->nextnode = curBackBone;;
				iter->index = i;
        return symbol;
      }
    }
  }
  return symbol;
}

//==================================================================//
/* symtabDeleteIterator( void * )
 * Deletes the iterator.
 */
void symtabDeleteIterator(void *iteratorHandle)
{
	struct Iterator * iter = iteratorHandle;
  free( iter );
  return;
}









































