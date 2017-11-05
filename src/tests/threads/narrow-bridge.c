/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "lib/random.h"


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
// semaphore to indicate that a vehicle on the right can drive
struct semaphore ticket_right;
// semaphore to indicate that a emergency vehicle on the left can drive
struct semaphore ticket_emergency_left;
// semaphore to indicate that a emergency vehicle on the right can drive
struct semaphore ticket_emergency_right;

/*TODO: add a variable (probably bool) to indicate the turn e.g. "right_turn" */




void ArriveBridge_car(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // vehicle on the left side case:
    // TODO: check if the no emergency vehicle is driving check if this is necessary
    if ((driving_left < max_bridge_capacity) &&
        (driving_right + waiting_right == 0) &&
        (waiting_emergency_left + waiting_emergency_right 
	+ driving_emergency_left + driving_emergency_right == 0)) 
    {
      // Generate ticket for one of the cars on the left side
      driving_left += 1;
      sema_up(&ticket_left);
    }else{
      // just add car to the waiting cars on left side
      waiting_left += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side becomes available
    sema_down(&ticket_left);

  }else{
    // Vehicle on the right side case:
    // TODO: check if the no emergency vehicle is driving check if this is necessary
    if ((driving_right < max_bridge_capacity) &&
        (driving_left + waiting_left == 0) &&
	(waiting_emergency_left + waiting_emergency_right 
	+ driving_emergency_left + driving_emergency_right == 0))
    {
      // Generate ticket for one of the cars on the right side
      driving_right += 1;
      sema_up(&ticket_right);
    }else{
      // just add car to the waiting cars on right side
      waiting_right += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side becomes available
    sema_down(&ticket_right);
  }
}

void ExitBridge_car(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // Vehicle coming from the left side case:
    driving_left -= 1;
    if (waiting_emergency_right + waiting_emergency_left > 0){
        // case if there are emergency vehicles waiting
        if (driving_left == 0){
          // case if there are emergency vehicles waiting and last car exited
          WakeUp_emergencies();
        }
        // the else case does nothing, because cars are still driving
    }else{
      // case if no emergency vehicles are waiting
      if ((driving_left == 0) && (waiting_right > 0)){
        // Vehicle was the last driving car from the left
        // and there are waiting cars on the other side
        // -> wake up up to 3 cars from the other side
        int i = 0;
        while ((i < max_bridge_capacity) && (waiting_right > 0)){
          driving_right += 1;
          waiting_right -= 1;
          sema_up(&ticket_right);
          i += 1;
        }
      }else{
        // Vehicle was not the last car driving from the left
        // or there are no cars waiting on the other side
        if ((waiting_left > 0) && (waiting_right == 0)){
          // if there are cars waiting on the same side just let
          // one of them go through
          sema_up(&ticket_left);
          driving_left += 1;
          waiting_left -= 1;
        }
      }
    }
  }else{
    // Vehicle coming from the right side case:
    driving_right -= 1;
    if (waiting_emergency_right + waiting_emergency_left > 0){
      // case if there are emergency vehicles waiting
        if (driving_right == 0){
          // case if there are emergency vehicles waiting and last car exited
          WakeUp_emergencies();
        }
        // the else case does nothing, because cars are still driving
    }else{
      if ((driving_right == 0) && (waiting_left > 0)){
        // Vehicle was the last driving car from the right
        // and there are waiting cars on the other side
        // -> wake up up to 3 cars from the other side
        int i = 0;
        while ((i < max_bridge_capacity) && (waiting_left > 0)){
          driving_left += 1;
          waiting_left -= 1;
          sema_up(&ticket_left);
          i += 1;
        }
      }else{
        // Vehicle was not the last car driving from the right
        // or there are no cars waiting on the other side
        if ((waiting_right > 0) && (waiting_left == 0)){
          // if there are cars waiting on the same side just let
          // one of them go through
          sema_up(&ticket_right);
          driving_right += 1;
          waiting_right -= 1;
        }
      }
    }
  }
  sema_up(&mutex);
}
   


void ArriveBridge_emergency(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // emergency on the left side case:
    if ((driving_emergency_left < max_bridge_capacity) &&
        (driving_emergency_right + waiting_emergency_right == 0) &&
        (driving_right + driving_left == 0)) 
    {
      // Generate ticket for one of the emergencies on the left side
      driving_emergency_left += 1;
      sema_up(&ticket_emergency_left);
    }else{
      // just add emergency to the waiting emergencies on left side
      waiting_emergency_left += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side becomes available
    sema_down(&ticket_emergency_left);

  }else{
    // emergency on the right side case:
    if ((driving_emergency_right < max_bridge_capacity) &&
        (driving_emergency_left + waiting_emergency_left == 0) &&
        (driving_right + driving_left == 0)) 
    {
      // Generate ticket for one of the emergencies on the right side
      driving_emergency_right += 1;
      sema_up(&ticket_emergency_right);
    }else{
      // just add emergency to the waiting emergencies on right side
      waiting_emergency_right += 1;
    }
    sema_up(&mutex);
    // start driving if a ticket on this side becomes available
    sema_down(&ticket_emergency_right);
  }

}



void ExitBridge_emergency(unsigned int direc){
  sema_down(&mutex);

  if (direc == 0){
    // Emergency coming from the left side case:
    driving_emergency_left -= 1;

    if ((driving_emergency_left == 0) && (waiting_emergency_right > 0)){
      // Emergency was the last driving Emergency from the left
      // and there are waiting Emergencies on the other side
      // -> wake up up to 3 Emergencies from the other side
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_emergency_right > 0)){
        driving_emergency_right += 1;
        waiting_emergency_right -= 1;
        sema_up(&ticket_emergency_right);
        i += 1;
      }
    }else{
      // Emergency was not the last Emergency driving from the left
      // or there are no Emergencies waiting on the other side
      if ((waiting_emergency_left > 0) && (waiting_emergency_right == 0)){
        // if there are Emergencies waiting on the same side just let
        // one of them go through
        sema_up(&ticket_emergency_left);
        driving_emergency_left += 1;
        waiting_emergency_left -= 1;
      }
      if (waiting_emergency_right + waiting_emergency_left +
          driving_emergency_left + driving_emergency_right == 0){
        WakeUp_cars();
      }
      // the case of waiting_emergency_left == 0 and
      // driving_emergency_left > 0 has to do "nothing" here
    }
  }else{
    // Emergency coming from the right side case:
    driving_emergency_right -= 1;

    if ((driving_emergency_right == 0) && (waiting_emergency_left > 0)){
      // Emergency was the last driving Emergency from the right
      // and there are waiting Emergencies on the other side
      // -> wake up up to 3 Emergencies from the other side
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_emergency_left > 0)){
        driving_emergency_left += 1;
        waiting_emergency_left -= 1;
        sema_up(&ticket_emergency_left);
        i += 1;
      }
    }else{
      // Emergency was not the last Emergency driving from the right
      // or there are no Emergencies waiting on the other side
      if ((waiting_emergency_right > 0) && (waiting_emergency_left == 0)){
        // if there are Emergencies waiting on the same side just let
        // one of them go through
        sema_up(&ticket_emergency_right);
        driving_emergency_right += 1;
        waiting_emergency_right -= 1;
      }
      if (waiting_emergency_right + waiting_emergency_left +
          driving_emergency_left + driving_emergency_right == 0){
        WakeUp_cars();
      }
      // the case of waiting_emergency_right == 0 and
      // driving_emergency_right > 0 has to do "nothing" here
    }
  }
  sema_up(&mutex);
}



// this function takes care of waking up car vehicles "fairly"
// by chosing the side on which more cars vehicles are
// currently waiting
// this function is only called when the mutex is already helt!
void WakeUp_cars(){
  // this function should only be called if no vehicles are
  // currently driving.
  ASSERT(driving_left == 0);
  ASSERT(driving_right == 0);
  ASSERT(driving_emergency_left == 0);
  ASSERT(driving_emergency_right == 0);

  if (waiting_right + waiting_left > 0){
    // case if there are any cars vehicles waiting
    if (waiting_right > waiting_left){
      // more cars waiting on the right side: wake them up
      // if possible!
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_right > 0)){
        driving_right += 1;
        waiting_right -= 1;
        sema_up(&ticket_right);
        i += 1;
      }
    }else{
      // more cars waiting on the left side: wake them up
      // if possible!
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_left > 0)){
        driving_left += 1;
        waiting_left -= 1;
        sema_up(&ticket_left);
        i += 1;
      }
    }
  }
}     


// this function takes care of waking up emergency vehicles "fairly"
// by chosing the side on which more emergency vehicles are
// currently waiting
// this function is only called when the mutex is already helt!
void WakeUp_emergencies(){
  // this function should only be called if no other vehicles are
  // currently driving.
  ASSERT(driving_left == 0);
  ASSERT(driving_right == 0);
  ASSERT(driving_emergency_left == 0);
  ASSERT(driving_emergency_right == 0);

  if (waiting_emergency_right + waiting_emergency_left > 0){
    // case if there are emergency vehicles waiting
    if (waiting_emergency_right > waiting_emergency_left){
      // more emergencys waiting on the right side: wake them up
      // if possible!
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_emergency_right > 0)){
        driving_emergency_right += 1;
        waiting_emergency_right -= 1;
        sema_up(&ticket_emergency_right);
        i += 1;
      }
    }else{
      // more emergencies waiting on the left side: wake them up
      // if possible!
      int i = 0;
      while ((i < max_bridge_capacity) && (waiting_emergency_left > 0)){
        driving_emergency_left += 1;
        waiting_emergency_left -= 1;
        sema_up(&ticket_emergency_left);
        i += 1;
      }
    }
  }
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
  int random_id;
  random_bytes(&random_id, sizeof random_id);
  printf("X Vehicle with prio %u and direction %u entered bridge (DEBUG_ID=%i)\n", direc, prio, random_id);
  int random_wait_time;
  random_bytes(&random_id, sizeof random_id);
  random_bytes(&random_wait_time, sizeof random_wait_time);
  random_wait_time = random_wait_time % 2000;
  timer_msleep(random_wait_time);
  printf("O Vehicle with prio %u and direction %u left bridge (DEBUG_ID=%i)\n", direc, prio, random_id);
}

void ExitBridge(unsigned int direc, unsigned int prio){
  if (prio == 1){
    ExitBridge_emergency(direc);
  }
  else{
    ExitBridge_car(direc);
  }
}


void OneVehicle(int *arguments){
  int direc = arguments[0];
  int prio = arguments[1];
  ArriveBridge(direc, prio);
  CrossBridge(direc, prio);
  ExitBridge(direc, prio);
}




void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
  sema_init(&ticket_left, 0);
  sema_init(&ticket_right, 0);
  sema_init(&ticket_emergency_left, 0);
  sema_init(&ticket_emergency_right, 0);
  sema_init(&mutex, 1);

  // TODO start Threads

  int argument_left_car[2] = { 0, 0};
  int argument_right_car[2] = { 1, 0};
  int argument_left_emergency[2] = { 0, 1};
  int argument_right_emergency[2] = { 1, 1};

  int i = 0;
  for (i=0; i<num_vehicles_left; i++){
    thread_create("leftVehicle_" + (char)i, 0, &OneVehicle, &argument_left_car);
  }

  for (i=0; i<num_vehicles_right; i++){
    thread_create("rightVehicle_" + (char)i, 0, &OneVehicle, &argument_right_car);
  }

  for (i=0; i<num_emergency_left; i++){
    thread_create("leftEmergency_" + (char)i, 0, &OneVehicle, &argument_left_emergency);
  }

  for (i=0; i<num_emergency_right; i++){
    thread_create("rightEmergency_" + (char)i, 0, &OneVehicle, &argument_right_emergency);
  }

  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
  timer_msleep(1000);
}
