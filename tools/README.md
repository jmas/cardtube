# Bundled command line tools

CardTube calls two external programs at runtime:

- **yt-dlp** – lists channel videos and downloads audio.
- **a media player** – `mpv` is strongly recommended, but `ffplay`, `mpg123`,
  `aplay` and `vlc`/`cvlc` are also detected.

Put the binaries in this folder (or make them available on `PATH`). The
application searches, in order:

1. the environment variables `CARDTUBE_YTDLP` / `CARDTUBE_PLAYER`
2. `tools/` next to the executable and in the current working directory
3. the parent directories of the executable (so `tools/` in the repository root
   is found by the desktop simulator)
4. on device: `/usr/share/<app>/tools`, `/usr/lib/<app>`, `/opt/<app>/tools`
5. the system `PATH`

## Desktop simulator (Windows)

```
tools/yt-dlp.exe        # copy the same yt-dlp.exe you use from the command line
```

For playback, install mpv (for example `winget install mpv`) or drop `mpv.exe`
here. Without a player the app can still list and download, it just cannot play.

## CardputerZero (aarch64 Linux)

```
tools/yt-dlp                     # Python zipapp, requires python3
# or
tools/yt-dlp_linux_aarch64       # standalone build, no python needed
```

A player such as `mpv` or `ffplay` must be installed on the device. These files
are deployed to `/usr/share/cardtube/tools/` by the Debian package when
present.

> [!NOTE]
> The binaries in this folder are intentionally ignored by git (`*.exe` and the
> generic build artefacts). Only this README is tracked.
