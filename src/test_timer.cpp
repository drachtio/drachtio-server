#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <cassert>

#include "sofia-sip/su.h"
#include "sofia-sip/su_wait.h"
#include "sofia-sip/su_log.h"

#include "timer-queue.hpp"

using std::cout ;
using std::endl ;
using namespace drachtio ;

TimerQueue* queue ;
su_timer_t* timer ;
su_root_t* root ;
int counter1 = 1 ;
int counter2 = 2 ;
int idx = 0 ;

void finish() {
  su_timer_destroy(timer) ;
  su_root_break(root) ;
}

void func3(void*arg){
  assert( arg ) ;
  assert( 0 == queue->size() ) ;
  cout << "OK" << endl ;

  cout << "should be able to append and then remove from back.." ;
  TimerEventHandle handle1 = queue->add( func3, NULL, 100) ;
  TimerEventHandle handle2 = queue->add( func3, NULL, 200) ;
  queue->remove( handle2 ) ;
  assert( 1 == queue->size() ) ;
  queue->remove( handle1 ) ;
  assert( 0 == queue->size() ) ;
  cout << "OK" << endl ;

  finish() ;
}

void func2( void* arg ) {
  int i = *(static_cast<int*>( arg )) ;
  idx++ ;

  if( 2 == idx && i == 1 ) {
    cout << "OK" << endl ;
    
    cout << "should be able to remove a timer from the head, middle, or tail of the queue..." ;
    TimerEventHandle handle1 = queue->add( func3, &counter1, 100) ;
    TimerEventHandle handle2 = queue->add( func3, NULL, 200) ;
    TimerEventHandle handle3 = queue->add( func3, NULL, 75) ;
    assert( 3 == queue->size() ) ;
    assert( 0 == queue->positionOf(handle3)) ;
    assert( 1 == queue->positionOf(handle1)) ;
    queue->remove( handle3 ) ;
    assert( 0 == queue->positionOf(handle1)) ;
    assert( 2 == queue->size() ) ;
    queue->remove( handle2 ) ;
    assert( 1 == queue->size() ) ;

  }
  else if( 2 == idx ) {
    assert(false) ;
  }
  else if( 1 == idx ) {
    assert( 1 == queue->size() ) ;
  }
}

void func1( void* arg ) {
  cout << "OK" << endl ;
  cout << "queue should be empty after single task timer expires..." ;
  assert(queue->isEmpty()) ;
  cout << "OK" << endl ;

  cout << "should be able to add tasks in reverse order of execution.." ;
  queue->add( func2, &counter1, 50) ;
  assert( 1 == queue->size() ) ;
  queue->add( func2, &counter2, 25) ;
  assert( 2 == queue->size() ) ;
  
}

void
start_test(su_root_magic_t * p, su_timer_t *t, su_timer_arg_t *a)
{
	cout << "Creating a queue" << endl ;
  root = static_cast<su_root_t*>( a )  ;
  queue = new TimerQueue( root ) ;
  assert( queue ) ;

  cout << "should be able to invoke a single task..."  ;
  queue->add( func1, NULL, 10) ;

}

int main( int argc, char **argv) {

	su_init() ;
  root = su_root_create( NULL ) ;
  timer = su_timer_create( su_root_task(root), 100) ;


  su_timer_set_interval(timer, start_test, root, 25 ) ;

	su_root_run( root ) ;

}

 