/* Decode the bytes the keyboard wrote to a serial port (the LED UART, SD0) with the real receiver of protocol.c, which
 * the LED MCU runs as well: the same state machine that the Shine firmware uses. A test that compares the decoded
 * messages checks the transmitter and the receiver against each other. Needs all_sources.h. */
#pragma once

typedef struct {
    uint8_t cmd, id, size;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} dec_msg_t;

#define DEC_MAX 1024
static dec_msg_t dec_msgs[DEC_MAX];
static size_t    dec_count;

static void dec_callback(const message_t *m) {
    if (dec_count >= DEC_MAX) return;
    dec_msgs[dec_count].cmd  = m->command;
    dec_msgs[dec_count].id   = m->msg_id;
    dec_msgs[dec_count].size = m->payload_size;
    memcpy(dec_msgs[dec_count].payload, m->payload, m->payload_size);
    dec_count++;
}

/* Decode everything written to the port since the last fake_sd_tx_clear() into dec_msgs[] (a message that follows a
 * copy of itself with the same id is dropped by the receiver, as on the keyboard). Returns the number of messages, and
 * leaves the receiver's error count in *errors when it is not NULL. */
static size_t decode_tx(const SerialDriver *sd, unsigned *errors) {
    static protocol_t rx; /* not the global proto, which annepro2.c uses for the receive direction */
    proto_init(&rx, dec_callback);
    dec_count = 0;
    for (size_t i = 0; i < sd->tx_len; i++) proto_consume(&rx, sd->tx[i]);
    if (errors) *errors = rx.errors;
    return dec_count;
}
