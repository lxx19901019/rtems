/*
 *  This file contains the console driver chip level routines for the
 *  Zilog z85c30 chip.
 *
 *  The Zilog Z8530 is also available as:
 *
 *    + Intel 82530
 *    + AMD ???
 *
 *  COPYRIGHT (c) 1998 by Radstone Technology
 *
 *
 * THIS FILE IS PROVIDED TO YOU, THE USER, "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTY OF FITNESS FOR A PARTICULAR PURPOSE. THE ENTIRE RISK
 * AS TO THE QUALITY AND PERFORMANCE OF ALL CODE IN THIS FILE IS WITH YOU.
 *
 * You are hereby granted permission to use, copy, modify, and distribute
 * this file, provided that this notice, plus the above copyright notice
 * and disclaimer, appears in all copies. Radstone Technology will provide
 * no support for this code.
 *
 *  COPYRIGHT (c) 1989-1997.
 *  On-Line Applications Research Corporation (OAR).
 *  Copyright assigned to U.S. Government, 1994.
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id$
 */

#include <rtems.h>
#include <rtems/libio.h>
#include <stdlib.h>

#include <libchip/serial.h>
#include "z85c30_p.h"

/*
 * Flow control is only supported when using interrupts
 */

console_flow z85c30_flow_RTSCTS =
{
  z85c30_negate_RTS,    /* deviceStopRemoteTx */
  z85c30_assert_RTS     /* deviceStartRemoteTx */
};

console_flow z85c30_flow_DTRCTS =
{
  z85c30_negate_DTR,    /* deviceStopRemoteTx */
  z85c30_assert_DTR     /* deviceStartRemoteTx */
};

/*
 * Exported driver function table
 */

console_fns z85c30_fns =
{
  z85c30_probe,                  /* deviceProbe */
  z85c30_open,                   /* deviceFirstOpen */
  z85c30_flush,                  /* deviceLastClose */
  NULL,                          /* deviceRead */
  z85c30_write_support_int,      /* deviceWrite */
  z85c30_initialize_interrupts,  /* deviceInitialize */
  z85c30_write_polled,           /* deviceWritePolled */
  NULL,                          /* deviceSetAttributes */
  FALSE,                         /* deviceOutputUsesInterrupts */
};

console_fns z85c30_fns_polled =
{
  z85c30_probe,                      /* deviceProbe */
  z85c30_open,                       /* deviceFirstOpen */
  z85c30_close,                      /* deviceLastClose */
  z85c30_inbyte_nonblocking_polled,  /* deviceRead */
  z85c30_write_support_polled,       /* deviceWrite */
  z85c30_init,                       /* deviceInitialize */
  z85c30_write_polled,               /* deviceWritePolled */
  NULL,                              /* deviceSetAttributes */
  FALSE,                             /* deviceOutputUsesInterrupts */
};

extern void set_vector( rtems_isr_entry, rtems_vector_number, int );



/* 
 * z85c30_initialize_port
 *
 * initialize a z85c30 Port
 */

static void z85c30_initialize_port(
  int minor
)
{
  unsigned32      ulCtrlPort;
  unsigned32      ulBaudDivisor;
  setRegister_f   setReg;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  setReg   = Console_Port_Tbl[minor].setRegister;

  /*
   * Using register 4
   * Set up the clock rate is 16 times the data
   * rate, 8 bit sync char, 1 stop bit, no parity
   */

  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR4, SCC_WR4_1_STOP | SCC_WR4_16_CLOCK );

  /*
   * Set up for 8 bits/character on receive with
   * receiver disable via register 3
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR3, SCC_WR3_RX_8_BITS );

  /*
   * Set up for 8 bits/character on transmit
   * with transmitter disable via register 5
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR5, SCC_WR5_TX_8_BITS );

  /*
   * Clear misc control bits
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR10, 0x00 );

  /*
   * Setup the source of the receive and xmit
   * clock as BRG output and the transmit clock
   * as the output source for TRxC pin via register 11
   */
  (*setReg)(
    ulCtrlPort,
    SCC_WR0_SEL_WR11,
    SCC_WR11_OUT_BR_GEN | SCC_WR11_TRXC_OI | 
      SCC_WR11_TX_BR_GEN | SCC_WR11_RX_BR_GEN
  );

  ulBaudDivisor = Z85C30_Baud( 
    (unsigned32) Console_Port_Tbl[minor].ulClock,
    (unsigned32) Console_Port_Tbl[minor].pDeviceParams
  );

  /*
   * Setup the lower 8 bits time constants=1E.
   * If the time constans=1E, then the desire
   * baud rate will be equilvalent to 9600, via register 12.
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR12, ulBaudDivisor & 0xff );

  /*
   * using register 13
   * Setup the upper 8 bits time constant
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR13, (ulBaudDivisor>>8) & 0xff );
           
  /*
   * Enable the baud rate generator enable with clock from the
   * SCC's PCLK input via register 14.
   */
  (*setReg)(
    ulCtrlPort,
    SCC_WR0_SEL_WR14,
    SCC_WR14_BR_EN | SCC_WR14_BR_SRC | SCC_WR14_NULL
  );

  /*
   * We are only interested in CTS state changes
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR15, SCC_WR15_CTS_IE );

  /*
   * Reset errors
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_INT );

  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_ERR_RST );

  /*
   * Enable the receiver via register 3
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR3, SCC_WR3_RX_8_BITS | SCC_WR3_RX_EN );

  /*
   * Enable the transmitter pins set via register 5.
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR5, SCC_WR5_TX_8_BITS | SCC_WR5_TX_EN );

  /*
   * Disable interrupts
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR1, 0 );

  /*
   * Reset TX CRC
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_TX_CRC );

  /*
   * Reset interrupts
   */
  (*setReg)( ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_INT );
}

static int z85c30_open(
  int   major,
  int   minor,
  void *arg
)
{
  /*
   * Assert DTR
   */

  if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_DTRCTS) {
    z85c30_assert_DTR(minor);
  }

  return(RTEMS_SUCCESSFUL);
}

static int z85c30_close(
  int   major,
  int   minor,
  void *arg
)
{
  /*
   * Negate DTR
   */

  if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_DTRCTS) {
    z85c30_negate_DTR(minor);
  }

  return(RTEMS_SUCCESSFUL);
}

/* 
 *  z85c30_write_polled
 *
 *  This routine transmits a character using polling.
 */

static void z85c30_write_polled(
  int   minor,
  char  cChar
)
{
  volatile unsigned8 z85c30_status;
  unsigned32         ulCtrlPort;
  getRegister_f      getReg;
  setData_f          setData;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  getReg     = Console_Port_Tbl[minor].getRegister;
  setData    = Console_Port_Tbl[minor].setData;

  /*
   * Wait for the Transmit buffer to indicate that it is empty.
   */

  z85c30_status = (*getReg)( ulCtrlPort, SCC_WR0_SEL_RD0 );

  while (!Z85C30_Status_Is_TX_buffer_empty(z85c30_status)) {
    /*
     * Yield while we wait
     */
    if (_System_state_Is_up(_System_state_Get())) {
      rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
    }
    z85c30_status = (*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);
  }

  /*
   * Write the character.
   */

  (*setData)(Console_Port_Tbl[minor].ulDataPort, cChar);
}

/*
 *  Console Device Driver Entry Points
 */

static boolean z85c30_probe(int minor)
{
  /*
   * If the configuration dependent probe has located the device then
   * assume it is there
   */

  return(TRUE);
}

static void z85c30_init(int minor)
{
  unsigned32       ulCtrlPort;
  unsigned8        dummy;
  z85c30_context  *pz85c30Context;
  setRegister_f    setReg;
  getRegister_f    getReg;

  setReg = Console_Port_Tbl[minor].setRegister;
  getReg   = Console_Port_Tbl[minor].getRegister;

  pz85c30Context = (z85c30_context *)malloc(sizeof(z85c30_context));

  Console_Port_Data[minor].pDeviceContext=(void *)pz85c30Context;

  pz85c30Context->ucModemCtrl = SCC_WR5_TX_8_BITS | SCC_WR5_TX_EN;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  if (ulCtrlPort == Console_Port_Tbl[minor].ulCtrlPort2) {
    /*
     * This is channel A
     */
    /*
     * Ensure port state machine is reset
     */
    dummy = (*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);

    (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR9, SCC_WR9_CH_A_RST);

  } else {
    /*
     * This is channel B
     */
    /*
     * Ensure port state machine is reset
     */
    dummy = (*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);

    (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR9, SCC_WR9_CH_B_RST);
  }

  z85c30_initialize_port(minor);
}

/*
 * These routines provide control of the RTS and DTR lines
 */

/*
 *  z85c30_assert_RTS
 */

static int z85c30_assert_RTS(int minor)
{
  rtems_interrupt_level  Irql;
  z85c30_context        *pz85c30Context;
  setRegister_f          setReg;

  setReg = Console_Port_Tbl[minor].setRegister;

  pz85c30Context = (z85c30_context *) Console_Port_Data[minor].pDeviceContext;
  
  /*
   * Assert RTS
   */

  rtems_interrupt_disable(Irql);
    pz85c30Context->ucModemCtrl|=SCC_WR5_RTS;
    (*setReg)(
      Console_Port_Tbl[minor].ulCtrlPort1,
      SCC_WR0_SEL_WR5,
      pz85c30Context->ucModemCtrl
    );
  rtems_interrupt_enable(Irql);
  return 0;
}

/*
 *  z85c30_negate_RTS
 */

static int z85c30_negate_RTS(int minor)
{
  rtems_interrupt_level  Irql;
  z85c30_context        *pz85c30Context;
  setRegister_f          setReg;

  setReg = Console_Port_Tbl[minor].setRegister;

  pz85c30Context = (z85c30_context *) Console_Port_Data[minor].pDeviceContext;
  
  /*
   * Negate RTS
   */

  rtems_interrupt_disable(Irql);
    pz85c30Context->ucModemCtrl&=~SCC_WR5_RTS;
    (*setReg)(
      Console_Port_Tbl[minor].ulCtrlPort1,
      SCC_WR0_SEL_WR5,
      pz85c30Context->ucModemCtrl
    );
  rtems_interrupt_enable(Irql);
  return 0;
}

/*
 * These flow control routines utilise a connection from the local DTR
 * line to the remote CTS line
 */

/*
 *  z85c30_assert_DTR
 */

static int z85c30_assert_DTR(int minor)
{
  rtems_interrupt_level  Irql;
  z85c30_context        *pz85c30Context;
  setRegister_f          setReg;

  setReg = Console_Port_Tbl[minor].setRegister;

  pz85c30Context = (z85c30_context *) Console_Port_Data[minor].pDeviceContext;
  
  /*
   * Assert DTR
   */

  rtems_interrupt_disable(Irql);
    pz85c30Context->ucModemCtrl|=SCC_WR5_DTR;
    (*setReg)(
      Console_Port_Tbl[minor].ulCtrlPort1,
      SCC_WR0_SEL_WR5,
      pz85c30Context->ucModemCtrl
  );
  rtems_interrupt_enable(Irql);
  return 0;
}

/*
 *  z85c30_negate_DTR
 */

static int z85c30_negate_DTR(int minor)
{
  rtems_interrupt_level  Irql;
  z85c30_context        *pz85c30Context;
  setRegister_f          setReg;

  setReg = Console_Port_Tbl[minor].setRegister;

  pz85c30Context = (z85c30_context *) Console_Port_Data[minor].pDeviceContext;
  
  /*
   * Negate DTR
   */

  rtems_interrupt_disable(Irql);
    pz85c30Context->ucModemCtrl&=~SCC_WR5_DTR;
    (*setReg)(
      Console_Port_Tbl[minor].ulCtrlPort1,
      SCC_WR0_SEL_WR5,
      pz85c30Context->ucModemCtrl
  );
  rtems_interrupt_enable(Irql);
  return 0;
}

/*
 *  z85c30_isr
 *
 *  This routine is the console interrupt handler for COM3 and COM4
 *
 *  Input parameters:
 *    vector - vector number
 *
 *  Output parameters: NONE
 *
 *  Return values:     NONE
 */

static void z85c30_process(
  int        minor,
  unsigned8  ucIntPend
)
{
  unsigned32          ulCtrlPort;
  unsigned32          ulDataPort;
  volatile unsigned8  z85c30_status;
  char                cChar;
  setRegister_f       setReg;
  getRegister_f       getReg;
  getData_f           getData;
  setData_f           setData;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  ulDataPort = Console_Port_Tbl[minor].ulDataPort;
  setReg     = Console_Port_Tbl[minor].setRegister;
  getReg     = Console_Port_Tbl[minor].getRegister;
  getData    = Console_Port_Tbl[minor].getData;
  setData    = Console_Port_Tbl[minor].setData;

  /*
   * Deal with any received characters
   */
  while (ucIntPend&SCC_RR3_B_RX_IP)
  {
    z85c30_status=(*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);
    if (!Z85C30_Status_Is_RX_character_available(z85c30_status)) {
      break;
    }

    /*
     * Return the character read.
     */

    cChar = (*getData)(ulDataPort);

    rtems_termios_enqueue_raw_characters(
      Console_Port_Data[minor].termios_data,
      &cChar,
      1
    );
  }

  while (TRUE)
  {
    z85c30_status = (*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);
    if (!Z85C30_Status_Is_TX_buffer_empty(z85c30_status)) {
      /*
       * We'll get another interrupt when
       * the transmitter holding reg. becomes
       * free again and we are clear to send
       */
      break;
    }
  
    if (!Z85C30_Status_Is_CTS_asserted(z85c30_status)) {
      /*
       * We can't transmit yet
       */
      (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_TX_INT);
      /*
       * The next state change of CTS will wake us up
       */
      break;
    }
  
    if (Ring_buffer_Is_empty(&Console_Port_Data[minor].TxBuffer)) {
      Console_Port_Data[minor].bActive=FALSE;
      if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_RTSCTS) {
        z85c30_negate_RTS(minor);
      }
      /*
       * There is no data to transmit
       */
      (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_TX_INT);
      break;
    }

    Ring_buffer_Remove_character( &Console_Port_Data[minor].TxBuffer, cChar);

    /*
     * transmit character
     */
    (*setData)(ulDataPort, cChar);

    /*
     * Interrupt once FIFO has room
      */
    (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_TX_INT);
    break;
  }

  if (ucIntPend&SCC_RR3_B_EXT_IP) {
    /*
     * Clear the external status interrupt
     */
    (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_INT);
    z85c30_status=(*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);
  }

  /*
   * Reset interrupts
   */
  (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_HI_IUS);
}

static rtems_isr z85c30_isr(
  rtems_vector_number vector
)
{
  int                 minor;
  unsigned32          ulCtrlPort;
  volatile unsigned8  ucIntPend;
  volatile unsigned8  ucIntPendPort;
  getRegister_f    getReg;

  for (minor=0;minor<Console_Port_Count;minor++) {
    if (vector==Console_Port_Tbl[minor].ulIntVector) {
      ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort2;
      getReg     = Console_Port_Tbl[minor].getRegister;
      do {
        ucIntPend=(*getReg)(ulCtrlPort, SCC_WR0_SEL_RD3);

          /*
           * If this is channel A select channel A status
           */

          if (ulCtrlPort == Console_Port_Tbl[minor].ulCtrlPort1) {
            ucIntPendPort = ucIntPend>>3;
            ucIntPendPort = ucIntPendPort&=7;
          } else {
            ucIntPendPort = ucIntPend &= 7;
          }

          if (ucIntPendPort) {
            z85c30_process(minor, ucIntPendPort);
          }
      } while (ucIntPendPort);
    }
  }
}

/*
 *  z85c30_flush
 */

static int z85c30_flush(
  int major,
  int minor,
  void *arg
)
{
  while (!Ring_buffer_Is_empty(&Console_Port_Data[minor].TxBuffer)) {
    /*
     * Yield while we wait
     */
    if (_System_state_Is_up(_System_state_Get())) {
      rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
    }
  }

  z85c30_close(major, minor, arg);

  return(RTEMS_SUCCESSFUL);
}

/*
 *  z85c30_initialize_interrupts
 *
 *  This routine initializes the console's receive and transmit
 *  ring buffers and loads the appropriate vectors to handle the interrupts.
 *
 *  Input parameters:  NONE
 *
 *  Output parameters: NONE
 *
 *  Return values:     NONE
 */

static void z85c30_enable_interrupts(
  int minor
)
{
  unsigned32     ulCtrlPort;
  setRegister_f  setReg;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  setReg     = Console_Port_Tbl[minor].setRegister;

  /*
   * Enable interrupts
   */
  (*setReg)(
    ulCtrlPort,
    SCC_WR0_SEL_WR1,
    SCC_WR1_EXT_INT_EN | SCC_WR1_TX_INT_EN | SCC_WR1_INT_ALL_RX
  );
  (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR2, 0);
  (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR9, SCC_WR9_MIE);

  /*
   * Reset interrupts
   */
  (*setReg)(ulCtrlPort, SCC_WR0_SEL_WR0, SCC_WR0_RST_INT);
}

static void z85c30_initialize_interrupts(
  int minor
)
{
  z85c30_init(minor);

  Ring_buffer_Initialize(&Console_Port_Data[minor].TxBuffer);

  Console_Port_Data[minor].bActive=FALSE;
  if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_RTSCTS) {
    z85c30_negate_RTS(minor);
  }

  if (Console_Port_Tbl[minor].ulCtrlPort1== Console_Port_Tbl[minor].ulCtrlPort2) {
    /*
     * Only do this for Channel A
     */

    set_vector(z85c30_isr, Console_Port_Tbl[minor].ulIntVector, 1);
  }

  z85c30_enable_interrupts(minor);
}

/* 
 *  z85c30_write_support_int
 *
 *  Console Termios output entry point.
 *
 */

static int z85c30_write_support_int(
  int   minor, 
  const char *buf, 
  int   len)
{
  int i;
  unsigned32 Irql;

  for (i=0; i<len;) {
    if (Ring_buffer_Is_full(&Console_Port_Data[minor].TxBuffer)) {
      if (!Console_Port_Data[minor].bActive) {
        /*
         * Wake up the device
         */
        if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_RTSCTS) {
          z85c30_assert_RTS(minor);
        }
        rtems_interrupt_disable(Irql);
        Console_Port_Data[minor].bActive=TRUE;
        z85c30_process(minor, SCC_RR3_B_TX_IP);
        rtems_interrupt_enable(Irql);
      } else {
        /*
         * Yield while we await an interrupt
         */
        rtems_task_wake_after(RTEMS_YIELD_PROCESSOR);
      }

      /*
       * Wait for ring buffer to empty
       */
      continue;
    } else {
      Ring_buffer_Add_character( &Console_Port_Data[minor].TxBuffer, buf[i]);
      i++;
    }
  }

  /*
   * Ensure that characters are on the way
   */
  if (!Console_Port_Data[minor].bActive) {
    /*
     * Wake up the device
     */
    if (Console_Port_Tbl[minor].pDeviceFlow !=&z85c30_flow_RTSCTS) {
            z85c30_assert_RTS(minor);
    }
    rtems_interrupt_disable(Irql);
    Console_Port_Data[minor].bActive=TRUE;
    z85c30_process(minor, SCC_RR3_B_TX_IP);
    rtems_interrupt_enable(Irql);
  }

  return (len);
}

/* 
 *  z85c30_inbyte_nonblocking_polled
 *
 *  This routine polls for a character.
 */

static int z85c30_inbyte_nonblocking_polled(
  int  minor
)
{
  volatile unsigned8  z85c30_status;
  unsigned32          ulCtrlPort;
  getRegister_f       getReg;
  getData_f           getData;

  ulCtrlPort = Console_Port_Tbl[minor].ulCtrlPort1;
  getData    = Console_Port_Tbl[minor].getData;
  getReg     = Console_Port_Tbl[minor].getRegister;

  /*
   * return -1 if a character is not available.
   */
  z85c30_status=(*getReg)(ulCtrlPort, SCC_WR0_SEL_RD0);
  if (!Z85C30_Status_Is_RX_character_available(z85c30_status)) {
    return -1;
  }

  /*
   * Return the character read.
   */
  return (*getData)(Console_Port_Tbl[minor].ulDataPort);
}

/* 
 *  z85c30_write_support_polled
 *
 *  Console Termios output entry point.
 *
 */

static int z85c30_write_support_polled(
  int   minor,
  const char *buf,
  int   len)
{
  int nwrite=0;

  /*
   * poll each byte in the string out of the port.
   */
  while (nwrite < len) {
    z85c30_write_polled(minor, *buf++);
    nwrite++;
  }

  /*
   * return the number of bytes written.
   */
  return nwrite;
}
