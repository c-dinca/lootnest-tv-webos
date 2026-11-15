# Lootnest for TV (webOS)

The TV app of [Lootnest](https://github.com/c-dinca/lootnest), the Windows
game manager with profiles: it plays the games on your PC on an LG TV, with
a controller, inside Lootnest's TV mode. Lootnest on the PC installs this
app on the TV and pairs it; nothing needs typing on the TV.

This is a fork of [Moonlight TV](https://github.com/mariotaku/moonlight-tv)
by Mariotaku Lee and its contributors, itself a client of the
[Moonlight](https://moonlight-stream.org/) GameStream protocol. All the
streaming, decoding and input in this app is their work. Their README is
kept as [README.moonlight-tv.md](README.moonlight-tv.md).

## What this fork changes

Kept to as few files as possible, so Moonlight TV's fixes can still be
merged (`git remote add upstream https://github.com/mariotaku/moonlight-tv`):

- Its name, icon, splash screen and app id (`com.lootnest.tv`).
- Its own home screen in Lootnest's look (`src/app/ui/lootnest/`), in
  place of Moonlight TV's computer list and app grid, which are never
  shown: it pairs, wakes the PC, opens the PC's app named "Lootnest" and
  says what it's doing; Play, Settings and Quit when the stream ends.
  Lootnest's fonts (Tilt Warp, Atkinson Hyperlegible Next; OFL,
  `src/app/res/fonts/OFL.txt`) and its gem as the logo.
- Pairing from the launch parameters Lootnest passes when it installs the
  app (`{"host": "...", "pairPin": "1234"}`), so the PIN never needs
  typing.
- Holding Start and Select for two seconds ends the stream.

## Build

As Moonlight TV: see [README.moonlight-tv.md](README.moonlight-tv.md) and
`.github/workflows/build-webos.yml`, which builds the `.ipk` on every push.

## License

GNU General Public License v3.0, the same as Moonlight TV: see
[LICENSE.txt](LICENSE.txt). The libraries under `core/` and `third_party/`
keep their own licenses. "Moonlight" is used here only to say where this
app comes from; it isn't this app's name.
