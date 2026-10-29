#include "debug.h"

#if NS_DEBUG  // whole file compiled out in release

#include <stdarg.h>
#include <esp_heap_caps.h>
#include "config.h"

DbgCounters g_dbg;

// Log lines go into a small static ring; a drain task (priority 1, like loopTask, but it
// only runs when notified) writes them to USB. Nothing that logs can ever block on a
// missing or slow terminal.
static constexpr size_t RING_BYTES = 2048;
static constexpr size_t DBG_LINE_MAX = 160;
static char ring[RING_BYTES];
static volatile size_t rHead = 0, rTail = 0;   // head = write, tail = read
static portMUX_TYPE rMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t drainTask = nullptr;
static TaskHandle_t appTask = nullptr;
static QueueHandle_t evq = nullptr;
static uint32_t lastReport = 0, lastIntegrity = 0;
static uint32_t lastConnectFree = 0;

static size_t ringFree() { return (rTail + RING_BYTES - rHead - 1) % RING_BYTES; }

static void drain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    if (!Serial) {                  // no terminal attached: discard, don't accumulate
      portENTER_CRITICAL(&rMux);
      rTail = rHead;
      portEXIT_CRITICAL(&rMux);
      continue;
    }
    while (rTail != rHead) {
      size_t tail = rTail, head = rHead;
      size_t n = (head >= tail) ? head - tail : RING_BYTES - tail;  // contiguous chunk
      size_t w = Serial.write((const uint8_t*)&ring[tail], n);
      if (w == 0) { vTaskDelay(pdMS_TO_TICKS(20)); break; }
      portENTER_CRITICAL(&rMux);
      rTail = (tail + w) % RING_BYTES;
      portEXIT_CRITICAL(&rMux);
    }
  }
}

void dbg_init() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);         // never block when no host is reading
  appTask = xTaskGetCurrentTaskHandle();
  xTaskCreate(drain, "dbg_drain", 3072, nullptr, 1, &drainTask);
}

void dbg_set_queue(QueueHandle_t q) { evq = q; }

void dbg_log(const char* tag, const char* fmt, ...) {
  char line[DBG_LINE_MAX];
  int n = snprintf(line, sizeof line, "[%8lu][%s] ", (unsigned long)millis(), tag);
  if (n < 0) return;
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  size_t len = strnlen(line, sizeof line - 1);
  line[len++] = '\n';

  portENTER_CRITICAL(&rMux);
  if (ringFree() < len) {
    g_dbg.logDropped++;
  } else {
    for (size_t i = 0; i < len; i++) { ring[rHead] = line[i]; rHead = (rHead + 1) % RING_BYTES; }
  }
  portEXIT_CRITICAL(&rMux);
  if (drainTask) xTaskNotifyGive(drainTask);
}

void dbg_assert_fail(const char* expr, const char* file, int line) {
  dbg_log("ASSERT", "%s (%s:%d)", expr, file, line);
  dbg_report_now("assert");
  delay(100);                       // let the drain task flush, then stop loudly
  abort();
}

static void stackLine(const char* name, TaskHandle_t h) {
  if (h) dbg_log("task", "%-12s stack free min %u B", name, (unsigned)uxTaskGetStackHighWaterMark(h));
}

void dbg_report_now(const char* why) {
  dbg_log("mem", "%s heap free %u  min %u  largest block %u", why,
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  stackLine("app/loop", appTask);
  stackLine("dbg_drain", drainTask);
  stackLine("nimble_host", xTaskGetHandle("nimble_host"));
  dbg_log("rtos", "queue depth max %u  isr drops %u  task drops %u  max latency %u ms  max event %u us",
          (unsigned)g_dbg.queueDepthMax, (unsigned)g_dbg.isrDrops, (unsigned)g_dbg.queueDrops,
          (unsigned)g_dbg.maxQueueLatencyMs, (unsigned)g_dbg.maxEventUs);
  dbg_log("cnt", "press %u debounce-retry %u notify ok/fail %u/%u reconnect %u adv %u pend drop/exp %u/%u logdrop %u",
          (unsigned)g_dbg.presses, (unsigned)g_dbg.debounceRetries, (unsigned)g_dbg.notifyOk,
          (unsigned)g_dbg.notifyFail, (unsigned)g_dbg.reconnects, (unsigned)g_dbg.advRestarts,
          (unsigned)g_dbg.pendingDropped, (unsigned)g_dbg.pendingExpired, (unsigned)g_dbg.logDropped);
}

void dbg_periodic(uint32_t now) {
  if (now - lastReport >= DBG_REPORT_MS) {
    lastReport = now;
    dbg_report_now("periodic");
#if defined(configUSE_TRACE_FACILITY) && configUSE_TRACE_FACILITY && \
    defined(configUSE_STATS_FORMATTING_FUNCTIONS) && configUSE_STATS_FORMATTING_FUNCTIONS
    static char taskBuf[512];       // vTaskList writes ~40 B per task
    vTaskList(taskBuf);
    dbg_log("tasks", "\n%s", taskBuf);
#endif
  }
  if (now - lastIntegrity >= DBG_INTEGRITY_MS) {  // expensive: walks the whole heap
    lastIntegrity = now;
    bool ok = heap_caps_check_integrity_all(true);
    dbg_log("heap", "integrity %s", ok ? "OK" : "CORRUPT");
    if (!ok) DBG_ASSERT(ok);
  }
  (void)evq;
}

#endif  // NS_DEBUG
