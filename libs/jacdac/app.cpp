#include "pxt.h"
#include "jdlow.h"
#include "mbbridge.h"

#define FRAME_EXT_FLAG 0x80

#define LOG(msg, ...) DMESG("JDAPP: " msg, ##__VA_ARGS__)
//#define LOG(...) ((void)0)

#define DEVICE_ID DEVICE_ID_JACDAC

#define EVT_DATA_READY 1
#define EVT_QUEUE_ANNOUNCE 100
#define EVT_TX_EMPTY 101

namespace jacdac {

static inline void raiseEvent(int src, int val) {
#ifdef PXT_ESP32
    pxt::raiseEvent(src, val);
#else
    Event(src, val);
#endif
}

// Calliope edition: larger RX queue so packets survive slow Arcade frames (upstream: 10)
#define MAX_RX 24
#define MAX_TX 10

static LinkedFrame *volatile rxQ;
static LinkedFrame *volatile txQ;
static LinkedFrame *superFrameRX;
static bool data_ready_rised;

extern "C" REAL_TIME_FUNC jd_frame_t *app_pull_frame() {
    target_disable_irq();
    jd_frame_t *res = NULL;
    if (txQ) {
        res = &txQ->frame;
        txQ = txQ->next;
    }
    target_enable_irq();
    return res;
}

static void queue_cnt() {
    if (txQ == NULL)
        raiseEvent(DEVICE_ID, EVT_TX_EMPTY);
}

extern "C" void app_queue_annouce() {
    //    LOG("announce");
    queue_cnt();
    raiseEvent(DEVICE_ID, EVT_QUEUE_ANNOUNCE);
}

int copyAndAppend(LinkedFrame *volatile *q, jd_frame_t *frame, int max, uint8_t *data) {
    auto buf = (LinkedFrame *)malloc(JD_FRAME_SIZE(frame) + LINKED_FRAME_HEADER_SIZE);

    buf->timestamp_ms = current_time_ms();

    if (data) {
        memcpy(&buf->frame, frame, JD_SERIAL_FULL_HEADER_SIZE);
        memcpy(&buf->frame.data[4], data, JD_FRAME_SIZE(frame) - JD_SERIAL_FULL_HEADER_SIZE);
        jd_compute_crc(&buf->frame);
        frame->crc = buf->frame.crc;
    } else {
        memcpy(&buf->frame, frame, JD_FRAME_SIZE(frame));
    }

    target_disable_irq();
    auto last = *q;
    int num = 0;
    buf->next = NULL;
    while (last && last->next) {
        last = last->next;
        num++;
    }
    if (num < max) {
        if (last)
            last->next = buf;
        else
            *q = buf;
        buf = NULL;
    }
    target_enable_irq();

    if (buf == NULL) {
        return 0;
    } else {
        free(buf);
        return -1;
    }
}

extern "C" int app_handle_frame(jd_frame_t *frame) {
    // DMESG("PKT t:%d fl:%x %d cmd=%x", (int)current_time_ms(), frame->flags,
    //      ((jd_packet_t *)frame)->service_number, ((jd_packet_t *)frame)->service_command);

    // Calliope edition: if the single DATA_READY event got lost (e.g. full CODAL event
    // queue), data_ready_rised stayed set forever and nothing was read again. Re-raise
    // when the queue is full or its oldest frame has waited for more than 100ms.
    if (copyAndAppend(&rxQ, frame, MAX_RX) < 0) {
        raiseEvent(DEVICE_ID, EVT_DATA_READY);
        return -1;
    } else {
        if (!data_ready_rised ||
            (rxQ && (uint32_t)current_time_ms() - rxQ->timestamp_ms > 100)) {
            data_ready_rised = true;
            raiseEvent(DEVICE_ID, EVT_DATA_READY);
        }
        return 0;
    }
}

extern "C" void app_frame_sent(jd_frame_t *frame) {
    // LOG("frame sent");
    free((uint8_t *)frame - LINKED_FRAME_HEADER_SIZE);
    if (txQ)
        jd_packet_ready();
    queue_cnt();
}

//%
int __physId() {
    return DEVICE_ID;
}

static bool isFloodPingReport(jd_packet_t *pkt) {
    return !(pkt->flags & JD_FRAME_FLAG_COMMAND) && pkt->service_number == 0 &&
           pkt->service_command == 0x83;
}

//%
void __physSendPacket(Buffer header, Buffer data) {
    if (!header || header->length != JD_SERIAL_FULL_HEADER_SIZE)
        jd_panic();

    jd_frame_t *frame = (jd_frame_t *)header->data;
    frame->size = (data->length + 4 + 3) & ~3;

    // Calliope edition: when the send queue is full, drop the OLDEST waiting frame
    // instead of the new one. Otherwise, after the bus was blocked for a while, stale
    // frames are sent first and every new packet lags behind by a full queue (~1s).
    // The frame currently on the wire is not in txQ (app_pull_frame removed it).
    target_disable_irq();
    int queued = 0;
    for (auto f = txQ; f; f = f->next)
        queued++;
    LinkedFrame *dropped = NULL;
    if (queued >= MAX_TX && txQ) {
        dropped = txQ;
        txQ = txQ->next;
    }
    target_enable_irq();
    if (dropped)
        free(dropped);

    if (copyAndAppend(&txQ, frame, MAX_TX, data->data) < 0) {
        // Calliope edition: queue full - make sure the sender is running, otherwise a
        // missed wake-up would leave the queue full (and the device mute) for good
        jd_packet_ready();
        return;
    }

    if (pxt::logJDFrame && !isFloodPingReport((jd_packet_t *)header->data)) {
        auto buf = (uint8_t *)malloc(JD_FRAME_SIZE(frame));
        memcpy(buf, frame, JD_SERIAL_FULL_HEADER_SIZE);
        memcpy(buf + JD_SERIAL_FULL_HEADER_SIZE, data->data,
               JD_FRAME_SIZE(frame) - JD_SERIAL_FULL_HEADER_SIZE);
        pxt::logJDFrame(buf);
        free(buf);
    }

    jd_packet_ready();
    // DMESG("s3:%d", (int)current_time_ms());
}

//%
int __physGetTimestamp() {
    if (!superFrameRX)
        return 0;
    return superFrameRX->timestamp_ms;
}

//%
Buffer __physGetPacket() {
    if (superFrameRX && jd_shift_frame(&superFrameRX->frame) == 0) {
        free(superFrameRX);
        superFrameRX = NULL;
    }

    if (!superFrameRX) {
        target_disable_irq();
        if ((superFrameRX = rxQ) != NULL)
            rxQ = rxQ->next;
        if (!superFrameRX)
            data_ready_rised = false;
        target_enable_irq();

        if (!superFrameRX)
            return NULL;

        if (pxt::logJDFrame && !(superFrameRX->frame.flags & FRAME_EXT_FLAG) &&
            !isFloodPingReport((jd_packet_t *)&superFrameRX->frame))
            pxt::logJDFrame((uint8_t *)&superFrameRX->frame);
    }

    auto pkt = (jd_packet_t *)&superFrameRX->frame;
    return mkBuffer(pkt, JD_SERIAL_FULL_HEADER_SIZE + pkt->service_size);
}

//%
bool __physIsRunning() {
    return jd_is_running() != 0;
}

// Calliope edition: GPIO "DETECT" watchdog. On the nRF52 all pins with a SENSE
// configuration share one DETECT signal; a PORT interrupt is only raised when it rises.
// CODAL's GPIOTE handler clears all LATCH bits after processing a snapshot, so in the
// default DETECT mode an edge on another pin (seen here: the GameKit display flow pin
// P0_10) arriving during the handler is dropped while its sense condition stays met.
// DETECT then never falls again and no pin gets any edge interrupt - Jacdac goes deaf
// (and the display waits for its flow edge) until the display controller times out
// after ~5s. uart_init_() in hw.cpp switches to latched detection (LDETECT), which
// prevents this; this watchdog is the safety net.
// CODAL flips a pin's SENSE after every handled edge, so a pin meeting its sense
// condition at two checks in a row is always a missed edge: we raise the PORT event
// again and let CODAL's handler process it; if the pin is still stuck afterwards, we
// flip its sense ourselves. Called every 200ms.
static int detectPin = -1, detectKickedPin = -1;

//%
void __physGpioWatchdog() {
#if defined(NRF52_SERIES)
    int found = -1;
    for (int p = 0; p < 48 && found < 0; ++p) {
        NRF_GPIO_Type *port = p >= 32 ? NRF_P1 : NRF_P0;
        int bit = p & 31;
        uint32_t sense = (port->PIN_CNF[bit] & GPIO_PIN_CNF_SENSE_Msk) >> GPIO_PIN_CNF_SENSE_Pos;
        if (sense == GPIO_PIN_CNF_SENSE_Disabled)
            continue;
        int level = (port->IN >> bit) & 1;
        if ((sense == GPIO_PIN_CNF_SENSE_High && level) ||
            (sense == GPIO_PIN_CNF_SENSE_Low && !level))
            found = p;
    }
    if (found < 0) {
        detectPin = -1;
        detectKickedPin = -1;
    } else if (found != detectPin) {
        detectPin = found;
    } else {
        // same pin stuck since the previous check: missed edge
        if (found != detectKickedPin) {
            // first try: re-raise the PORT event so CODAL handles the edge normally
            detectKickedPin = found;
            NRF_GPIOTE->EVENTS_PORT = 1;
            NVIC_SetPendingIRQ(GPIOTE_IRQn);
        } else {
            // still stuck after that: flip the sense ourselves to release DETECT (the
            // pin's owner misses this one edge, but all other pins work again)
            NRF_GPIO_Type *port = found >= 32 ? NRF_P1 : NRF_P0;
            port->PIN_CNF[found & 31] ^= 0x00010000;
            detectKickedPin = -1;
        }
        detectPin = -1;
    }
#endif
}

// Calliope edition: periodic bus watchdog, see jd_watchdog() in jdlow.c
//%
void __physWatchdog() {
    if (!jd_is_running())
        return;
    // frames waiting to be sent: make sure the sender knows (missed wake-up)
    if (txQ)
        jd_packet_ready();
    jd_watchdog();
}

static void sendExtFrame(const uint8_t *data) {
    jd_frame_t *frame = (jd_frame_t *)data;
    frame->flags |= FRAME_EXT_FLAG; // set flag saying the frame came from USB
    app_handle_frame(frame);        // pretend we got it from the wire
    frame->flags &= ~FRAME_EXT_FLAG;
    copyAndAppend(&txQ, frame, MAX_TX); // and also put it on the send Q
    jd_packet_ready();
}

#ifdef PIN
extern "C" int jd_pin_num() {
    return PIN(JACK_TX);
}
#endif

//%
void __physStart() {
    jd_init();
    sendJDFrame = sendExtFrame;
#if defined(MICROBIT_CODAL) && MICROBIT_CODAL
#if CONFIG_ENABLED(DEVICE_BLE) && CONFIG_ENABLED(JACDAC_BLE_TRANSPORT)
    jdble_init();
#else
    mbbridge_init();
#endif
#endif
}

//%
Buffer __physGetDiagnostics() {
    if (!jd_is_running())
        return NULL;
    return mkBuffer(jd_get_diagnostics(), sizeof(jd_diagnostics_t));
}

} // namespace jacdac