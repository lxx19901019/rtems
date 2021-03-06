/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (C) 2018, 2019 embedded brains GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/record.h>
#include <rtems.h>

#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#ifdef RTEMS_SMP
#define CHUNKS (3 * CPU_MAXIMUM_PROCESSORS)
#else
#define CHUNKS 4
#endif

typedef struct {
  int           available;
  struct iovec *current;
  struct iovec  iov[CHUNKS];
} writev_visitor_context;

static void writev_visitor(
  const rtems_record_item *items,
  size_t                   count,
  void                    *arg
)
{
  writev_visitor_context *ctx;

  ctx = arg;

  if ( ctx->available > 0 ) {
    ctx->current->iov_base = RTEMS_DECONST( rtems_record_item *, items );
    ctx->current->iov_len = count * sizeof( *items );
    --ctx->available;
    ++ctx->current;
  }
}

ssize_t rtems_record_writev( int fd, bool *written )
{
  writev_visitor_context ctx;
  int n;

  ctx.available = CHUNKS;
  ctx.current = &ctx.iov[ 0 ];
  rtems_record_drain( writev_visitor, &ctx );
  n = CHUNKS - ctx.available;

  if ( n > 0 ) {
    *written = true;
    return writev( fd, &ctx.iov[ 0 ], n );
  } else {
    *written = false;
    return 0;
  }
}

#define WAKEUP_EVENT RTEMS_EVENT_0

static void wakeup( rtems_id task )
{
  (void) rtems_event_send( task, WAKEUP_EVENT );
}

static void wait( rtems_option options )
{
  rtems_event_set events;

  (void) rtems_event_receive(
    WAKEUP_EVENT,
    RTEMS_EVENT_ANY | options,
    RTEMS_NO_TIMEOUT,
    &events
  );
}

static void wakeup_timer( rtems_id timer, void *arg )
{
  rtems_id *server;

  server = arg;
  wakeup( *server );
  (void) rtems_timer_reset( timer );
}

void _Record_Stream_header_initialize( Record_Stream_header *header )
{
#if BYTE_ORDER == LITTLE_ENDIAN
#if __INTPTR_WIDTH__ == 32
  header->format = RTEMS_RECORD_FORMAT_LE_32,
#elif __INTPTR_WIDTH__ == 64
  header->format = RTEMS_RECORD_FORMAT_LE_64,
#else
#error "unexpected __INTPTR_WIDTH__"
#endif
#elif BYTE_ORDER == BIG_ENDIAN
#if __INTPTR_WIDTH__ == 32
  header->format = RTEMS_RECORD_FORMAT_BE_32,
#elif __INTPTR_WIDTH__ == 64
  header->format = RTEMS_RECORD_FORMAT_BE_64,
#else
#error "unexpected __INTPTR_WIDTH__"
#endif
#else
#error "unexpected BYTE_ORDER"
#endif

  header->magic = RTEMS_RECORD_MAGIC;

  header->Version.event = RTEMS_RECORD_TIME_EVENT( 0, RTEMS_RECORD_VERSION );
  header->Version.data = RTEMS_RECORD_THE_VERSION;

  header->Processor_maximum.event =
    RTEMS_RECORD_TIME_EVENT( 0, RTEMS_RECORD_PROCESSOR_MAXIMUM );
  header->Processor_maximum.data = rtems_get_processor_count() - 1;

  header->Count.event = RTEMS_RECORD_TIME_EVENT( 0, RTEMS_RECORD_COUNT );
  header->Count.data = _Record_Item_count;

  header->Frequency.event =
    RTEMS_RECORD_TIME_EVENT( 0, RTEMS_RECORD_FREQUENCY );
  header->Frequency.data = rtems_counter_frequency();
}

static void send_header( int fd )
{
  Record_Stream_header header;

  _Record_Stream_header_initialize( &header );
  (void) write( fd, &header, sizeof( header ) );
}

void rtems_record_server( uint16_t port, rtems_interval period )
{
  rtems_status_code sc;
  rtems_id self;
  rtems_id timer;
  struct sockaddr_in addr;
  int sd;
  int rv;

  sd = -1;
  self = rtems_task_self();

  sc = rtems_timer_create( rtems_build_name( 'R', 'C', 'R', 'D' ), &timer );
  if ( sc != RTEMS_SUCCESSFUL ) {
    return;
  }

  sd = socket( PF_INET, SOCK_STREAM, 0 );
  if (sd < 0) {
    goto error;
  }

  memset( &addr, 0, sizeof( addr ) );
  addr.sin_family = AF_INET;
  addr.sin_port = htons( port );
  addr.sin_addr.s_addr = htonl( INADDR_ANY );

  rv = bind( sd, (const struct sockaddr *) &addr, sizeof( addr ) );
  if (rv != 0) {
    goto error;
  }

  rv = listen( sd, 0 );
  if (rv != 0) {
    goto error;
  }

  while ( true ) {
    int cd;
    bool written;
    ssize_t n;

    cd = accept( sd, NULL, NULL );

    if ( cd < 0 ) {
      break;
    }

    wait( RTEMS_NO_WAIT );
    (void) rtems_timer_fire_after( timer, period, wakeup_timer, &self );
    send_header( cd );

    while ( true ) {
      n = rtems_record_writev( cd, &written );

      if ( written && n <= 0 ) {
        break;
      }

      wait( RTEMS_WAIT );
    }

    (void) rtems_timer_cancel( timer );
    (void) close( cd );
  }

error:

  (void) close( sd );
  (void) rtems_timer_delete( timer );
}

typedef struct {
  rtems_id       task;
  uint16_t       port;
  rtems_interval period;
} server_arg;

static void server( rtems_task_argument arg )
{
  server_arg     *sarg;
  uint16_t        port;
  rtems_interval  period;

  sarg = (server_arg *) arg;
  port = sarg->port;
  period = sarg->period;
  wakeup(sarg->task);
  rtems_record_server( port, period );
  rtems_task_exit();
}

rtems_status_code rtems_record_start_server(
  rtems_task_priority priority,
  uint16_t            port,
  rtems_interval      period
)
{
  rtems_status_code sc;
  rtems_id          id;
  server_arg        sarg;

  sarg.port = port;
  sarg.period = period;
  sarg.task = rtems_task_self();

  sc = rtems_task_create(
    rtems_build_name( 'R', 'C', 'R', 'D' ),
    priority,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_DEFAULT_ATTRIBUTES,
    &id
  );
  if ( sc != RTEMS_SUCCESSFUL ) {
    return sc;
  }

  (void) rtems_task_start( id, server, (rtems_task_argument) &sarg );
  wait( RTEMS_WAIT );

  return RTEMS_SUCCESSFUL;
}
