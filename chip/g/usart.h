/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "consumer.h"
#include "producer.h"
#include "registers.h"
#include "task.h"
#include "board.h"

#ifndef __CROS_FORWARD_UART_H
#define __CROS_FORWARD_UART_H

#define CONFIGURE_INTERRUPTS__rx_int(NAME) send_data_to_usb(&NAME)

struct usart_config {
	int uart;

	struct producer const producer;
	struct consumer const consumer;
};

extern struct consumer_ops const uart_consumer_ops;
extern struct producer_ops const uart_producer_ops;
#define CONFIGURE_INTERRUPTS(NAME,					\
			     RXINT,					\
			     TXINT)					\
	void CONCAT2(NAME, _rx_int_)(void);				\
	void CONCAT2(NAME, _tx_int_)(void);				\
	DECLARE_IRQ(RXINT, CONCAT2(NAME, _rx_int_), 1);			\
	DECLARE_IRQ(TXINT, CONCAT2(NAME, _tx_int_), 1);			\
	void CONCAT2(NAME, _tx_int_)(void)				\
	{								\
		/* Clear transmit interrupt status */			\
		GR_UART_ISTATECLR(NAME.uart) =				\
			GC_UART_ISTATECLR_TX_MASK;			\
		/* Fill output FIFO */					\
		get_data_from_usb(&NAME);				\
	}								\
	void CONCAT2(NAME, _rx_int_)(void)				\
	{								\
		/* Clear receive interrupt status */			\
		GR_UART_ISTATECLR(NAME.uart) =				\
			GC_UART_ISTATECLR_RX_MASK;			\
		/* Read input FIFO until empty */			\
		CONFIGURE_INTERRUPTS__rx_int(NAME);			\
	}


#define USART_CONFIG(NAME,						\
		     UART,						\
		     RX_QUEUE,						\
		     TX_QUEUE)						\
	struct usart_config const NAME = {				\
		.uart      = UART,					\
		.consumer  = {						\
			.queue = &TX_QUEUE,				\
			.ops   = &uart_consumer_ops,			\
		},							\
		.producer  = {						\
			.queue = &RX_QUEUE,				\
			.ops   = &uart_producer_ops,			\
		},							\
	}


/* Read data from UART and add it to the producer queue */
void send_data_to_usb(struct usart_config const *config);

/* Read data from the consumer queue and send it to the UART */
void get_data_from_usb(struct usart_config const *config);

/* Helper for UART bitbang mode. */
extern struct usart_config const ec_uart;

/* Change EC UART-USB Bridge status */
void uart_ec_bridge_enable(int enabled, int write);

/* Return non-zero if EC UART-USB Bridge is enabled, or 0 otherwise */
int uart_ec_bridge_is_enabled(void);

/* Return non-zero if EC UART-USB Bridge is readwritable, or 0 otherwise */
int uart_ec_bridge_tx_is_enabled(void);

#endif  /* __CROS_FORWARD_UART_H */
