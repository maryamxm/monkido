# 06 · EEPROM: saving the high score

The ATmega328P has three separate memories:

| Memory | Size | Survives power-off | Used for |
|---|---|---|---|
| Flash | 32 KB | yes | program code, `PROGMEM` constants |
| SRAM | 2 KB | no | variables, stack, the OLED buffer |
| EEPROM | 1 KB | yes | small settings and data, like the high score |

EEPROM can be rewritten byte by byte from your program. The costs: a write takes about 3.4 ms, and each cell is rated for about 100 000 write/erase cycles. Reads are fast and unlimited.

## Declaring it: `EEMEM`

```c
static uint8_t EEMEM ee_highScore_eep = 0xFF;   // 0xFF means "not initialized"
```

`EEMEM` places the variable in the `.eeprom` section. The linker gives it address 0 (the `.map` shows `.eeprom 0x810000 0x1`; `0x810000` is how avr-gcc tags EEPROM addresses). It is not a normal variable. You never read it directly, only through `eeprom_*()` with its address.

The `= 0xFF` initialiser does **not** get written on a normal upload. It only ends up in the separate `.eep` file (see below).

## Loading at boot: `main()`

```c
uint8_t stored = eeprom_read_byte(&ee_highScore_eep);
if (stored == 0xFF) {                       // erased EEPROM reads 0xFF
    stored = 0;
    eeprom_update_byte(&ee_highScore_eep, stored);
}
highScore = stored;
```

Erased EEPROM reads as `0xFF`, so `0xFF` doubles as "never written". That is safe here because a score can never reach 255: the real maximum is 48 (see [07](07-game-logic.md)).

## Saving a record: `gameOver()`

```c
if (lastScore > highScore) {
    highScore = lastScore;
    eeprom_update_byte(&ee_highScore_eep, highScore);
}
```

Two good habits are already here:

- **It only writes on a new record**, not every game.
- **`eeprom_update_byte()`** reads the cell first and skips the write if the value is unchanged. `eeprom_write_byte()` would always write.

Wear is not a concern at this rate. Even writing after every single game, 100 000 cycles is 100 000 games. Wear starts to matter when code writes in a loop, like logging every second (that would wear out a cell in about a day).

The library also handles the hardware's timed unlock sequence (`EEMPE`, then `EEPE` within 4 cycles) and waits for any previous write to finish.

## Why flashing the `.eep` file resets the score

The build produces two images (see [08](08-build-and-flash.md)):

- `mini-project.hex`: flash contents. The `.eeprom` section is stripped out (`-R .eeprom`).
- `mini-project.eep`: EEPROM contents. In this build it is:

```
:01000000FF00      1 byte at address 0x0000 = 0xFF
:00000001FF        end of file
```

Programming the `.eep` writes `0xFF` to the high score cell, and the next boot reads that as "never written", which shows as 0.

A **chip erase** has the same effect. ISP programming (Atmel-ICE, USBasp, Atmel Studio's Device Programming) normally erases the chip before writing flash. Chip erase wipes EEPROM too, unless the `EESAVE` fuse is programmed. On the Uno's default fuses (`hfuse = 0xDE`), `EESAVE` is not programmed, so **flashing over ISP resets the high score**. Uploading over USB through the bootloader doesn't erase the chip, so the score survives.

To keep the score across ISP flashing, program `EESAVE` (`hfuse 0xDE → 0xD6`). Read [08](08-build-and-flash.md) on fuses before touching them.

## Possible additions

- **A reset gesture.** There is no way to clear the record from the game. For example: if START is held at power-up, write 0. This goes after `io_init()`, so the pull-up is already on.

  ```c
  if ((PIND & (1 << START_BUTTON_BIT)) == 0) {
      eeprom_update_byte(&ee_highScore_eep, 0);
  }
  ```

- **Power loss mid-write** can corrupt the byte. The Uno's brown-out detector (2.7 V, from `efuse`) resets the chip before the voltage gets too low, which mostly prevents it. For important data, store a copy or a checksum.

## Check yourself

1. Why doesn't `ee_highScore_eep = 0xFF` reset the score on every upload?
2. What's the difference between `eeprom_write_byte` and `eeprom_update_byte`?
3. You flash with an ISP programmer and the high score is gone. Why?

<details><summary>Answers</summary>

1. The initialiser only goes into the `.eep` file, which is a separate image and isn't programmed unless you choose to.
2. `update` reads first and writes only if the value differs, which saves time and wear.
3. Chip erase wipes EEPROM because `EESAVE` is unprogrammed on the default Uno fuses.
</details>
