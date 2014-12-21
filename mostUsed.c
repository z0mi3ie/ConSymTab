//
// This program reads a series of filenames from the command line.
// It uses a thread to open and read each file to count how many times
// each word appears in the collection of files. The threads all write
// to a shared symbol table.
//
// A word starts with a letter (either uppercase or lowercase) and
// continues until a non-letter (or EOF) is encountered. The program
// ignores non-words, words shorter than six characters in length,
// and words longer than fifty characters in length. All letters
// are converted to lowercase.
//
// When the threads are done reading the files, the main thread then
// prints the twenty words with the highest counts.
//
// Phil Hatcher
// Oct. 2012

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

// support for a concurrent symbol table for storing words and their counts
#include "symtab.h"

#define MAX_WORD 50
#define MIN_WORD 6

#define TOP_COUNTS 20

// prints an error message and terminates the program
void error(char *str)
{
    perror(str);
    exit(-1);
}

// get the next word in the current input file
//   returns NULL if at EOF
static char *getWord(FILE *fp)
{
  char buf[MAX_WORD+1];
  int c;
  int i = 0;

  //read until a letter or EOF is seen
  c = getc(fp);
  while (!isalpha(c))
  {
    if (c == EOF)
    {
      return NULL;
    }
    c = getc(fp);
  }

  // now read until a non-letter or EOF is seen
  while (isalpha(c))
  {
    // if word too long, stop storing letters
    if (i < MAX_WORD)
    {
      buf[i] = tolower(c);
    }
    i += 1;
    c = getc(fp);
  }

  // if word too short or too long, can discard it and try to read another
  if ((i < MIN_WORD) || (i > MAX_WORD))
  {
    // just recurse
    return getWord(fp);
  }
  // otherwise make a safe copy of word
  else {
    buf[i] = 0;
    char *p = malloc(strlen(buf)+1);
    strcpy(p, buf);
    return p;
  }
}

// struct for passing info into the thread work function
struct work {
  char *filename;
  void *symtab;
};

// callback function passed to symtabUpdate
// the trick here is that an integer count is embedded in the void* data item
static void *updateCallback(void *data)
{
  if (data == NULL) // not yet in table
  {
    return (void *) 1;
  }
  else
  {
    unsigned int oldCount = (unsigned int) data;
    return (void *)(oldCount + 1);
  }
}

// thread work function: read and process all the words in one file
static void * processFile(void *in)
{
  struct work *work = in;
  FILE *fp = fopen(work->filename, "r");
  if (fp == NULL)
  {
    fprintf(stderr, "could not open %s: ignored.\n", work->filename);
    return NULL;
  }

  char *word = getWord(fp);
  while (word != NULL)
  {
    if (!symtabUpdate(work->symtab, word, updateCallback))
    {
      fprintf(stderr, "symtab update failed!\n");
      exit(-1);
    }

    // symbol table makes another safe copy of symbol so need to free this one
    free(word);

    word = getWord(fp);
  }
  fclose(fp);
  return NULL;
}

// the main function
//   just starts a thread for each filename on the command line
int main(int argc, char *argv[])
{
  int i;

  if (argc < 2)
  {
    fprintf(stderr, "no filenames given!\n");
    return -1;
  }

  // array to hold thread IDs
  pthread_t pt[argc];

  // array to hold work for each thread
  struct work work[argc];

  // create a symbol table to store the words with their counts
  //void *symtab = symtabCreate(10000, SYMTAB_USE_THIN_LOCKS);
  void *symtab = symtabCreate(10000, SYMTAB_ALLOW_CONCURRENT_READS);
  //void *symtab = symtabCreate(10000, SYMTAB_SINGLE_LOCK);
  //void *symtab = symtabCreate(10000, SYMTAB_LOCK_BUCKET_GROUPS);
  //void *symtab = symtabCreate(10000, 0);
  if (symtab == NULL)
  {
    fprintf(stderr, "can't create symbol table!\n");
    exit(-1);
  }

  // create a thread to process each file
  for (i = 1; i < argc; i++)
  {
    work[i].filename = argv[i];
    work[i].symtab = symtab;

    if (pthread_create(&pt[i], NULL, processFile, &work[i]) != 0)
      error("error in thread create");
  } 
 
  // now wait for each thread to finish
  for (i = 1; i < argc; i++)
  {
     if (pthread_join(pt[i], NULL))
     {
       error("error in thread join");
     }
  } 
 
  // now iterate over the table to find the words with the highest counts.

  // initialize TOP_COUNTS number of counters
  unsigned int topCounts[TOP_COUNTS]; 
  for (i = 0; i < TOP_COUNTS; i++) topCounts[i] = 0;

  // symbols associated with top counters
  const char *topSymbols[TOP_COUNTS];
  for (i = 0; i < TOP_COUNTS; i++) topSymbols[i] = NULL;

  // create iterator for the symbol table
  void *iterator = symtabCreateIterator(symtab);

  // now iterate through the symbol table to find words with highest counts
  void *out;
  const char *p = symtabNext(iterator, &out);
  while (p != NULL)
  {
     // sleezy: unsigned integer count is stored in the bits of the void*
    unsigned int count = (unsigned int) out;

    // Keep the top counts sorted, so need to find the right place to
    // insert new count; topCounts[0] is the highest count seen so far,
    // topCounts[1] is the next highest, etc. Start the search at the
    // bottom of the list since most counts will be below this.
    for (i = TOP_COUNTS-1; i >= 0; i--)
    {
      if (count < topCounts[i]) break;
    }
    i += 1; // new count should go in this slot unless it is TOP_COUNTS

    // If i < TOP_COUNTS then current count needs to be placed in slot i,
    // lower slots need to be shuffled down one position, and the lowest
    // count needs to be removed.
    if (i < TOP_COUNTS)
    {
      int j;
      for (j = TOP_COUNTS-1; j > i; j--)
      {
        topCounts[j] = topCounts[j-1];
        topSymbols[j] = topSymbols[j-1];
      }
      topCounts[i] = count;
      topSymbols[i] = p;
    }
    p = symtabNext(iterator, &out);
  }

  // now print the top counts
  for (i = 0; i < TOP_COUNTS; i++)
  {
    if (topSymbols[i] == NULL) break;
    printf("%s %d\n", topSymbols[i], topCounts[i]);
  }

  // delete the symbol table and the iterator
  symtabDeleteIterator(iterator);
  symtabDelete(symtab);
  
  return 0;
}
