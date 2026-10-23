// settings.cpp - automation settings page
//
// Uses the same terminal-style widgets as the dashboard: one row per toggle,
// description on the left, ON/OFF pill on the right.
#include "settings.h"

#include "automation.h"
#include "ui.h"

// A toggle row.  Returns true on click so the caller can flip the bool.
// The row is tall enough for the description to breathe.
static bool ToggleRow(Rectangle row, const char* label, const char* desc,
                      bool value, bool enabled)
{
    Vector2 m = GetMousePosition();
    bool hover = enabled && CheckCollisionPointRec(m, row);

    DrawRectangleRec(row, hover ? COL_HOVER : COL_PANEL_ALT);
    DrawRectangleLinesEx(row, 1, hover ? COL_ACCENT : COL_BORDER);

    DrawText(label, (int)row.x + 14, (int)row.y + 10, 18,
             enabled ? COL_TEXT : COL_GREY);
    DrawText(desc, (int)row.x + 14, (int)row.y + 34, 13,
             enabled ? COL_DIM : COL_GREY);

    // Status pill on the right
    const char* status = value ? "ON" : "OFF";
    int tw = MeasureText(status, 16);
    Rectangle pill = { row.x + row.width - 80, row.y + (row.height - 26) / 2, 64, 26 };

    Color pillBg = !enabled ? COL_BTN_OFF
                            : (value ? Fade(COL_GOOD, 0.25f) : Fade(COL_BAD, 0.20f));
    Color pillBorder = !enabled ? COL_GREY
                                : (value ? COL_GOOD : COL_BAD);
    Color pillText   = !enabled ? COL_GREY
                                : (value ? COL_GOOD : COL_BAD);

    DrawRectangleRec(pill, pillBg);
    DrawRectangleLinesEx(pill, 1, pillBorder);
    DrawText(status, (int)(pill.x + (pill.width - tw) / 2), (int)pill.y + 5, 16, pillText);

    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Apply the master switch's new value to every per-task flag.  Toggling the
// master off really means "no automation anywhere"; toggling it on turns every
// task on at once.  Individual tasks can then be tuned from that starting
// point without touching the master again.
static void ApplyMaster(Settings& s, bool enabled) {
    s.enabled              = enabled;
    s.autoRepair           = enabled;
    s.autoRecharge         = enabled;
    s.autoUpdate           = enabled;
    s.autoLaunch           = enabled;
    s.randomizeDestination = enabled;
    s.autoReturn           = enabled;
}

void DrawSettings() {
    // Page heading
    DrawText("AUTOMATION SETTINGS", 10, 62, 22, COL_TEXT);
    DrawText("Changes take effect immediately. The master switch sets every task below in one go.",
             10, 90, 14, COL_DIM);

    Settings& s = g_app.settings;

    // ---- Master switch card ----
    const Rectangle masterCard = { 10, 116, 1260, 100 };
    DrawRectangleRounded(masterCard, 0.01f, 4, COL_PANEL);
    DrawRectangleLinesEx(masterCard, 1, COL_BORDER);
    DrawText("MASTER CONTROL", (int)masterCard.x + 14, (int)masterCard.y + 10, 14, COL_DIM);

    const Rectangle masterRow = { masterCard.x + 14, masterCard.y + 32,
                                  masterCard.width - 28, 58 };
    if (ToggleRow(masterRow,
                  "Automation Enabled",
                  "Turn every automatic repair / recharge / update / launch / return on or off.",
                  s.enabled, true))
    {
        const bool newValue = !s.enabled;
        ApplyMaster(s, newValue);
        if (!newValue) ReleaseAllFromAutomation();
    }

    // ---- Per-task card ----
    // Fills the whole area below the master card (down to y=710).  Six rows
    // at 62 px each plus generous padding.
    const Rectangle card = { 10, 228, 1260, 482 };
    DrawRectangleRounded(card, 0.01f, 4, COL_PANEL);
    DrawRectangleLinesEx(card, 1, COL_BORDER);
    DrawText("AUTOMATION TASKS", (int)card.x + 14, (int)card.y + 10, 14, COL_DIM);

    const float rowH = 62.0f;
    const float gap  = 8.0f;
    const float x    = card.x + 14;
    const float w    = card.width - 28;
    float y = card.y + 34;

    // Individual task switches are always clickable - they are independent of
    // the master once it has set them.  (Master OFF still stops every task at
    // runtime because UpdateAutomation is gated on s.enabled.)
    if (ToggleRow({ x, y, w, rowH },
                  "Auto Repair",
                  "Repair damaged drones at the nearest location (15 s per repair).",
                  s.autoRepair, true))
        s.autoRepair = !s.autoRepair;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Recharge",
                  "Charge parked drones - fast task below 50% battery, plus a slow trickle otherwise.",
                  s.autoRecharge, true))
        s.autoRecharge = !s.autoRecharge;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Software Update",
                  "Install the latest software on outdated drones (15 s per update).",
                  s.autoUpdate, true))
        s.autoUpdate = !s.autoUpdate;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Launch",
                  "Send docked, healthy drones out on a mission from the Drone Facility.",
                  s.autoLaunch, true))
        s.autoLaunch = !s.autoLaunch;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Randomize Destination",
                  "ON: pick a fresh destination each launch. OFF: re-use the previous destination.",
                  s.randomizeDestination, true))
        s.randomizeDestination = !s.randomizeDestination;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Return To Launch",
                  "After arriving, fly back to the Drone Facility before parking.",
                  s.autoReturn, true))
        s.autoReturn = !s.autoReturn;
}