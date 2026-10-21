# 07 · Game logic

All of the game lives in `main()` and a handful of helpers. There's no `enum` of states, but the code is still a state machine. The states are just encoded as "where the program counter is right now".

## The states

```
           power-on
              │
              ▼
   ┌──────► IDLE ─────────────── shows "HI nn", waits for START (no timeout)
   │          │ START pressed
   │          ▼
   │        INTRO ────────────── "START" 2 s, then "SC 0" 0.5 s, gameIndex = 0
   │          │
   │          ▼
   │   ┌──► ADD STEP ─────────── gameSequence[gameIndex++] = rand() % 4
   │   │      │
   │   │      ▼
   │   │    PLAYBACK ─────────── playSequence(): LED + tone, 100 ms gap, per step
   │   │      │
   │   │      ▼
   │   │    INPUT ────────────── checkUserSequence(): one press per step, 3 s timeout each
   │   │      │           │
   │   │   correct      wrong / timeout
   │   │      ▼           ▼
   │   └─ SHOW SCORE   GAME OVER ─ "GAME OVER!!" 1.5 s → "SC/HI" 4 s → "HI"
   │      "SC n" ~0.84 s   │       save record to EEPROM, gameIndex = 0
   └───────────────────────┘
```

In code:

| State | Where |
|---|---|
| IDLE | `wait_for_start_button_press()` |
| INTRO | the lines after it in `main()` |
| ADD STEP | top of `while (playing)` |
| PLAYBACK | `playSequence()` → `lightLedAndPlayTone()` |
| INPUT | `checkUserSequence()` → `readButtonsWithTimeout(3000, …)` |
| SHOW SCORE | the `else` branch: `oled_show_score(gameIndex)` |
| GAME OVER | `gameOver(lastScore)` |

Small oddity: in SHOW SCORE, `if (gameIndex > 0) delay_ms(90);` is always true at that point, so the pause is simply 840 ms.

## The sequence array

```c
#define MAX_GAME_LENGTH 50
static uint8_t gameSequence[MAX_GAME_LENGTH];   // values 0..3
static uint8_t gameIndex;                       // current sequence length
```

Each round appends one random step and replays the whole array from index 0. The player's input is compared step by step against the same array:

```c
for (uint8_t i = 0; i < gameIndex; i++) {
    if (!readButtonsWithTimeout(3000, &actualButton)) return false;   // too slow
    lightLedAndPlayTone(actualButton);
    if (gameSequence[i] != actualButton) return false;                // wrong
}
```

The wrong button still lights and beeps before the game ends, which is good feedback.

**Score** = rounds completed. After a successful round it is `gameIndex`. On failure it is `gameIndex - 1`, because the round in progress didn't count. Both mean the same thing.

## The 49-step cap and what actually happens

```c
gameSequence[gameIndex] = rand() % 4;
gameIndex++;
if (gameIndex >= MAX_GAME_LENGTH) gameIndex = MAX_GAME_LENGTH - 1;
```

This avoids writing past the array, but here is what it does round by round (this exact loop, run on a PC):

| Round | Steps played | Score if you fail now |
|---|---|---|
| 48 | 48 | 47 |
| 49 | 49 | 48 |
| 50 | 49 (same as round 49) | 48 |
| 51+ | 49 (same again) | 48 |

From round 50 on, the new step goes into `gameSequence[49]`, but playback only covers indices 0–48, so that step is never played. The sequence stops growing and repeats identically. The OLED says `SC 49` after you clear round 49, but **the highest score that can ever be saved is 48**, because the score on failure is `gameIndex - 1`.

A cleaner rule is to treat a full array as a win:

```c
if (gameIndex == MAX_GAME_LENGTH) {
    gameOver(MAX_GAME_LENGTH);       // or a dedicated "YOU WIN" screen
    playing = false;
    continue;
}
gameSequence[gameIndex++] = rand() % 4;
```

(No human is likely to hit 49, but edge cases like this are where bugs hide.)

## Everything blocks

Every step busy-waits: tones, `delay_ms()`, OLED updates, UART prints. Consequences:

- Buttons are read **only** inside `readButtonsWithTimeout()`. A press during playback is ignored.
- Timing is the sum of whatever each function happens to take. For example, playback is ~547 ms per tone instead of 50 ms (see [02](02-timing-and-sound.md)).
- Adding anything concurrent (an animation, a sound while waiting) is hard.

For a game this simple, blocking is a reasonable choice and makes the code easy to follow. The alternative is an explicit, non-blocking state machine driven by `millis()`:

```c
typedef enum { IDLE, INTRO, PLAYBACK, INPUT, SHOW_SCORE, GAME_OVER } state_t;

state_t  state = IDLE;
uint32_t t0;              // when the current state/step started

for (;;) {
    uint32_t now = millis();
    switch (state) {
    case IDLE:
        if (start_pressed()) { show("START"); t0 = now; state = INTRO; }
        break;
    case INTRO:
        if (now - t0 >= 2000) { gameIndex = 0; add_step(); step = 0; t0 = now; state = PLAYBACK; }
        break;
    case PLAYBACK:
        // turn the LED/tone for step on and off by time; when done: state = INPUT
        break;
    /* ... */
    }
    // nothing above waits, so the loop can also poll buttons, animate, etc.
}
```

Each state does a little work and returns, so the loop runs thousands of times per second. That is the usual structure for embedded firmware that has to do more than one thing at a time.

## Leftover

`gameOver()` sets `bool newRecord` but never uses it (avr-gcc warns: `variable 'newRecord' set but not used`). It is an obvious hook for a "NEW HI!" screen, which would need `N` and `W` added to `textFont`.

## Check yourself

1. The player clears round 5, then presses the wrong button in round 6. What score is saved?
2. Why can't the player press a button during playback?
3. What breaks if you remove the cap check entirely?

<details><summary>Answers</summary>

1. 5 (`gameIndex` = 6, minus 1).
2. Buttons are only polled inside `readButtonsWithTimeout()`, which runs after `playSequence()` returns.
3. Round 51 writes `gameSequence[50]`, past the end of the array, corrupting whatever sits next in RAM.
</details>
