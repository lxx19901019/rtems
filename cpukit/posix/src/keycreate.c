/*
 *  $Id$
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>

#include <rtems/system.h>
#include <rtems/score/thread.h>
#include <rtems/score/wkspace.h>
#include <rtems/posix/key.h>

/*PAGE
 *
 *  17.1.1 Thread-Specific Data Key Create, P1003.1c/Draft 10, p. 163
 */

int pthread_key_create(
  pthread_key_t  *key,
  void          (*destructor)( void * )
)
{
  POSIX_Keys_Control  *the_key;
  void                *table;
  unsigned32           the_class;
  unsigned32           bytes_to_allocate;


  _Thread_Disable_dispatch();
 
  the_key = _POSIX_Keys_Allocate();
 
  if ( !the_key ) {
    _Thread_Enable_dispatch();
    return EAGAIN;
  }

  the_key->destructor = destructor;

  for ( the_class = OBJECTS_CLASSES_FIRST_THREAD_CLASS;
        the_class <= OBJECTS_CLASSES_LAST_THREAD_CLASS;
        the_class++ ) {

    bytes_to_allocate =
      (_Objects_Information_table[ the_class ]->maximum + 1) * sizeof( void * );

    table = _Workspace_Allocate( bytes_to_allocate );

    if ( !table ) {
      for ( --the_class;
            the_class >= OBJECTS_CLASSES_FIRST_THREAD_CLASS;
            the_class-- )
        _Workspace_Free( the_key->Values[ the_class ] );

      _POSIX_Keys_Free( the_key );
      _Thread_Enable_dispatch();
      return ENOMEM;
    }

    the_key->Values[ the_class ] = table;
    memset( table, '\0', bytes_to_allocate ); 
  }

  the_key->is_active = TRUE;

  _Objects_Open( &_POSIX_Keys_Information, &the_key->Object, 0 );
 
  *key = the_key->Object.id;

  _Thread_Enable_dispatch();

  return 0;
}
