// settings.cpp - automation settings page
//
// Uses the same terminal-style widgets as the dashboard: one row per toggle,
// description on the left, ON/OFF button on the right.  No new widget API.
#include "settings.h"

#include "automation.h"
#include "ui.h"

// A toggle row.  Returns the hover/click state so the caller can flip the bool.
static bool ToggleRow(Rectangle row, const char* label, const char* desc,
                      bool value, bool enabled)
{
    Vector2 m = GetMousePosition();
    bool hover = enabled && CheckCollisionPointRec(m, row);

    DrawRectangleRec(row, hover ? COL_HOVER : COL_PANEL_ALT);
    DrawRectangleLinesEx(row, 1, hover ? COL_ACCENT : COL_BORDER);

    DrawText(label, (int)row.x + 14, (int)row.y + 8, 18,
             enabled ? COL_TEXT : COL_GREY);
    DrawText(desc, (int)row.x + 14, (int)row.y + 30, 13,
             enabled ? COL_DIM : COL_GREY);

    // Status pill on the right
    const char* status = value ? "ON" : "OFF";
    int tw = MeasureText(status, 16);
    Rectangle pill = { row.x + row.width - 80, row.y + 10, 64, 26 };

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

void DrawSettings() {
    // Page heading
    DrawText("AUTOMATION SETTINGS", 10, 62, 22, COL_TEXT);
    DrawText("Changes take effect immediately. Automation master switch is in the header.",
             10, 90, 14, COL_DIM);

    const Rectangle card = { 10, 116, 1260, 420 };
    DrawRectangleRounded(card, 0.01f, 4, COL_PANEL);
    DrawRectangleLinesEx(card, 1, COL_BORDER);
    DrawText("AUTOMATION TASKS", (int)card.x + 14, (int)card.y + 10, 14, COL_DIM);

    Settings& s = g_app.settings;

    // Master switch disables all the task toggles (they still hold their state
    // so you can turn automation back on later without resetting).
    const bool master = g_app.automation;

    const float rowH = 66.0f;
    const float gap  = 8.0f;
    const float x    = card.x + 14;
    const float w    = card.width - 28;
    float y = card.y + 36;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Repair",
                  "Repair damaged drones at the nearest location (15 s per repair).",
                  s.autoRepair, master))
        s.autoRepair = !s.autoRepair;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Recharge",
                  "Recharge drones whose battery drops below 50% (15 s per charge).",
                  s.autoRecharge, master))
        s.autoRecharge = !s.autoRecharge;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Software Update",
                  "Install the latest software on outdated drones (15 s per update).",
                  s.autoUpdate, master))
        s.autoUpdate = !s.autoUpdate;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Randomize Destination",
                  "Dispatch idle healthy drones to a random location after a short delay.",
                  s.randomizeDestination, master))
        s.randomizeDestination = !s.randomizeDestination;
    y += rowH + gap;

    if (ToggleRow({ x, y, w, rowH },
                  "Auto Return To Launch",
                  "After arriving, fly back to the launch point before parking.",
                  s.autoReturn, master))
        s.autoReturn = !s.autoReturn;

    // Status summary panel
    const Rectangle summary = { 10, 552, 1260, 158 };
    DrawRectangleRounded(summary, 0.01f, 4, COL_PANEL);
    DrawRectangleLinesEx(summary, 1, COL_BORDER);
    DrawText("CURRENT BEHAVIOUR", (int)summary.x + 14, (int)summary.y + 10, 14, COL_DIM);

    int onCount = 0;
    if (s.autoRepair)           onCount++;
    if (s.autoRecharge)         onCount++;
    if (s.autoUpdate)           onCount++;
    if (s.randomizeDestination) onCount++;
    if (s.autoReturn)           onCount++;

    DrawText(TextFormat("Tasks enabled: %d / 5", onCount),
             (int)summary.x + 14, (int)summary.y + 36, 16, COL_TEXT);

    DrawText(g_app.automation
                ? "Master automation: ON - tasks run automatically."
                : "Master automation: OFF - only manual actions via the Dashboard work.",
             (int)summary.x + 14, (int)summary.y + 62, 16,
             g_app.automation ? COL_GOOD : COL_WARN);

    const char* hint =
        "Tip: turning Randomize Destination off makes idle drones stay parked until you dispatch them.";
    DrawText(hint, (int)summary.x + 14, (int)summary.y + 96, 14, COL_DIM);

    const char* hint2 =
        "Press A to toggle master automation. Automation toggles are ignored while it is off.";
    DrawText(hint2, (int)summary.x + 14, (int)summary.y + 118, 14, COL_DIM);
}