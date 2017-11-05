/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"


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
// number of normal vehicles waiting on the left hand side of the bridge
int waiting_left = 0;
// number of emergency vehicles waiting on the left hand side of the bridge
int waiting_emergency_left = 0;
// number of normal vehicles waiting on the right hand side of the bridge
int waiting_right = 0;
// number of emergency vehicles waiting on the right hand side of the bridge
int waiting_emergency_right = 0;
// number of normal vehicles driving from the left hand side of the bridge
int driving_left = 0;
// number of emergency vehicles driving from the left hand side of the bridge
int driving_emergency_left = 0;
// number of normal vehicles driving from the right hand side of the bridge
int driving_right = 0;
// number of emergency vehicles driving from the right hand side of the bridge
int driving_emergency_right = 0;

// semaphore to update variables safely
struct semaphore mutex;
// semaphore to indicate that a vehicle on the left can drive
struct semaphore ticket_left;
// semaphore to indicate that a emergency vehicle on the left can drive
struct semaphore ticker_emergency_left;
// semaphore to indicate that a vehicle on the right can drive
struct semaphore ticket_vehicle_right;
// semaphore to indicate that a emergency vehicle on the right can drive
struct semaphore ticket_emergency_right;

/*TODO: add a variable (probably bool) to indicate the turn e.g. "right_turn" */

void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
  sema_init(&ticket_left, 0);
  sema_init(&ticket_right, 0);
  sema_init(&ticket_emergency_left, 0);
  sema_init(&ticket_emergency_right, 0);
  sema_init(&mutex, 1);

  // TODO start Threads
}

void OneVehicle(int direc, int prio){
  ArriveBridge(direc, prio);
  CrossBridge(direc, prio);
  ExitBridge(direc, prio);
}

void ArriveBridge_car(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // vehicle on the left side case:
    if ((driving_left < max_bridge_capacity) && (driving_right + waiting_right == 0)){
      // Generate ticket for one of the cars on the left side
      driving_left += 1;
      sema_up(ticket_left);
    }else{
      // just add car to the waiting cars on left side
      waiting_left += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side is available
    sema_down(&ticket_left);

  }else{
    // Vehicle on the right side case:
    if ((driving_right < max_bridge_capacity) && (driving_right + waiting_right == 0)){
      // Generate ticket for one of the cars on the right side
      driving_right += 1;
      sema_up(ticket_right);
    }else{
      // just add car to the waiting cars on right side
      waiting_right += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side is available
    sema_down(&ticket_right);
  }
}

void ExitBridge_car(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // Vehicle coming from the left side case:
    driving_left -= 1;
    if ((driving_left == 0) && (waiting_right > 0)){
      // Vehicle was the last driving car from the left
      // and there are waiting cars on the other side
      // -> wake up up to 3 cars from the other side
      int i = 0;
      while ((i < 3) && (waiting_right > 0)){
        driving_right += 1;
        waiting_right -= 1;
        sema_up(ticket_right);
        i += 1;
      }
    } else{
      // Vehicle was not the last car driving from the left
      // or there are no cars waiting on the other side
      if ((waiting_left > 0) && (waiting_right == 0)){
        // if there are cars waiting on the same side just let
        // one of them go through
        sema_up(ticket_left);
        driving_left += 1;
        waiting_left -= 1;
      }
    }
  }else{
    // Vehicle coming from the right side case:
    driving_right -= 1;
    if ((driving_right == 0) && (waiting_left > 0)){
      // Vehicle was the last driving car from the right
      // and there are waiting cars on the other side
      // -> wake up up to 3 cars from the other side
      int i = 0;
      while ((i < 3) && (waiting_left > 0)){
        driving_left += 1;
        waiting_left -= 1;
        sema_up(ticket_left);
        i += 1;
      }
    } else{
      // Vehicle was not the last car driving from the right
      // or there are no cars waiting on the other side
      if ((waiting_right > 0) && (waiting_left == 0)){
        // if there are cars waiting on the same side just let
        // one of them go through
        sema_up(ticket_right);
        driving_right += 1;
        waiting_right -= 1;
      }
    }

  }
  sema_up(&mutex);
}


void ArriveBridge_emergency(unsigned int direc){

}

void ArriveBridge(unsigned int direc, unsigned int prio){
  if (prio == 1){
    ArriveBridge_emergency(direc);
  }
  else{
    ArriveBridge_car(direc);
  }
}

void CrossBridge(unsigned int direc, unsigned int prio){
  int id = rand();
  printf("X Vehicle with prio %u and direction %u entered bridge (DEBUG_ID=%i)\n", direc, prio, id);
  int r = rand() % 2000;
  timer_msleep(r);
  printf("O Vehicle with prio %u and direction %u left bridge (DEBUG_ID=%i)\n", direc, prio, id);
}

void ExitBridge(unsigned int direc, unsigned int prio){
  if (prio == 1){
    ExitBridge_emergency(direc);
  }
  else{
    ExitBridge_car(direc);
  }
}
