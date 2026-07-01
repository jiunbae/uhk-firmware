#include "segment_display.h"
#include "event_scheduler.h"
#include "led_display.h"
#include "macros/core.h"
#include "macros/status_buffer.h"
#include "macros/vars.h"
#include "timer.h"
#include <string.h>
#include "keymap.h"
#include "utils.h"
#include "debug.h"

#ifdef __ZEPHYR__
#include "device.h"
#else
#include "device.h"
#endif

#define CLOCK_CHANGE_INTERVAL 3000

uint16_t changeInterval = 1500;
uint32_t lastChange = 0;
segment_display_slot_record_t slots[SegmentDisplaySlot_Count] = {
    [SegmentDisplaySlot_Keymap] = { .text = { ' ', ' ', ' ' }, .active = true, .len = 3 }
};
uint8_t activeSlotCount = 1;
segment_display_slot_t currentSlot = SegmentDisplaySlot_Keymap;
static bool clockActive = false;
static uint8_t clockSyncHour = 0;
static uint8_t clockSyncMinute = 0;
static uint32_t clockSyncTime = 0;
static uint16_t clockDisplayedMinute = UINT16_MAX;

static bool isClockSlot(segment_display_slot_t slot);

static uint16_t getChangeInterval()
{
    return isClockSlot(currentSlot) ? CLOCK_CHANGE_INTERVAL : changeInterval;
}

static void scheduleSlotChange(const char* event)
{
    if (activeSlotCount > 1) {
        EventScheduler_Reschedule(Timer_GetCurrentTime() + getChangeInterval(), EventSchedulerEvent_SegmentDisplayUpdate, event);
    }
}

static void writeLedDisplay()
{
#ifndef __ZEPHYR__
    if (isClockSlot(currentSlot)) {
        LedDisplay_SetTextSimpleDigits(slots[currentSlot].len, slots[currentSlot].text);
    } else {
        LedDisplay_SetText(slots[currentSlot].len, slots[currentSlot].text);
    }
#endif
}

#if DEVICE_HAS_SEGMENT_DISPLAY
#define RETURN_IF_SEGMENT_NOT_PRESENT
#else
#define RETURN_IF_SEGMENT_NOT_PRESENT return
#endif

static bool handleOverrides()
{
    if (slots[SegmentDisplaySlot_Debug].active) {
        currentSlot = SegmentDisplaySlot_Debug;
        return true;
    } else if (slots[SegmentDisplaySlot_Macro].active) {
        currentSlot = SegmentDisplaySlot_Macro;
        return true;
    } else {
        return false;
    }
}

static bool isClockSlot(segment_display_slot_t slot)
{
    return slot == SegmentDisplaySlot_ClockHour || slot == SegmentDisplaySlot_ClockMinute;
}

static bool isSlotSelectable(segment_display_slot_t slot)
{
    if (!slots[slot].active) {
        return false;
    }
    return !clockActive || slot != SegmentDisplaySlot_Keymap;
}

static void formatClockText(char* text, uint8_t value, char suffix)
{
    text[0] = '0' + value / 10;
    text[1] = '0' + value % 10;
    text[2] = suffix;
}

static void setSlotTextWithoutRefresh(segment_display_slot_t slot, const char* text)
{
    activeSlotCount += slots[slot].active ? 0 : 1;
    memcpy(&slots[slot].text, text, sizeof(slots[slot].text));
    slots[slot].len = sizeof(slots[slot].text);
    slots[slot].active = true;
}

static void updateClockText()
{
    if (!clockActive) {
        return;
    }

    uint32_t elapsedMinutes = (Timer_GetCurrentTime() - clockSyncTime) / 60000;
    uint16_t totalMinutes = (clockSyncHour * 60 + clockSyncMinute + elapsedMinutes) % 1440;

    if (totalMinutes == clockDisplayedMinute) {
        return;
    }

    clockDisplayedMinute = totalMinutes;
    uint8_t hour = totalMinutes / 60;
    uint8_t minute = totalMinutes % 60;
    char text[3];

    formatClockText(text, hour, 'H');
    setSlotTextWithoutRefresh(SegmentDisplaySlot_ClockHour, text);
    formatClockText(text, minute, 'M');
    setSlotTextWithoutRefresh(SegmentDisplaySlot_ClockMinute, text);
}

static void changeSlot()
{
    if (!handleOverrides()) {
        do {
            currentSlot = (currentSlot + 1) % SegmentDisplaySlot_Count;
        } while (!isSlotSelectable(currentSlot));
    }
    lastChange = Timer_GetCurrentTime();
    scheduleSlotChange("SegmentDisplay - change slot.");

    writeLedDisplay();
}

void SegmentDisplay_SetText(uint8_t len, const char* text, segment_display_slot_t slot)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    activeSlotCount += slots[slot].active ? 0 : 1;
    memcpy(&slots[slot].text, text, len);
    slots[slot].len = len;
    slots[slot].active = true;
    lastChange = Timer_GetCurrentTime();
    currentSlot = slot;
    if (!handleOverrides() && !isSlotSelectable(currentSlot)) {
        currentSlot = SegmentDisplaySlot_ClockHour;
    }
    writeLedDisplay();
    scheduleSlotChange("SegmentDisplay - setText slot change");
}

void SegmentDisplay_DeactivateSlot(segment_display_slot_t slot)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    if (isClockSlot(slot)) {
        clockActive = false;
        clockDisplayedMinute = UINT16_MAX;
    }
    activeSlotCount -= slots[slot].active ? 1 : 0;
    slots[slot].active = false;
    if (currentSlot == slot) {
        changeSlot();
    }
}

void SegmentDisplay_Update()
{
    EventVector_Unset(EventVector_SegmentDisplayNeedsUpdate);
    RETURN_IF_SEGMENT_NOT_PRESENT;
    updateClockText();
    if (currentSlot == SegmentDisplaySlot_Debug) {
        activeSlotCount -= slots[SegmentDisplaySlot_Debug].active ? 1 : 0;
        slots[SegmentDisplaySlot_Debug].active = false;
    }
    if (Timer_GetCurrentTime() - lastChange >= getChangeInterval()) {
        changeSlot();
    }
}

void SegmentDisplay_UpdateKeymapText()
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    keymap_reference_t *currentKeymap = AllKeymaps + CurrentKeymapIndex;
    SegmentDisplay_SetText(currentKeymap->abbreviationLen, currentKeymap->abbreviation, SegmentDisplaySlot_Keymap);
}

void SegmentDisplay_SetClock(uint8_t hour, uint8_t minute, uint8_t second)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    clockSyncHour = hour;
    clockSyncMinute = minute;
    clockSyncTime = Timer_GetCurrentTime() - second * 1000;
    clockDisplayedMinute = UINT16_MAX;
    clockActive = true;
    updateClockText();

    lastChange = Timer_GetCurrentTime();
    currentSlot = SegmentDisplaySlot_ClockHour;
    handleOverrides();
    writeLedDisplay();
    scheduleSlotChange("SegmentDisplay - clock slot change");
}

void SegmentDisplay_DeactivateClock()
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    SegmentDisplay_DeactivateSlot(SegmentDisplaySlot_ClockHour);
    SegmentDisplay_DeactivateSlot(SegmentDisplaySlot_ClockMinute);
    clockActive = false;
    clockDisplayedMinute = UINT16_MAX;
}

void SegmentDisplay_SerializeVar(char* buffer, macro_variable_t var)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    switch (var.type) {
        case MacroVariableType_Float:
            SegmentDisplay_SerializeFloat(buffer, var.asFloat);
            break;
        case MacroVariableType_Int:
            SegmentDisplay_SerializeInt(buffer, var.asInt);
            break;
        case MacroVariableType_Bool:
            SegmentDisplay_SerializeInt(buffer, var.asBool);
            break;
        default:
            Macros_ReportErrorNum("Unexpected variable type:", var.type, NULL);
            break;
    }
}

void SegmentDisplay_SerializeFloat(char* buffer, float num)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    if (num <= -10.0f || 10.0f <= num ) {
        SegmentDisplay_SerializeInt(buffer, num);
        return;
    }

    int mag = 0;
    bool negative = false;

    if (num < 0.0f) {
        num = -num;
        negative = true;
    }

    while (num < 10.0f && mag < 10) {
        mag++;
        num *= 10;
    }
    if (negative) {
        buffer[0] = 'Z' - mag + 1;
    } else {
        buffer[0] = 'A' + mag - 1;
    }
    buffer[1] = '0' + (uint8_t)(num / 10.0f);
    buffer[2] = '0' + (uint8_t)(num) % 10;
}

void SegmentDisplay_SerializeInt(char* buffer, int32_t a)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    int mag = 0;
    int num = a;
    bool negative = false;

    if (num < 0) {
        num = -num;
        negative = true;
    }

    if (num < 1000 && !negative) {
        buffer[0] = '0' + num / 100;
        buffer[1] = '0' + num % 100 / 10;
        buffer[2] = '0' + num % 10;
    } else {
        while (num >= 100) {
            mag++;
            num /= 10;
        }
        buffer[0] = '0' + num / 10;
        buffer[1] = '0' + num % 10;

        if (negative) {
            buffer[2] = mag == 0 ? '-' : ('Z' + 1 - mag);
        } else {
            buffer[2] = mag == 0 ? '0' : ('A' - 1 + mag);
        }
    }
}

void SegmentDisplay_SetInt(int32_t a, segment_display_slot_t slot)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    char b[3];
    SegmentDisplay_SerializeInt(b, a);
    SegmentDisplay_SetText(3, b, slot);
}

void SegmentDisplay_SetFloat(float a, segment_display_slot_t slot)
{
    RETURN_IF_SEGMENT_NOT_PRESENT;
    char b[3];
    SegmentDisplay_SerializeFloat(b, a);
    SegmentDisplay_SetText(3, b, slot);
}

bool SegmentDisplay_SlotIsActive(segment_display_slot_t slot)
{
    return slots[slot].active;
}
