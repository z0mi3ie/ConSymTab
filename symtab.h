//
// This is the interface for a generic symbol table. A table stores
// (symbol, data) pairs.
//
// A symbol is simply a C string (null-terminated char sequence).
//
// The data associated with a symbol is simply a void*.
//
// This implementation includes support for concurrent access to the symbol
// table. However, creation, deletion, and iteration are assumed to be
// single-threaded.
//

void *symtabCreate(int sizeHint, int concurrencySupport);
  // Creates a symbol table.
  // If successful, returns a handle for the new table.
  // If memory cannot be allocated for the table, returns NULL.
  // The first parameter is a hint as to the expected number of (symbol, data)
  //   pairs to be stored in the table.
  // The second parameter selects a level of concurrency support:
  //   SYMTAB_NO_LOCKING - implementation should not use locking because symbol
  //                       table will only be accessed by a single thread.
  //   SYMTAB_SINGLE_LOCK - implementation should use a single lock for the
  //                        whole table.
  //   SYMTAB_LOCK_BUCKET_GROUPS - implementation should use 16 locks and
  //                               assign a group of buckets to each lock.
  //   SYMTAB_ALLOW_CONCURRENT_READS - implementation should use 16 locks and
  //                                   assign a group of buckets to each lock,
  //                                   but should allow concurrent reads within
  //                                   a group.
  //   SYMTAB_USE_THIN_LOCKS -  implementation should use "16" thin locks and
  //                            assign a group of buckets to each lock.
  //
  // This routine should only be used in single-threaded mode.
  //
#define SYMTAB_NO_LOCKING 0
#define SYMTAB_SINGLE_LOCK 1
#define SYMTAB_LOCK_BUCKET_GROUPS 2
#define SYMTAB_ALLOW_CONCURRENT_READS 3
#define SYMTAB_USE_THIN_LOCKS 4

void symtabDelete(void *symtabHandle);
  // Deletes a symbol table.
  // Reclaims all memory used by the table.
  // Note that the memory associate with data items is not reclaimed since
  //   the symbol table does not know the actual type of the data. It only
  //   manipulates pointers to the data.
  // Also note that no validation is made of the symbol table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // This routine should only be used in single-threaded mode.
  //

int symtabUpdate(void *symtabHandle, const char *symbol, void *(*func)(void *));
  // Update or install a (symbol, data) pair in the table.
  // A lookup of the symbol is first done. Then the passed-in function
  //   is called with either NULL for its parameter if the symbol was not
  //   previously installed or the installed data pointer for its parameter
  //   if the symbol was previously installed. The function return value
  //   is then used as the data pointer to update or install the (symbol, data)
  //   pair.
  // If the symbol was not already installed, then space is allocated and
  //   a copy is made of the symbol, and the (symbol, data) pair is then
  //   installed in the table.
  // If successful, returns 1.
  // If memory cannot be allocated for a new symbol or if the passed-in
  // function returns NULL, then return 0.
  // Note that no validation is made of the symbol table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // This routine is designed to be used in multi-threaded mode. 


void *symtabLookup(void *symtabHandle, const char *symbol);
  // Return the data item stored with the given symbol.
  // If the symbol is found, return the associated data item.
  // If the symbol is not found, returns NULL.
  // Note that no validation is made of the symbol table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // This routine is designed to be used in multi-threaded mode. 

void *symtabCreateIterator(void *symtabHandle);
  // Create an iterator for the contents of the symbol table.
  // If successful, a handle to the iterator is returned which can be
  // repeatedly passed to symtableNext to iterate over the contents
  // of the table.
  // If memory cannot be allocated for the iterator, returns NULL.
  // Note that no validation is made of the symbol table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // This routine should only be used in single-threaded mode.
  //

const char *symtabNext(void *iteratorHandle, void **returnData);
  // Returns the next (symbol, data) pair for the iterator.
  // The symbol is returned as the return value and the data item
  // is placed in the location indicated by the second parameter.
  // If the whole table has already been traversed then NULL is
  //   returned and the location indicated by the second paramter
  //   is not modified.
  // Note that no validation is made of the iterator table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // Also note that if there has been a symbtabInstall call since the
  //   iterator was created, the behavior is undefined (but probably
  //   benign).
  // This routine should only be used in single-threaded mode.
  //

void symtabDeleteIterator(void *iteratorHandle);
  // Delete the iterator indicated by the only parameter.
  // Reclaims all memory used by the iterator.
  // Note that no validation is made of the iterator table handle passed
  //   in. If not a valid handle, then the behavior is undefined (but
  //   probably bad).
  // This routine should only be used in single-threaded mode.
  //

