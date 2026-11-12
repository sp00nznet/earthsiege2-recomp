# Notice

The MIT licence in [LICENSE](LICENSE) covers what is ours in this repository:
the host, the setup scripts, the tools and the documentation. It does not
cover the game.

*Earth Siege 2* is © 1996 Sierra On-Line / Dynamix, and is not ours to
license. Its executables, data, movies, music and art are not in this
repository and are excluded by `.gitignore`.

- You need your own copy of the game to use any of this. Setup links your
  copy of the CD as `game/`; the pipeline reads it from there.
- Lifted code is a derivative work of the binary it came from. Anything the
  pipeline writes into `work/` or `src/recomp/gen*/` carries whatever licence
  the original carries, not this one. The MIT grant stops at our own work.
- The host's shape follows the Sim Theme Park recomp
  ([simthemepark-recomp](https://github.com/sp00nznet/simthemepark-recomp)),
  and it builds against [pcrecomp](https://github.com/sp00nznet/pcrecomp);
  both are ours and MIT. There is no third-party code in this repository.
