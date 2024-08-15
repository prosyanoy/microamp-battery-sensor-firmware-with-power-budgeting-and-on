#include <pb_decode.h>
#include <pb_encode.h>

#include "microamp.pb.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_event_round_trip(void)
{
    LogRecord in = LogRecord_init_zero, out = LogRecord_init_zero;
    in.seq = 7;
    in.which_payload = LogRecord_event_tag;
    in.payload.event.timestamp_ms = 123456;
    in.payload.event.event_class = EventClass_EVENT_CLASS_IMPACT;
    in.payload.event.peak_mg = 9876;
    in.payload.event.confidence = 77;
    in.payload.event.duration_ms = 320;
    uint8_t buf[LogRecord_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof buf);
    TEST_ASSERT_TRUE(pb_encode(&os, LogRecord_fields, &in));
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&is, LogRecord_fields, &out));
    TEST_ASSERT_EQUAL_UINT32(7, out.seq);
    TEST_ASSERT_EQUAL(LogRecord_event_tag, out.which_payload);
    TEST_ASSERT_EQUAL_UINT32(9876, out.payload.event.peak_mg);
    TEST_ASSERT_EQUAL(EventClass_EVENT_CLASS_IMPACT, out.payload.event.event_class);
}

static void test_status_round_trip_and_max_size(void)
{
    LogRecord in = LogRecord_init_zero, out = LogRecord_init_zero;
    in.which_payload = LogRecord_status_tag;
    in.payload.status.battery_years_x100 = 1234;
    in.payload.status.sensor_ok = true;
    in.payload.status.avg_current_na = 0xFFFFFFFFu;
    uint8_t buf[LogRecord_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof buf);
    TEST_ASSERT_TRUE(pb_encode(&os, LogRecord_fields, &in));
    TEST_ASSERT_TRUE(os.bytes_written <= LogRecord_size);
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&is, LogRecord_fields, &out));
    TEST_ASSERT_EQUAL_UINT32(1234, out.payload.status.battery_years_x100);
    TEST_ASSERT_TRUE(out.payload.status.sensor_ok);
}

static void test_undersized_buffer_fails_cleanly(void)
{
    LogRecord in = LogRecord_init_zero;
    in.which_payload = LogRecord_event_tag;
    in.payload.event.peak_mg = 100000;
    uint8_t buf[2];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof buf);
    TEST_ASSERT_FALSE(pb_encode(&os, LogRecord_fields, &in));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_event_round_trip);
    RUN_TEST(test_status_round_trip_and_max_size);
    RUN_TEST(test_undersized_buffer_fails_cleanly);
    return UNITY_END();
}
