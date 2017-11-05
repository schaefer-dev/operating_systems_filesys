/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/string.h"


void producer_consumer(unsigned int num_producer, unsigned int num_consumer);


void test_producer_consumer(void)
{
  /*producer_consumer(0, 0);
  producer_consumer(1, 0);
  producer_consumer(0, 1);
  producer_consumer(1, 1);
  producer_consumer(3, 1);
  producer_consumer(1, 3);
  producer_consumer(4, 4);
  producer_consumer(7, 2);
  producer_consumer(2, 7);*/
  producer_consumer(6, 6);
  pass();
}

// size of the buffer
int buffersize = 10;
// the buffer on which producer/consumer work
char buffer[10];
// number of elements in the buffer
int element_counter = 0;
// the next free index of the buffer
int insert_index = 0;
// the next index which will be read (FIFO)
int read_index = 0;
// condition if the buffer is not empty (elementcounter > 0)
struct condition not_empty;
// condition if the buffer is not empty (elementcounter < buffersize-1)
struct condition not_full;
// lock which guarantees mutal exclusive access to the critical path
struct lock mutex;


void producer(void *aux){
  char text[11];
  strlcpy(text, "Hello world",12);
  int i=0;
  for (i=0; i<11; i++){
    lock_acquire(&mutex);
    ASSERT (element_counter < buffersize);
    while(element_counter == buffersize-1){
      cond_wait(&not_full, &mutex);
    }
    buffer[insert_index] = text[i];
    insert_index = (insert_index + 1) % buffersize;
    element_counter += 1;
    cond_signal(&not_empty, &mutex);
    lock_release(&mutex);
  }
}

void consumer(void *aux){
  while (true){
    lock_acquire(&mutex);
    while(element_counter == 0){
      cond_wait(&not_empty, &mutex);
    }
    printf("%c",buffer[read_index]);
    //buffer[read_index] = (char)"";
    read_index = (read_index + 1) % buffersize;
    element_counter -= 1;
    cond_signal(&not_full, &mutex);
    lock_release(&mutex);
  }
}


void producer_consumer(UNUSED unsigned int num_producer,
                       UNUSED unsigned int num_consumer)
{
  // initialize lock
  lock_init(&mutex);
  // initialize condition variables
  cond_init(&not_full);
  cond_init(&not_empty);
  // struct thread producer_array[num_producer];
  unsigned int i=0;
  for (i=0; i<num_producer; i++){
    // start producer thread
    thread_create("producer_" + (char)i, 0, &producer, 0);
  }

//struct thread consumer_array[num_consumer];
  for (i=0; i<num_consumer; i++){
    // start producer thread
    thread_create("consumer_" + (char)i, 0, &consumer, 0);
  }

}
