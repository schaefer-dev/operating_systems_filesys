/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);


void test_narrow_bridge(void)
{
    /*narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);*/
    narrow_bridge(0, 10, 10, 0);
    pass();
}

// maximal possible number of vehicles on the bridge at the same time
int max_bridge_capacity = 3;
// current number of vehicles on the bridge
int vehicles_on_bridge=0;
// number of normal vehicles waiting on the left hand side of the bridge
int waiting_left = 0;
// number of emergency vehicles waiting on the left hand side of the bridge
int waiting_emergency_left = 0;
// number of normal vehicles waiting on the right hand side of the bridge
int waiting_right = 0;
// number of emergency vehicles waiting on the right hand side of the bridge
int waiting_emergency_right = 0;

// semaphore to update variables safely
struct semaphore lock;
// semaphore to indicate that a vehicle on the left can drive
struct semaphore vehicle_left;
// semaphore to indicate that a emergency vehicle on the left can drive
struct semaphore emergency_left;
// semaphore to indicate that a vehicle on the right can drive
struct semaphore vehicle_right;
// semaphore to indicate that a emergency vehicle on the right can drive
struct semaphore emergency_right;

void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
    msg("NOT IMPLEMENTED");
    /* FIXME implement */
}


