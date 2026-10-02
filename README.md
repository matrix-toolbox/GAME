# GAME: Boulder Dash without gravity, merged with Sokoban and The Power

Rockford in a cave where nothing falls, with three sliding stones (red, green and blue) and Sokoban-style targets for boxes.

I have no name for it yet, so for now the game is simply called **GAME**.

I will not go into detail about the aim of the game. Those who have played the three games above will guess it immediately. Everyone else, please have fun finding out for yourself! :)  Hint: the RGB stones can be not only pushed but also kicked (fire + direction) so a kicked stone slides until something stops it. When properly combined, they form a firefly!

To build it, one needs a C compiler and the SDL2 development files (`sudo apt install build-essential libsdl2-dev`) then `make` and `./GAME`.

## History

I had been planning to write such a game for &approx;20 years but never found the time.

My first encounter with Boulder Dash was around 1988, then in 1991 I played The Power and later I discovered Sokoban. Each of these games has its own distinctive features and marks a milestone in gaming history. Ever since, I have wanted a game that combines their most original ideas. At first I meant to write it purely in `x86` assembly. The first attempts were made in 2020, but the project was never finished (instead, I focused on some [disassembly projects](https://github.com/matrix-toolbox/x86)). Recently, in the era of LLMs, it turned out that the right prompts were enough and within a few hours the entire project was ready. All I provided was my plan, preliminary graphics, instructions and a general vision of how it should be designed.

Credits go to Claude (Claude Opus 5.5 by Anthropic, working in Claude Code), which in October 2026 wrote all the code from my plan, rules, graphics and instructions: the engine, the random cave generator with its solvability checks, the drawing and the sounds. The concept, the rules, the graphics and the caves are mine (Claude drew only the explosion frames). Further credits:
- **Inspiration:** Boulder Dash (Peter Liepa and Chris Gray, First Star Software, 1984), The Power (1991) and Sokoban (Hiroyuki Imabayashi, Thinking Rabbit, 1982). This game is not affiliated with or endorsed by their rights holders. Boulder Dash is a trademark of its owner.
- **Sound:** the POKEY simulation comes from my earlier project BD_DREAM (UNRELEASED). The sound effects are based on Boulder Dash's: they are generated the way the Atari game generates them (on a simulated POKEY chip), but changed.
- **Font:** font8x8_basic by Daniel Hepper, based on Marcel Sondaar's font8x8 (public domain).

Since I do not hold the rights to Boulder Dash, I cannot use its original graphics. Out of respect, the version that looks like [this](BD_POWER.png) will not be published unless I obtain permission to do so.


## Caves

Editing caves is easy. The game comes with a few quickly designed caves that show off the new features. Put new caves next to the game as `C01.data`, `C02.data`, ... (in hex: `C09` is followed by `C0A`, up to `CFE`), with no gaps, because the game counts them at start-up and plays them in that order. Each file is read again whenever its cave starts, so one can edit a cave and try it immediately (`./GAME --cave C05.data` plays just that one). A random cave makes a good starting point: `./GAME --seed 42 --export C03.data`, then edit it.

A cave file is a plain text file:

```
diamonds         12        # needed to open the exit: 0-9999
time             300       # seconds on the clock: 1-9999
amoeba_time      60        # seconds until the amoeba grows fast: 0-9999
amoeba_limit     200       # an amoeba this big turns into stones: 1-880
colors           $C4 $46 $4C $04 $00   # COLOR0 COLOR1 COLOR2 COLOR3 background (optional)

map
iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii
i*.b   g   r   .000....................i
…                                         (22 rows of 40 characters)
```

`diamonds` is required while the other settings are optional. In the map, row 1 is the top of the cave and the steel border is part of the map, so a row shorter than 40 characters is padded with space. There must be exactly one `*` and at least one `E`.

| | | | |
|---|---|---|---|
| ` ` space | `.` dirt | `w` wall | `i` iron (steel) |
| `*` Rockford's start | `E` exit | `a` amoeba | `o` box |
| `x` `5` `6` `7` firefly, first moving left, up, right, down | | | |
| `0` diamond of a random colour | `1` `2` `3` red, green, blue diamond | | |
| `r` `g` `b` red, green, blue stone | `y` `m` `c` two joined stones: RG, RB, GB (as light mixes: yellow, magenta, cyan) | | |
| `_` target | `O` box on a target | `R` `G` `B`, `Y` `M` `C` stones on a target | `A` amoeba on a target |
| `=` dirt on a target | `W` wall on a target (must be blown up; the target remains) | | |

If a cave file contains an error, a warning shows what is wrong and where.

## Keys

| Key | |
|---|---|
| arrows, Control | move, fire |
| Z | go back in time while held: one tick first, then faster and faster, as far back as Rockford's birth. Playing on from there replaces the old future, but chance does not go back, so the amoeba grows differently. Works only while Rockford is in the cave (not after a death or the exit) |
| Space | pause (all sound stops) |
| Esc | give up the cave (costs a life), or quit (in the menu and after GAME OVER) |
| F2 | a new game: the menu at C01 |
| F3 | a new game: the menu at a random cave |
| M | sound on/off |
| F11 | full screen |

