#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "sofia-sip/su.h"
#include "sofia-sip/su_wait.h"
#include "sofia-sip/su_log.h"

void
print_stamp(su_root_magic_t * p, su_timer_t *t, su_timer_arg_t *a)
{
	printf("timer goes\n") ;
}

int main( int argc, char **argv) {

	su_init() ;
	su_root_t* m_root = su_root_create( NULL ) ;
	su_timer_t* m_timer = su_timer_create( su_root_task(m_root), 100) ;
	su_timer_set_for_ever(m_timer, print_stamp, NULL) ;
	su_root_run( m_root ) ;

}

 