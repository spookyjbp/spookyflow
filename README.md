# spookyflow — milestone 4

A cover-flow game launcher for a jailbroken/CFW/HEN PS3, written in C with
PSL1GHT and Tiny3D. Milestone 1 was the **look and feel**; milestone 2 added
**real cover art** from your webMAN / multiMAN cover folders; milestone 3
**launches games through webMAN MOD**, with each game's settings applied;
milestone 4 adds **games on NTFS/exFAT external drives** (any number of
drives at once), through webMAN MOD's NTFS index.

![PS2 tab](preview/18_covers_ps2.png)

## What milestone 1 includes

- Loading screen (“spookyflow” with a soft glow, pulsing cards, live status
  line and progress bar). The library scan runs in small steps per frame so
  the animation never freezes. It stays up at least 5 s (many TVs are still black for 2–3 s after the
  video mode switch), then cross-fades
  into the cover flow.
- Three tabs: PS1, PS2, PS3 (switch with L1 / R1; they stop at PS1 and PS3).
- A large cover flow: the selected case is about 53% of the screen height,
  neighbours about 64% of that, evenly spaced and running off both edges.
  The selected case has a soft highlight frame and neighbours dim with
  distance. PS1 cases are square, PS2 and PS3 cases are tall.
- The game title in a rounded pill that changes width smoothly, plus info
  chips: position ("1 of 250") always, then genre, players and size when known.
- An evenly spaced control bar along the bottom.
- Letter jumping with L2 / R2: an A–Z rail appears above the controls bar;
  your letter grows and glows in the UI color, the focus glides from the
  letter you came from, letters with no games are dimmed, and it fades out.
- **Game Settings** panel (▲) and **Flow Settings** panel (START).
- Hold ◯ for 1.5 s to exit to the XMB (a ring fills around the ◯ icon).
- **Sound effects** play when you move, switch tabs, jump letters, open/close a
  Settings menu, change a value and launch. Set their volume (or 0% = off) in Flow Settings →
  **Sound Effects**. To use your own sounds, put WAV files (PCM 8- or 16-bit, mono
  or stereo, any sample rate, up to 3 s) in `/dev_hdd0/tmp/spookyflow/sounds/`
  named `move.wav`, `tab.wav`, `open.wav`, `close.wav`, `change.wav`,
  `confirm.wav`; any you don't add keep the built-in sound. Restart spookyflow
  after adding them; the log lists which files were used.
- Tab images are trimmed of transparent borders and smoothly scaled to fill the tab.
- Flow Settings → **Offline mode** sets offline mode for every PS2/PS3 game; a game's own
  Offline mode setting can still override it (Default / On / Off).
- Flow Settings → **Power Off PS3** turns the console off cleanly through webMAN MOD
  (its `/shutdown.ps3` command, same as the XMB's Turn Off System). One press of ✖.
  If webMAN MOD isn't running, nothing happens and a message says so.
- Flow Settings → **UI Color**: Blue, Red, Green, Purple, Pink, Orange or White.
- Flow Settings → **Sound Effects** is a volume slider (0% = off).
- In both Settings menus only the D-pad is used, one step per press.
- Flow Settings → **Pin to top of XMB** puts spookyflow at the top of the XMB Game
  column. It sets `ITEM_PRIORITY` in spookyflow's own PARAM.SFO
  (`/dev_hdd0/game/SPKF00001/PARAM.SFO`); restart the PS3 to see the change. The
  choice is re-applied automatically after you reinstall the PKG.
- The startup loading screen shows for about 5 seconds (monitors often stay black
  for 2–3 s after the video mode switch); it can't be skipped.
- A built-in **demo library** (65–75 well-known titles per tab), so you can
  test everything with no games installed. If your internal HDD has games in
  the usual webMAN folders, that tab shows your real games instead.
- A log file and a settings file you can read or send back.

## 1. Put the project on GitHub

You only need a free GitHub account and a web browser.

1. Sign in at <https://github.com>, click **+** (top right) → **New repository**.
   Name it `spookyflow`. Leave “Add a README” unticked. Click **Create repository**.
2. Unzip `spookyflow-milestone2.zip` on your computer.
3. On the new repository page click **uploading an existing file**. Drag in
   **everything inside** the unzipped `spookyflow` folder (the files *and* the
   `src`, `tools`, `tests`, `ci`, `pkgfiles`, `preview` folders), not the folder
   itself. Click **Commit changes**.
4. **Important — the hidden `.github` folder.** Its name starts with a dot, so
   many computers hide it and the browser upload often skips it. Without it,
   nothing gets built. Check: on your repository page, is there a `.github`
   folder? If not:
   1. Click **Add file** → **Create new file**.
   2. In the file-name box type exactly `.github/workflows/build.yml`
      (typing the `/` characters creates the folders).
   3. Open `.github/workflows/build.yml` from the unzipped project in a plain
      text editor (Notepad / TextEdit), copy all of it, paste it into the
      big box on GitHub.
   4. Click **Commit changes**.
   (Also optional: the `.gitignore` file — same method, name `.gitignore`.)

## 2. Get the PKG

1. In your repository, open the **Actions** tab. If GitHub asks, click
   **I understand my workflows, go ahead and enable them**.
2. A run named **build** starts automatically after each upload/commit. To start
   one by hand: **Actions** → **build** (left) → **Run workflow**.
3. Wait for it to finish (green tick, usually 5–10 minutes; the first run is
   the slowest).
4. Click the run, scroll to **Artifacts**, and download
   **spookyflow-pkg-…**. It's a zip; inside is `spookyflow.pkg`.
   - If that artifact is missing but **spookyflow-pkg-docker-fallback** exists,
     use that one instead (it was built with a different toolchain).
   - **previews** contains the same mockup pictures that are in `preview/`.
5. If the run is red, open the failed step, copy the last ~50 lines of the log
   and send them to me.

## 3. Install on the PS3

You need a PS3 running CFW or HEN (with HEN enabled before installing).

- **USB:** copy `spookyflow.pkg` to the **root** of a FAT32 USB stick, plug it
  into the PS3, then XMB → **Game** → **Package Manager** → **Install Package
  Files** → **Standard** (or **USB device**) → pick `spookyflow.pkg`.
- **Or via the HDD:** copy the pkg to `/dev_hdd0/packages/` (FTP, or webMAN's
  file manager) and install from **Package Manager** → **Install Package Files** →
  **Standard**. webMAN's own “install pkg” option works too.

The app appears under **Game** as **spookyflow**. If the normal `.pkg` refuses to
install on your setup, try `spookyflow.gnpdrm.pkg` from the same artifact.

## Controls

| Button | Cover flow | In a settings panel |
|---|---|---|
| D-pad ← → or left stick | D-pad: previous / next game, one per press. Left stick: hold to scroll faster (the stick does nothing in the Settings menus) | Change value |
| D-pad ↑ ↓ | — | Move between rows |
| L1 / R1 | Previous / next tab (PS1 ↔ PS2 ↔ PS3, stops at the ends) | — |
| L2 / R2 | Jump to previous / next letter (A–Z, # first) | — |
| ✖ | Launch (through webMAN MOD, see *Launching*) | Change / activate |
| ▲ | Open **Game Settings** for the selected game | — |
| START | Open **Flow Settings** | — (only ◯ goes back) |
| ◯ | **Hold 1.5 s** to exit to the XMB (tap does nothing) | Back (tap). Holding here never exits |
| SELECT | Next song (background music) | not used |

L2 details: in the middle of a letter, L2 first goes to the start of that
letter; press again for the previous letter. Empty letters are skipped. Jumps stop at the first and last letter (no wrap).

After closing a panel with ◯, the exit only starts from a *new* ◯ press on
the cover flow.

**Game Settings (▲)** — saved per game in spookyflow's own settings file
(no files are ever renamed):

- PS1: Auto-play, Emulator (Standard / Netemu)
- PS2: Auto-play, Emulator (Default / Netemu), Offline mode, PS2 Config (see *PS2 configs*)
- PS3: Auto-play, Offline mode

“Default (Off)” means “use the global Auto-play setting, which is Off”.
They are applied when the game is launched (see *Launching*). The RSX clock
row was removed in milestone 3; overclocking may come back later.

**Flow Settings (START)** — Auto-play (global, default Off), Music volume
(0% turns the music off), Shuffle music, Background (Stage, Drift, Aurora, Fireflies,
Ribbons, Halo, Twilight), Download missing covers (stub),
Ignore “The” when sorting, Rescan library, Quit to XMB.

## Where your games are read from (internal HDD)

| Tab | Folders (one level of subfolders is fine) | What counts as a game |
|---|---|---|
| PS1 | `/dev_hdd0/PSXISO`, `/dev_hdd0/PSXGAMES` | each `.cue` (its `.bin` is hidden); a `.bin`/`.img`/`.iso` without a `.cue` |
| PS2 | `/dev_hdd0/PS2ISO` | `.iso` and split `.iso.0` (+ `.iso.1`… sizes added) |
| PS3 | `/dev_hdd0/PS3ISO`, `/dev_hdd0/GAMES`, `/dev_hdd0/GAMEZ` | `.iso` / `.iso.0`; every folder in GAMES/GAMEZ |

A tab with no games found shows the demo list and a small “Demo library”
chip. Title IDs are read from tags like `[SLUS_209.14]` in file names, and
otherwise from inside the game (see below).
External/NTFS drives come in a later milestone.

## Cover art (milestone 2)

spookyflow looks for covers in the same places and with the same names as
webMAN MOD, so a webMAN / multiMAN cover pack works without changes. For each
game, in this order (`.JPG`, `.PNG`, `.jpg`, `.png`):

1. Next to the game, same name: `/dev_hdd0/PS2ISO/Gran Turismo 4.jpg`
   (this always wins, so it is the way to override one cover)
2. webMAN's temp folder: `/dev_hdd0/tmp/wmtmp/<game file name>.jpg`
3. PS1 / PS2 retro packs: `/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/SLUS_209.14_COV.JPG`
   (also under `/dev_hdd0/tmp/game_repo/main` and `/dev_hdd0/game/NPEA00374/USRDIR`)
4. By title ID: `<the same three folders>/covers/BLES80608.JPG`, plus
   `/dev_hdd0/GAMES/covers`,
   `/dev_hdd0/GAMEZ/covers`
5. `/dev_hdd0/tmp/wmtmp/covers/<ID>.jpg`
6. The game's own folder: `/dev_hdd0/PS2ISO/SLUS20914.jpg`

**Title IDs.** If the file name has no ID tag, spookyflow reads it from the
game itself: `SYSTEM.CNF` inside PS1/PS2 images (`.iso`, and `.bin` through
its `.cue`), the disc header of PS3 ISOs, and `PS3_GAME/PARAM.SFO` of PS3
game folders. Nothing is renamed.

**How it stays fast with 1000+ games.**

- Only the 30 games around the selection (plus the 15 visible on each other
  tab, so switching tabs shows covers at once) are kept in video memory: 64
  fixed texture slots, 21 MB in total. Nothing grows with library size.
- Loading happens on a background thread, nearest cover first. A cover fades
  in over the placeholder when it's ready, and the UI never waits for it.
- Each cover is fitted to the case (never cropped), resized smoothly and saved once as a
  small cache file: `/dev_hdd0/tmp/spookyflow/covers/`. That takes
  about 290–340 KB per game, so roughly 300 MB for 1000 games. After that,
  showing it is a single file read.
- Games without a cover are remembered, so their folders are searched only once.
- **Flow Settings → Rescan Library** clears that cache, which picks up covers
  you've added or replaced. You can also delete the
  `/dev_hdd0/tmp/spookyflow/covers` folder at any time.

The log lists the cover folders that were found and, for the first 400
covers, where each one came from (or the title ID it looked for).

## Stage background (default)

The default background is **Stage**: a dark stage with a reflective floor and
lights along its horizon. Over it, the left and right sets of light ribbons
sway, tilt and breathe on their own; about 110 single particles (sharp sparks
and soft glow dots, cut from the particle sprites)
each live on their own: born at a random spot, they fade in, drift and rise at
a pace set by their depth (near = bigger, faster, brighter), sway and twinkle,
fade out and are born again elsewhere; the horizon line and its glows pulse, soft
fog drifts over the floor in three layers (a seamless generated texture, each
layer at its own depth, pace and thickness), and glassy triangle / circle / cross / square shapes
float beside the title. The cases stand on its floor in perspective: the
selected case stands in front of the horizon (`LY_FLOOR_Y`), smaller cases
farther back stand nearer it (`LY_HORIZON_Y`), and each casts a soft contact
shadow and a reflection (every other background keeps the original centered
row). The lights, the horizon and the ribbons glow: soft halos (`art/stage_glow.jpg`)
breathe around them, and the ribbons' halos move with them. Everything is stored as brightness and tinted with
the UI color on the GPU (Flow Settings → UI Color); the brightest parts stay
white-hot. Assets: `art/stage.jpg` (scene), `art/stage_rib.jpg` (ribbons),
`art/stage_fx.png` (+ `.json`: glows, mist, particles), `art/stage_shapes.png`,
embedded by `tools/gen_stage.py`. `tools/make_stage.py <asset folder> <scene>
<particle folder> <scene with ribbons>` rebuilds them; the ribbons are the
difference between the two scene pictures, and single particles are picked out of
the particle PNGs (cut off-grid from their sheet, so stitched back together
first; `DOTS` in the script lists which ones). Pick other styles in Flow
Settings → Background.

The **Ribbons** style fills the whole screen with flowing bands of light,
from the top down to the floor. (The *Image* background, your own picture,
was removed in 0.6.0; if it was selected, Stage is used.)

**Reflections** (Flow Settings, On by default) mirror the bottom of each case
on the floor below it, fading out.

## Case images (optional)

spookyflow ships no case artwork. To show covers inside cases, put PNGs here
(Flow Settings → Custom Cases turns them on or off):

```
/dev_hdd0/tmp/spookyflow/cases/ps1.png
/dev_hdd0/tmp/spookyflow/cases/ps2.png
/dev_hdd0/tmp/spookyflow/cases/ps3.png
```

They use ManaGunZ's case layout, so ManaGunZ's own `PS1_CASE.PNG`,
`PS2_CASE.PNG` and `PS3_CASE.PNG` (from `/dev_hdd0/game/MANAGUNZ0/USRDIR/GUI/common/`)
work once copied and renamed in lowercase. Any size with the same
proportions works: the cover sits at 30,5 240×240 in a 275×250 PS1 image,
0,10 260×370 in a 270×390 PS2 image, and 0,30 260×300 in a 270×340 PS3
image, and the image is drawn over the cover. Without an image the cover is
shown on its own. The log says which case images were used.

## Game info line

Under the title: **players · size · position** (e.g. `7.6 GB · 25 of 73`).
Players are only known for demo games so far. Release years are planned but
have no source yet (the info line shows a year when one is known; the cover
cache keeps a field for it).

## External NTFS drives (milestone 4)

The PS3 can't read NTFS drives itself; **webMAN MOD's NTFS edition** can, and
spookyflow uses its index. webMAN's prepNTFS walks **every NTFS drive it has
mounted** (two drives, or more, at once) and writes one small pointer file per
game into `/dev_hdd0/tmp/wmtmp/`:

- `Name.ntfs[PS3ISO]` — a PS3 ISO (plus `Name.SFO`, its PARAM.SFO, and
  `Name.PNG`, its icon or a cover found next to the ISO on the drive)
- `Name.ntfs[PSXISO]` — a PS1 game (.bin/.iso, with its .cue)

spookyflow lists those (PS3 ISOs on the PS3 tab, PS1 games on the PS1 tab)
next to your internal-drive games: the size comes from the pointer file, the
PS3 title ID from `Name.SFO` (so covers by title ID are found), and ✖
launches the pointer file through webMAN like any other game.

On the drive, webMAN looks in `PS3ISO` and `PSXISO` (also `BDISO`/`DVDISO`
for videos, which spookyflow skips), at the drive's root or under `/PS3/`,
including one level of sub-folders (shown as a `[Folder]` tag, which
spookyflow drops from the title).

**Rescan Library** first asks webMAN to re-index the NTFS drives
(`/refresh.ps3?prepntfs`, "Updating NTFS drives…" on the loading screen) and
then scans, so games added to or removed from a drive show up. A normal
start reads the index as it is (webMAN also refreshes it itself when its
setup option to scan NTFS is on).

Not possible through webMAN's NTFS index: **PS2 ISOs** (webMAN doesn't index
`PS2ISO` on NTFS; PS2 ISOs have to be on the internal drive), **PS3 games as
folders** (only ISOs), and two copies of the same game file name on two
drives (their pointer files share a name, so one replaces the other).
A drive that is unplugged still shows its games until the next Rescan.

## Background music (milestone 5)

Put your music in **`/dev_hdd0/tmp/spookyflow/music/`** (the folder is made
on the first start; copy files with webMAN's FTP or a file manager). Files
directly in it and in its sub-folders (one level, e.g. `music/Album/`) are
played:

- **.mp3** — any bitrate, CBR or VBR, mono or stereo, any sample rate
  (decoded by minimp3, public domain). ID3 tags and embedded cover pictures
  are skipped.
- **.wav** — 16-bit PCM, mono or stereo, any sample rate.

Music starts when the cover flow appears and loops through the folder.
**Shuffle music** On plays every song once in a random order before any
repeats; Off plays them by file name. **Music volume** sets the level (the
UI sounds have their own volume); 0% stops it. The scale is gentle: 50% is
about a quarter as loud as 100%, and 100% is still under the UI sounds. **SELECT** skips to the next
song. Whenever a new song starts, a **Now Playing** card with the kittens
slides in at the top left for a few seconds; a name too long for the card
scrolls until its end has shown, and the card stays up until it has.
It gets quieter while the screen sleeps and fades out when you exit or
launch a game. Files that can't be played are skipped (the log says which).
Rescan Library also re-reads the music folder.

## PS2 configs (0.6.0)

PS2 ISOs run with `ps2_netemu`, which reads compatibility patches from a
**CONFIG** file next to the ISO (`Game.iso.CONFIG`). When there is none,
webMAN MOD copies the first one it finds for the game's ID, in this order:
the PS2 Config app's own file, then the **CUSTOM**, **NET**, **GX** and
**SOFT** sets (from the PS2 Config app, `/dev_hdd0/game/PS2CONFIG/USRDIR`,
or ManaGunZ, `/dev_hdd0/game/MANAGUNZ0/USRDIR/sys/CONFIG`).

**Game Settings → PS2 Config** (← →) lets you choose:

- **Auto** — webMAN decides, as before. The row shows what that will be,
  e.g. `Auto (NET)`, `Auto (game's own file)` or `Auto (none found)`.
- **CUSTOM / NET / GX / SOFT / PS2 Config app** — only the ones that exist
  for this game are offered. On launch spookyflow copies that file to
  `Game.iso.CONFIG`.

A CONFIG of your own (made with ManaGunZ, say) is never lost: before
replacing it spookyflow keeps it as `Game.iso.CONFIG.sfbak`, and choosing
Auto again puts it back. Switching back to Auto removes spookyflow's copy,
so webMAN picks again. The game's ID comes from the file name
(`[SLUS-21115]`) or from inside the ISO. Not available for PS2 Classics
(`.BIN.ENC`) or games without an ID. The log lists what was found and copied.

## Launching (milestone 3)

✖ on a game (or on a disc in **Choose Disc**) hands it to **webMAN MOD** and
spookyflow exits to the XMB; webMAN mounts the game there and, with
Auto-play on, starts it. webMAN MOD must be installed and running (it is
what mounts ISOs and folders on CFW/HEN). If it isn't, spookyflow shows
"Launching needs webMAN MOD running" and stays open.

spookyflow sends one request to webMAN (`http://127.0.0.1/`, built in
`src/core/launch.c`):

```
/wait.ps3?xmb;/mount_ps3<path>?random=0[&offline=1][&emu=<emulator>];/wait.ps3?2;/play.ps3
```

- **Wait for the XMB**: webMAN doesn't mount while an app is running, so it
  waits (up to 30 s) until spookyflow has closed.
- **Mount** the game file or folder (PS3 ISO / folder, PS2 ISO, PS1
  .cue/.bin/.iso); the path is URL-encoded.
- **Offline mode** (PS2/PS3, per game or global): `offline=1` turns the
  network off while the game runs; webMAN turns it back on afterwards.
- **Emulator**: PS1 Standard / Netemu → `ps1_emu.self` / `ps1_netemu.self`;
  PS2 Netemu → `ps2_netemu.self`, PS2 Default → the emulator saved in your
  webMAN settings (read from `/dev_hdd0/tmp/wm_config.bin`; webMAN decides if
  the file can't be read).
- **Auto-play** on: `/play.ps3` starts the disc icon. Off: the game is only
  mounted and waits on the XMB's disc icon.

Notes (from webMAN MOD's source): `offline=` must come before `emu=`, and
`emu=` also changes webMAN's default emulator until the next boot, which is
why spookyflow always names the emulator. If **webMAN's own Auto-play**
setting is on, webMAN starts every mounted game itself: spookyflow then
doesn't add `/play.ps3` (no double start), and games start even when
spookyflow's Auto-play is off.

Not tested on hardware yet: the request format follows webMAN MOD's source,
but the first launches on a real console are the test. The log
(`/dev_hdd0/tmp/spookyflow/log.txt`) records every request.

## Multi-disc games

Games named like `Final Fantasy VII (Disc 1)`, `(Disc 2)`… (also `(Disk 1 of 2)`,
`(CD1)`, `[Disc1of2]`, `Grandia CD2`, `Game - Disc 2`) show as **one** game
called `Final Fantasy VII`, with the sizes added up. Games whose names are the
same apart from their tags, like `Mortal Kombat - Deception (USA)` and
`Mortal Kombat - Deception (USA) (Bonus Disc)`, are grouped the same way; their
discs are listed by those tags ("USA", "USA, Bonus Disc"). The log lists every
grouped game and its discs. Its cover, title ID and Game Settings come from disc 1.
Pressing ✖ opens **Choose Disc**; pick a disc and press ✖ to launch it.

## Optional: your own tab images

You can replace the “PS1 / PS2 / PS3” tab text with your own pictures
(spookyflow does not include any logos). Put PNG files here:

```
/dev_hdd0/tmp/spookyflow/tabs/ps1.png
/dev_hdd0/tmp/spookyflow/tabs/ps2.png
/dev_hdd0/tmp/spookyflow/tabs/ps3.png
```

- Transparent background, about **200×64 px** (max 1024×256), ideally a
  **white** logo/word.
- A single-colour image is tinted automatically: dark on the active (white)
  tab, white on the others. A multi-colour image is shown as-is.
- The folder is created on first launch. Missing files fall back to text.
- Restart spookyflow after adding them. The log says whether each was found.

## Log and settings files

- Log: `/dev_hdd0/tmp/spookyflow/log.txt` — startup info, video mode, every
  folder scanned and how many games were found, every launch, every settings
  change, and how the app exited. Each line is saved immediately, so it
  survives a crash.
- Settings: `/dev_hdd0/tmp/spookyflow/settings.ini` (plain text).

To send me the log: open webMAN's file manager in a PC browser
(`http://<ps3-ip>/dev_hdd0/tmp/spookyflow/log.txt`) or FTP to the PS3, or copy
it to USB with a file manager (e.g. multiMAN / IRISMAN). Then paste its
contents (or attach it) in our chat.

## What to check (please report back)

1. **Loading screen:** appears right away, animates smoothly, the status line
   changes, and it fades into the cover flow (tabs and covers slide in).
2. **Size and layout** on your TV (720p and/or 1080p): selected cover big and
   centred, neighbours evenly spaced and running off both edges, nothing cut
   off at the top/bottom (some TVs crop the edges — tell me if the tabs or
   the bottom bar are clipped).
3. **Title pill:** sized to the title, animates when you move; long titles
   shrink a bit and then get “…”.
4. **Chips** (genre / players / size) sit clearly below the selected cover and
   above the bar.
5. **Smoothness:** tap ← →; each press moves one game smoothly (holding moves only one)
   Is it 60 fps or does it stutter?
6. **Tabs:** L1/R1 slide rows in and out and the white pill glides.
7. **Letter jump:** L2/R2 with the A–Z rail; check the two-press L2 behaviour.
8. **Panels:** ▲ and START open, rows highlight smoothly, ◯ closes; values
   change with ← → and ✖; Flow Settings scrolls with a thin scrollbar.
9. **Ignore “The”** reorders the list but keeps your current game selected.
10. **Exit:** hold ◯ — ring fills around the ◯ icon, screen fades, you're
    back at the XMB. Also try: tap ◯ (nothing happens), Flow Settings →
    Quit to XMB, and the PS button → Quit Game.
11. **Your real games** (if any) show up on the right tabs with clean titles
    and sizes. Send the log.
12. Optional: tab images.
13. **Covers:** your games show their real covers; they fade in shortly after
    you arrive and stay smooth while you scroll quickly. Games without a cover
    keep the plain placeholder. Hold ← or → across many games and check it
    stays 60 fps. Send the log (it lists where each cover came from).
14. **Second start:** covers appear almost at once (read from the cache).
15. **0.6.1:** only ◯ closes Game Settings and Flow Settings (▲ and START
    no longer do); the kittens on the Now Playing card have no glow behind them.
    **0.6.0:** Game Settings → PS2 Config picks the CONFIG for PS2 ISOs
    (Auto / CUSTOM / NET / GX / SOFT); SELECT “Next Song” is on the controls
    bar; the Ribbons background fills the whole screen; the Image background
    is removed.
    **0.5.1:** the Stage background no longer shows hard vertical lines
    near the left and right edges (its glow layer was drawn about 7% too
    narrow on a 16:9 screen; the ribbons were slightly too narrow too).
    **Milestone 5 (0.5.0):** background music from
    `/dev_hdd0/tmp/spookyflow/music` (.mp3/.wav), Music volume and Shuffle
    work, SELECT skips; check it never stutters while you scroll fast or
    while covers load, and send the log.
    **Milestone 4 (0.4.0):** games on NTFS/exFAT drives (any number) through
    webMAN MOD's NTFS index; Rescan re-indexes the drives first.
    **0.3.5:** the A–Z rail uses the controls bar's fill and outline.
    **0.3.4:** the PS1/PS2 Full Screen / Smoothing options (0.3.1-0.3.3) are
    removed: the emulator uses the XMB's own Game Settings (set them once there).
    The A–Z jump shows an alphabet rail instead of the big letter.
    **Milestone 3 (0.3.0):** games launch through webMAN MOD (mount at the
    XMB, then play), with per-game Auto-play, emulator and offline mode; the
    RSX clock row is removed.
    **Fix 30 (0.2.30):** the release-year sources (GameDB table, disc dates) are
    removed for now; the info line shows no year.
    **Fix 29 (0.2.29):** many more release years: borrowed from other regions
    of the same title, else read from the disc image (cover cache version 5,
    so every game is looked up once more).
    **Fix 28 (0.2.28):** the horizon glows and line are generated cleanly (the
    old glow sprite carried a stray fragment that slid out beside the selected
    case as the glow pulsed).
    **Fix 27 (0.2.27):** no confirm sound on Power Off PS3; the Stage fog is an
    even haze (its densest spots no longer drift by as a blob).
    **Fix 26 (0.2.26):** Power Off PS3 works on one press; the loading screen
    cross-fades smoothly into the cover flow (keeping its own background while
    it fades); the Stage ribbons never dip below the horizon.
    **Fix 25 (0.2.25):** the big bokeh orbs are removed from Stage.
    **Fix 24 (0.2.24):** the star gliding along the Stage horizon is removed.
    **Fix 23 (0.2.23):** comets removed from Stage.
    **Fix 22 (0.2.22):** Stage particles move one by one (sparks, glow dots,
    bokeh, comets) instead of as whole sprite sheets.
    **Fix 21 (0.2.21):** fast stick scrolling keeps the selected game centered
    (the row stiffens with scroll speed instead of trailing 2-3 games behind).
    **Fix 20 (0.2.20):** Stage lights and ribbons glow; other backgrounds use
    the original centered case row again.
    **Fix 19 (0.2.19):** natural fog over the floor replaces the mist sprites.
    **Fix 18 (0.2.18):** plain Stage scene with crisp animated light ribbons;
    new particles (sparkle bands, rising orbs, light streaks) replace the
    dusty star field.
    **Fix 17 (0.2.17):** new Stage scene; cases stand on the floor in
    perspective with contact shadows.
    **Fix 16 (0.2.16):** the cases stand on the floor (bottoms on the
    horizon); Stage rebuilt from the full-size animation assets.
    **Fix 15 (0.2.15):** Stage rebuilt from the layer sheet: clean scene,
    moving ribbons, stars and flares, sharper glass buttons. **Fix 14 (0.2.14):** the Stage background follows the UI color.
    **Fix 13 (0.2.13):** the Stage background is built in and is the default,
    with its shapes floating; existing installs switch to it once.
    **Fix 12 (0.2.12):** your own background picture (Background → Image);
    case reflections (Flow Settings → Reflections). **Fix 11 (0.2.11):** Flow Settings → Background has six animated styles
    (Drift, Aurora, Fireflies, Ribbons, Halo, Twilight) plus Image; all follow
    the UI color. **Fix 10 (0.2.10):** the title floats without a pill with a thin glowing
    line under it; the info is one line (year · players · size · position);
    release years from GameDB replace the genre. **Fix 9 (0.2.9):** the selected case has a soft glowing line under it
    instead of an outline; launching shows no pop-up (the log still records it).
    **Fix 8 (0.2.8):** the A–Z letter is just the letter (no box); all UI
    outlines glow slowly together (a 4-second breathing cycle).
    **Fix 7 (0.2.7):** more multi-disc names grouped (see Multi-disc games);
    only the selected case has the UI-color outline; Game Settings, Flow
    Settings and Choose Disc have it too. **Fix 6 (0.2.6):** ManaGunZ's covers folder is no longer searched.
    **Fix 5 (0.2.5):** multi-disc games show once and ✖ asks which disc;
    case images only come from `tmp/spookyflow/cases` (ManaGunZ's folder is no
    longer read). **Fix 4 (0.2.4):** sleep dims the screen (still visible); Flow Settings →
    Sleep Mode (Off, 1, 5, 10, 15, 30, 60 min; default 5) and Custom Cases
    (On/Off); UI-color outlines around the title, info chips, cases and the
    controls bar; Blue is the default UI color; without case images covers
    are shown bare (the drawn cases are gone). **Fix 3 (0.2.3):** with ManaGunZ installed, the cases are ManaGunZ's own
    case images. **Fix 2 (0.2.2):** cases are a thin clear plastic rim around the whole
    cover (ManaGunZ style; PS2 cases are black); covers are never cut at the
    edges; switching tabs shows covers at once and never flashes titles. **Fix 1 (0.2.1):** no highlight behind the selected case; the
    active tab is an outline in your UI color; the A–Z letter uses the UI
    color; the title sits centered between the tabs and the case, the info
    chips between the case and the controls; Flow Settings → Show Game Info
    hides the chips; after 5 minutes without input the screen fades to black
    and any button wakes it (that press does nothing else).

## Known unverified things (could not run on a PS3 or with the PS3 toolchain here)

- The code has not been compiled with the PS3 compiler yet; the first GitHub
  build is the first real compile. All PSL1GHT/Tiny3D function names were
  checked against the published headers, and the PS3 files pass a syntax
  check against stub headers made from those signatures.
- `tiny3d_UserViewport` scale meaning (pixels per virtual unit) is
  inferred from Tiny3D's own source; if the picture looks offset or
  wrongly scaled, this is the place.
- `Video_aspect == 1` meaning 4:3 is assumed; it only matters for 480/576 SD output.
- Directory test uses `st_mode` POSIX bits / `d_type == 1` (Cell SDK convention).
- Performance of ~12–18k vertices per frame through Tiny3D is
  expected to be fine but not measured.
- The CI toolchain tarball (from bucanero/ps3toolchain, used by Apollo Save
  Tool's CI) and the Docker fallback image are not maintained by us.
- `sfo.xml` uses ATTRIBUTE 0 and CATEGORY HG (standard homebrew).
- Music: MP3 decoding runs on the PPU in its own thread (priority 1100) with
  2 s buffered ahead; its CPU cost on the Cell is expected to be small (a few
  percent) but not measured.
- Cover textures use Tiny3D's 16-bit `A1R5G5B5` format. JPEGs are decoded
  with the system decoder (at 1/2–1/8 size when a cover is much bigger than
  needed), PNGs with `pngLoadFromFile` (PNG covers larger than 2048 px are
  skipped to keep memory bounded). Neither has run on hardware yet.

## Not yet (on purpose)

- Downloading missing covers (the Flow Settings row is still a stub).
- PS2 ISOs and PS3 game folders on NTFS drives (webMAN's NTFS index has neither).
- Folder sizes for `GAMES`/`GAMEZ` entries are not computed (chip hidden).

## For developers

```
make -f Makefile.host test      # unit tests of the portable core (gcc, ASan/UBSan)
make -f Makefile.host preview   # renders preview/*.png with the real UI code
make pkg                        # PS3 build (needs PSL1GHT + Tiny3D, $PSL1GHT set)
python3 tools/gen_font.py       # regenerate font atlases (src/core/font_data.c)
python3 tools/gen_icons.py      # regenerate button badges (src/core/icons_data.c)
python3 tools/cutout_cats.py    # art/cats_source.png -> art/cats.png (transparent kittens)
python3 tools/gen_cats.py       # art/cats.png -> src/core/cats_data.c + pkgfiles/ICON0.PNG
```

- `src/core/` — portable C99, no PS3 headers: title cleanup, sorting, letter
  table, springs, layout, settings, demo library, scanner, input/hold logic,
  cover cache, cover art lookup/processing/worker (`coverart.c`), title IDs
  from disc images (`discid.c`), background music (`music.c`, with
  `minimp3.h`), app state machine and the whole UI drawing
  (`ui.c`) on top of a tiny primitive interface (`render.h`).
- `src/ps3/` — PSL1GHT/Tiny3D: `render_tiny3d.c` implements `render.h`,
  plus pads, file system, tab images, the cover worker thread and JPEG/PNG
  decoding (`ps3_covers.c`) and `main.c`.
- `tools/preview/` — software rasterizer implementing `render.h`, so the
  previews are drawn by exactly the same code as the console.
- Colours: `src/core/theme.h`. Layout fractions: `src/core/layout.h/.c`.
- App ID `SPKF00001`, content ID `UP0001-SPKF00001_00-0000000000000000`.
