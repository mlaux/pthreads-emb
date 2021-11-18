#include <MacTypes.h>
#include <Memory.h>
#include <MixedMode.h>
#include <Threads.h>

#include "pte_osal.h"

// Mac OS doesn't support preemptive multitasking and only switches threads
// when we explicitly call SetThreadState, YieldToThread, or YieldToAnyThread.
// This is terrible but turns a lot of these functions into no-ops.

// Mac-specific stuff for supporting atomic operations taken from Apple's
// InterruptDisableLib example
// http://mirror.informatimago.com/next/developer.apple.com/technotes/tn/tn1137.html

enum {
  kGetSRProcInfo = kRegisterBased
      | RESULT_SIZE(SIZE_CODE(sizeof(UInt16)))
      | REGISTER_RESULT_LOCATION(kRegisterD0),
  kSetSRProcInfo = kRegisterBased
      | RESULT_SIZE(0)
      | REGISTER_ROUTINE_PARAMETER(1, kRegisterD0, SIZE_CODE(sizeof(UInt16)))
};

// We define the 68K as a statically initialised data structure.
// The use of MixedMode to call these routines makes the routines
// themselves very simple.
static UInt16 gGetSR[] = {
  0x40c0,		// move sr,d0
  0x4e75		// rts
};

static UInt16 gSetSR[] = {
  0x46c0,		// move d0,sr
  0x4e75		// rts
};

// Returns the current value of the SR, interrupt mask
// and all!  This routine uses MixedMode to call the gGetSR data
// structure as if it was 68K code (which it is!).
static UInt16 GetSR(void)
{
  return (UInt16) CallUniversalProc((UniversalProcPtr) &gGetSR, kGetSRProcInfo);
}

// Returns the value of the SR, including the interrupt mask and all
// the flag bits.  This routine uses MixedMode to call the gGetSR data
// structure as if it was 68K code (which it is!).
static void SetSR(UInt16 newSR)
{
  CallUniversalProc((UniversalProcPtr) &gSetSR, kSetSRProcInfo, newSR);
}

static UInt16 SetInterruptMask(UInt16 newMask)
{
	UInt16 currentSR;

	currentSR = GetSR();
	SetSR((UInt16) ((currentSR & 0xF8FF) | (newMask << 8)));

	return (UInt16) ((currentSR >> 8) & 7);
}

#define MAX_THREADS 32

// windows supports a minimum of 64 TLS values, that seems good for us
#define MAX_VALUES 64

struct tls_value {
  int in_use;
  void *value;
};

struct macos_thread {
  short in_use;
  ThreadID id;
  struct tls_value tls[MAX_VALUES];
  // todo semaphores for wait and cancel
};

struct entry_point_data {
  pte_osThreadEntryPoint entry_point;
  void *param;
};

static struct macos_thread threads[MAX_THREADS];

static struct macos_thread *alloc_thread(ThreadID id)
{
  int k, t;
  for (k = 0; k < MAX_THREADS; k++) {
    if (!threads[k].in_use) {
      threads[k].in_use = 1;
      threads[k].id = id;
      for (t = 0; t < MAX_VALUES; t++) {
        threads[k].tls[t].in_use = 0;
      }
      return &threads[k];
    }
  }

  // out of slots, how to handle?
  return NULL;
}

static struct macos_thread *find_thread(ThreadID id)
{
  int k;
  for (k = 0; k < MAX_THREADS; k++) {
    if (threads[k].in_use && threads[k].id == id) {
      return &threads[k];
    }
  }
  return NULL;
}

static struct macos_thread *current_thread(void)
{
  ThreadID cur_tid;
  cur_tid = pte_osThreadGetHandle();
  return find_thread(cur_tid);
}

static void free_thread(ThreadID id)
{
  struct macos_thread *thread = find_thread(id);
  thread->in_use = 0;
}

static void *thread_entry_point(void *param)
{
  struct entry_point_data *epd = (struct entry_point_data *) param;
  int return_val = epd->entry_point(epd->param);
  free_thread(pte_osThreadGetHandle());
  return (void *) return_val;
}

/**
 * Provides a hook for the OSAL to implement any OS specific initialization.  This is guaranteed to be
 * called before any other OSAL function.
 */
pte_osResult pte_osInit(void)
{
  MaxApplZone();
}

/** @name Mutexes */
//@{

/**
 * Creates a mutex
 *
 * @param pHandle  Set to the handle of the newly created mutex.
 *
 * @return PTE_OS_OK - Mutex successfully created
 * @return PTE_OS_NO_RESOURCESs - Insufficient resources to create mutex
 */
pte_osResult pte_osMutexCreate(pte_osMutexHandle *pHandle)
{

}

/**
 * Deletes a mutex and frees any associated resources.
 *
 * @param handle Handle of mutex to delete.
 *
 * @return PTE_OS_OK - Mutex successfully deleted.
 */
pte_osResult pte_osMutexDelete(pte_osMutexHandle handle)
{

}

/**
 * Locks the mutex
 *
 * @param handle Handle of mutex to lock.
 *
 * @return PTE_OS_OK - Mutex successfully locked.
 */
pte_osResult pte_osMutexLock(pte_osMutexHandle handle)
{

}

/**
 * Locks the mutex, returning after @p timeoutMsecs if the resources is not
 * available.  Can be used for polling mutex by using @p timeoutMsecs of zero.
 *
 * @param handle Handle of mutex to lock.
 * @param timeoutMsecs Number of milliseconds to wait for resource before returning.
 *
 * @return PTE_OS_OK - Mutex successfully locked.
 * @return PTE_OS_TIMEOUT - Timeout expired before lock was obtained.
 */
pte_osResult pte_osMutexTimedLock(pte_osMutexHandle handle, unsigned int timeoutMsecs)
{

}

/**
 * Unlocks the mutex
 *
 * @param handle Handle of mutex to unlock
 *
 * @return PTE_OS_OK - Mutex successfully unlocked.
 */
pte_osResult pte_osMutexUnlock(pte_osMutexHandle handle)
{

}
//@}

/** @name Threads */
//@{

/**
 * Creates a new thread.  The thread must be started in a suspended state - it will be
 * explicitly started when pte_osThreadStart() is called.
 *
 * @param entryPoint Entry point to the new thread.
 * @param stackSize The initial stack size, in bytes.  Note that this can be considered a minimum -
 *                  for instance if the OS requires a larger stack space than what the caller specified.
 * @param initialPriority The priority that the new thread should be initially set to.
 * @param argv Parameter to pass to the new thread.
 * @param ppte_osThreadHandle set to the handle of the new thread.
 *
 * @return PTE_OS_OK - New thread successfully created.
 * @return PTE_OS_NO_RESOURCES - Insufficient resources to create thread
 */
pte_osResult pte_osThreadCreate(pte_osThreadEntryPoint entryPoint,
                                int stackSize,
                                int initialPriority,
                                void *argv,
                                pte_osThreadHandle* ppte_osThreadHandle)
{
  OSErr err;
  struct entry_point_data epd;

  epd.entry_point = entryPoint;
  epd.param = argv;

  err = NewThread(
    kCooperativeThread,
    (ThreadEntryProcPtr) thread_entry_point,
    &epd, 
    stackSize,
    kNewSuspend | kCreateIfNeeded,
    nil,
    ppte_osThreadHandle
  );

  switch (err) {
    case noErr: {
      // create slot for TLS, etc
      struct macos_thread *pt;
      pt = alloc_thread(*ppte_osThreadHandle);
      if (!pt) {
        DisposeThread(*ppte_osThreadHandle, nil, false);
        return PTE_OS_NO_RESOURCES;
      }
      return PTE_OS_OK;
    }
    case memFullErr: return PTE_OS_NO_RESOURCES;
    case paramErr:   return PTE_OS_INVALID_PARAM;
  }
  return PTE_OS_GENERAL_FAILURE;
}

/**
 * Starts executing the specified thread.
 *
 * @param osThreadHandle handle of the thread to start.
 *
 * @return PTE_OS_OK - thread successfully started.
 */
pte_osResult pte_osThreadStart(pte_osThreadHandle osThreadHandle)
{
  SetThreadState(osThreadHandle, kReadyThreadState, kNoThreadID);
}

/**
 * Causes the current thread to stop executing.
 *
 * @return Never returns (thread terminated)
 */
void pte_osThreadExit()
{
  SetThreadState(kCurrentThreadID, kStoppedThreadState, kNoThreadID);
}

/**
 * Waits for the specified thread to end.  If the thread has already terminated, this returns
 * immediately.
 *
 * @param threadHandle Handle fo thread to wait for.
 *
 * @return PTE_OS_OK - specified thread terminated.
 */
pte_osResult pte_osThreadWaitForEnd(pte_osThreadHandle threadHandle)
{
  OSErr err;
  ThreadState state;

  do {
    err = GetThreadState(threadHandle, &state);
    if (err == threadNotFoundErr) {
      // assume thread not found means it already exited... is that ok
      return PTE_OS_OK;
    } else if (err != noErr) {
      return PTE_OS_GENERAL_FAILURE;
    }

    YieldToAnyThread(); // is this ok here?
  } while (state != kStoppedThreadState);

  return PTE_OS_OK;
}

/**
 * Returns the handle of the currently executing thread.
 */
pte_osThreadHandle pte_osThreadGetHandle(void)
{
  OSErr err;
  ThreadID id;
  err = MacGetCurrentThread(&id);

  if (err == threadNotFoundErr) {
    // ???
  }

  return id;

  // I think this might be fine since any other functions accepting the thread
  // id as a parameter work with kCurrentThreadID, so we don't need to do
  // a lookup to get the actual ID
  // return kCurrentThreadID;
}

/**
 * Returns the priority of the specified thread.
 */
int pte_osThreadGetPriority(pte_osThreadHandle threadHandle)
{
  // MacOS doesn't support priority
  // Future TODO: associate a priority with each thread on our side, and use
  // the priorities to decide which thread to suggest next for calls to 
  // SetThreadState() and YieldToThread()
  return 0;
}

/**
 * Sets the priority of the specified thread.
 *
 * @return PTE_OS_OK - thread priority successfully set
 */
pte_osResult pte_osThreadSetPriority(pte_osThreadHandle threadHandle, int newPriority)
{
  // MacOS doesn't support priority
  return PTE_OS_OK;
}

/**
 * Frees resources associated with the specified thread.  This is called after the thread has terminated
 * and is no longer needed (e.g. after pthread_join returns).  This call will always be made
 * from a different context than that of the target thread.
 */
pte_osResult pte_osThreadDelete(pte_osThreadHandle handle)
{
  free_thread(handle); // free our local storage
  DisposeThread(handle, nil, false);
}

/**
 * Frees resources associated with the specified thread and then causes the thread to exit.
 * This is called after the thread has terminated and is no longer needed (e.g. after
 * pthread_join returns).  This call will always be made from the context of the target thread.
 */
pte_osResult pte_osThreadExitAndDelete(pte_osThreadHandle handle)
{

}

/**
 * Cancels the specified thread.  This should cause pte_osSemaphoreCancellablePend() and for pte_osThreadCheckCancel()
 * to return @p PTE_OS_INTERRUPTED.
 *
 * @param threadHandle handle to the thread to cancel.
 *
 * @return Thread successfully canceled.
 */
pte_osResult pte_osThreadCancel(pte_osThreadHandle threadHandle)
{

}

/**
 * Check if pte_osThreadCancel() has been called on the specified thread.
 *
 * @param threadHandle handle of thread to check the state of.
 *
 * @return PTE_OS_OK - Thread has not been cancelled
 * @return PTE_OS_INTERRUPTED - Thread has been cancelled.
 */
pte_osResult pte_osThreadCheckCancel(pte_osThreadHandle threadHandle)
{

}

/**
 * Causes the current thread to sleep for the specified number of milliseconds.
 */
void pte_osThreadSleep(unsigned int msecs)
{

}

/**
 * Returns the maximum allowable priority
 */
int pte_osThreadGetMaxPriority()
{
  // MacOS doesn't support priority
  return 0;
}

/**
 * Returns the minimum allowable priority
 */
int pte_osThreadGetMinPriority()
{
  // MacOS doesn't support priority
  return 0;
}

/**
 * Returns the priority that should be used if the caller to pthread_create doesn't
 * explicitly set one.
 */
int pte_osThreadGetDefaultPriority()
{
  // MacOS doesn't support priority
  return 0;
}

//@}


/** @name Semaphores */
//@{

/**
 * Creates a semaphore
 *
 * @param initialValue Initial value of the semaphore
 * @param pHandle  Set to the handle of the newly created semaphore.
 *
 * @return PTE_OS_OK - Semaphore successfully created
 * @return PTE_OS_NO_RESOURCESs - Insufficient resources to create semaphore
 */
pte_osResult pte_osSemaphoreCreate(int initialValue, pte_osSemaphoreHandle *pHandle)
{

}

/**
 * Deletes a semaphore and frees any associated resources.
 *
 * @param handle Handle of semaphore to delete.
 *
 * @return PTE_OS_OK - Semaphore successfully deleted.
 */
pte_osResult pte_osSemaphoreDelete(pte_osSemaphoreHandle handle)
{

}

/**
 * Posts to the semaphore
 *
 * @param handle Semaphore to release
 * @param count  Amount to increment the semaphore by.
 *
 * @return PTE_OS_OK - semaphore successfully released.
 */
pte_osResult pte_osSemaphorePost(pte_osSemaphoreHandle handle, int count)
{

}

/**
 * Acquire a semaphore, returning after @p timeoutMsecs if the semaphore is not
 * available.  Can be used for polling a semaphore by using @p timeoutMsecs of zero.
 *
 * @param handle Handle of semaphore to acquire.
 * @param pTimeout Pointer to the number of milliseconds to wait to acquire the semaphore
 *                 before returning.  If set to NULL, wait forever.
 *
 * @return PTE_OS_OK - Semaphore successfully acquired.
 * @return PTE_OS_TIMEOUT - Timeout expired before semaphore was obtained.
 */
pte_osResult pte_osSemaphorePend(pte_osSemaphoreHandle handle, unsigned int *pTimeout)
{

}

/**
 * Acquire a semaphore, returning after @p timeoutMsecs if the semaphore is not
 * available.  Can be used for polling a semaphore by using @p timeoutMsecs of zero.
 * Call must return immediately if pte_osThreadCancel() is called on the thread waiting for
 * the semaphore.
 *
 * @param handle Handle of semaphore to acquire.
 * @param pTimeout Pointer to the number of milliseconds to wait to acquire the semaphore
 *                 before returning.  If set to NULL, wait forever.
 *
 * @return PTE_OS_OK - Semaphore successfully acquired.
 * @return PTE_OS_TIMEOUT - Timeout expired before semaphore was obtained.
 */
pte_osResult pte_osSemaphoreCancellablePend(pte_osSemaphoreHandle handle, unsigned int *pTimeout)
{

}
//@}


/** @name Thread Local Storage */
//@{

/**
 * Initializes the OS TLS support.  This is called by the PTE library
 * prior to performing ANY TLS operation.
 */
void pte_osTlsInit(void)
{

}

/**
 * Sets the thread specific value for the specified key for the
 * currently executing thread.
 *
 * @param index The TLS key for the value.
 * @param value The value to save
 */
pte_osResult pte_osTlsSetValue(unsigned int key, void * value)
{
  struct macos_thread *thread;

  thread = current_thread();
  if (!thread || !thread->tls[key].in_use) {
    return PTE_OS_INVALID_PARAM;
  }

  thread->tls[key].value = value;

  return PTE_OS_OK;
}

/**
 * Retrieves the thread specific value for the specified key for
 * the currently executing thread.  If a value has not been set
 * for this key, NULL should be returned (i.e. TLS values default
 * to NULL).
 *
 * @param index The TLS key for the value.
 *
 * @return The value associated with @p key for the current thread.
 */
void * pte_osTlsGetValue(unsigned int key)
{
  struct macos_thread *thread;

  thread = current_thread();
  if (!thread || !thread->tls[key].in_use) {
    return NULL;
  }

  return thread->tls[key].value;
}

/**
 * Allocates a new TLS key.
 *
 * @param pKey On success will be set to the newly allocated key.
 *
 * @return PTE_OS_OK - TLS key successfully allocated.
 * @return PTE_OS_NO_RESOURCESs - Insufficient resources to allocate key (e.g.
 *                         maximum number of keys reached).
 */
pte_osResult pte_osTlsAlloc(unsigned int *pKey)
{
  struct macos_thread *thread;
  int k;

  thread = current_thread();
  if (!thread) {
    return PTE_OS_INVALID_PARAM;
  }

  for (k = 0; k < MAX_VALUES; k++) {
    if (!thread->tls[k].in_use) {
      thread->tls[k].in_use = 1;
      *pKey = k;
      return PTE_OS_OK;
    }
  }

  return PTE_OS_NO_RESOURCES;
}

/**
 * Frees the specified TLS key.
 *
 * @param index TLS key to free
 *
 * @return PTE_OS_OK - TLS key was successfully freed.
 */
pte_osResult pte_osTlsFree(unsigned int key)
{
  struct macos_thread *thread;

  thread = current_thread();
  if (!thread || !thread->tls[key].in_use) {
    return PTE_OS_INVALID_PARAM;
  }

  thread->tls[key].in_use = 0;
  thread->tls[key].value = NULL;
  return PTE_OS_OK;
}
//@}

/** @name Atomic operations */
//@{

/**
 * Sets the target to the specified value as an atomic operation.
 *
 * \code
 * origVal = *ptarg
 * *ptarg = val
 * return origVal
 * \endcode
 *
 * @param pTarg Pointer to the value to be exchanged.
 * @param val Value to be exchanged
 *
 * @return original value of destination
 */
int pte_osAtomicExchange(int *pTarg, int val)
{
  UInt16 oldMask;
  int origVal;

  oldMask = SetInterruptMask(7);

  origVal = *pTarg;
  *pTarg = val;

  SetInterruptMask(oldMask);

  return origVal;
}

/**
 * Performs an atomic compare-and-exchange oepration on the specified
 * value.  That is:
 *
 * \code
 * origVal = *pdest
 * if (*pdest == comp)
 *   then *pdest = exchange
 * return origVal
 * \endcode
 *
 * @param pdest Pointer to the destination value.
 * @param exchange Exchange value (value to set destination to if destination == comparand)
 * @param comp The value to compare to destination.
 *
 * @return Original value of destination
 */
int pte_osAtomicCompareExchange(int *pdest, int exchange, int comp)
{
  UInt16 oldMask;
  int origVal;

  oldMask = SetInterruptMask(7);

  origVal = *pdest;
  if (origVal == comp) {
    *pdest = exchange;
  }

  SetInterruptMask(oldMask);

  return origVal;
}

/**
 * Adds the value to target as an atomic operation
 *
 * \code
 * origVal = *pdest
 * *pAddend += value
 * return origVal
 * \endcode
 *
 * @param pdest Pointer to the variable to be updated.
 * @param value Value to be added to the variable.
 *
 * @return Original value of destination
 */
int  pte_osAtomicExchangeAdd(int volatile* pdest, int value)
{
  UInt16 oldMask;
  int origVal;

  oldMask = SetInterruptMask(7);

  origVal = *pdest;
  *pdest += value;

  SetInterruptMask(oldMask);

  return origVal;
}

/**
 * Decrements the destination.
 *
 * \code
 * origVal = *pdest
 * *pdest++
 * return origVal
 * \endcode
 *
 * @param pdest Destination value to decrement
 *
 * @return Original destination value
 */
int pte_osAtomicDecrement(int *pdest)
{
  UInt16 oldMask;
  int origVal;

  oldMask = SetInterruptMask(7);

  origVal = *pdest;
  *pdest--;

  SetInterruptMask(oldMask);

  return origVal;
}

/**
 * Increments the destination value
 *
 * \code
 * origVal = *pdest;
 * *pdest++;
 * return origVal;
 */
int pte_osAtomicIncrement(int *pdest)
{
  UInt16 oldMask;
  int origVal;

  oldMask = SetInterruptMask(7);

  origVal = *pdest;
  *pdest++;

  SetInterruptMask(oldMask);

  return origVal;
}
//@}

// struct timeb;

// int ftime(struct timeb *tb);

#ifdef __cplusplus
}
#endif // __cplusplus
