# Configuration

TwinLine reads one configuration file when it starts. Every setting has a
default, so the file is optional and may hold only the keys you want to
change.

## Where the file is

The first of these that is set decides:

1. `--config=FILE` on the command line (the file must exist);
2. the `TWINLINE_CONFIG` environment variable;
3. `$XDG_CONFIG_HOME/twinline/config.toml`;
4. `~/.config/twinline/config.toml`.

`--no-config` reads no file at all. `twinline config --path` prints the path
in use, and `twinline config` prints every setting in effect as a complete
file:

```sh
mkdir -p ~/.config/twinline
twinline config > ~/.config/twinline/config.toml
```

## Precedence

Built-in defaults, then the file, then command-line options. In the editor,
`set NAME VALUE` changes a setting for the rest of the session without
touching the file.

## Format

The file is [TOML](https://toml.io). TwinLine reads the part of TOML that a
settings file needs: `[tables]`, `key = value` pairs, `"basic"` strings (with
`\"`, `\\`, `\n` and `\t`), `'literal'` strings, integers (with `_`
separators), floats, `true` and `false`, and `#` comments, also after a
value.

Unknown tables or keys, values of the wrong type, values out of range and
keys set twice are errors. TwinLine stops with the file name and line rather
than guess, and exits with code 2:

```text
twinline: /home/me/.config/twinline/config.toml:3: unknown key 'treshold' in [analysis]
```

## Keys

### `[analysis]`

| Key | Type | Default | Meaning |
| --- | ---- | ------- | ------- |
| `threshold` | number | `0.70` | Pairs with a Jaccard similarity at or above this are near duplicates; above 0 and at most 1 |
| `threads` | integer or `"auto"` | `"auto"` | Worker threads, 1 to 256; `"auto"` uses one per processor |
| `strategy` | string | `"jaccard"` | `"jaccard"` (exact), `"exact"` (identical lines only) or `"minhash"` (fast for large files, may miss a few pairs) |
| `exhaustive` | boolean | `false` | With `jaccard`: score every pair, which adds the mean similarity |
| `fold` | string | `"default"` | Case folding: `"default"` or `"turkish"` (`I` becomes dotless `ı`) |
| `ngram` | integer | `2` | Longest word n-gram compared, 1 to 4: 1 ignores word order, higher values make it count more |

### `[ui]`

| Key | Type | Default | Meaning |
| --- | ---- | ------- | ------- |
| `theme` | string | `"dark"` | `"dark"`, `"light"`, `"dracula"` or `"nord"` |
| `animations` | boolean | `true` | `false` draws still frames (also `--no-animation`, `TWINLINE_REDUCE_MOTION=1`) |
| `color` | boolean | `true` | `false` turns colours off (also `--no-color`, `NO_COLOR`) |

### `[autosave]`

| Key | Type | Default | Meaning |
| --- | ---- | ------- | ------- |
| `enabled` | boolean | `true` | Write recovery files for unsaved changes |
| `interval` | integer | `30` | Seconds between autosaves, 1 to 3600 |

Recovery files are written next to the file (`.notes.txt.twinline.swp`)
with permissions `0600`, only for buffers that have a file name and unsaved
changes. Saving or quitting normally removes them. A recovery file left
by a crash is never overwritten until you `recover` it, discard it or save.

### `[watch]`

| Key | Type | Default | Meaning |
| --- | ---- | ------- | ------- |
| `enabled` | boolean | `true` | Report when another program changes or deletes an open file |

## A complete example

```toml
# ~/.config/twinline/config.toml

[analysis]
threshold  = 0.75
threads    = "auto"
strategy   = "jaccard"
exhaustive = false
fold       = "default"
ngram      = 2

[ui]
theme      = "nord"
animations = true
color      = true

[autosave]
enabled  = true
interval = 30

[watch]
enabled = true
```
