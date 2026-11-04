# Continuum 93 in a web page

`build web` (Windows) or `./build.sh web` (Linux/macOS) builds it in Docker. The output is in `build/web/`:

| File | What |
|---|---|
| `continuum.html` | The page (from `web/shell.html`) |
| `continuum.js`, `continuum.wasm` | The emulator |
| `continuum.data` | `Data/`, mounted at `/Data` |

Upload these four files together. Nothing else is needed.

## The website (`dist/web/`)

`package.cmd` / `./package.sh` assembles a site to upload as it is (e.g. to continuum93.tape-drive.xyz):

| Path | What |
|---|---|
| `index.html` | The page (from `web/index.html`): the machine, 16:9 and as large as the window allows, then the download buttons |
| `play/` | The four files above. The page shows `play/continuum.html` in an iframe |
| `downloads/` | Every zip from `dist/`. A button whose zip is missing is greyed out; the others show their size |
| `.htaccess`, `_headers` | The two headers for Apache and for Netlify/Cloudflare |
| `coi-sw.js` | A service worker that adds the headers when the server does not (HTTPS only; the page reloads once) |

The machine is in an iframe because SDL's fill-document mode takes over its whole document: it hides every
other element on the page. The iframe gets keyboard focus when it loads; the Full screen button enlarges it.

## Serving: two headers are required

The CPU and the audio run on their own threads, as on the desktop. Browsers allow threads
(`SharedArrayBuffer`) only on a page that is *cross-origin isolated*, so the server must send:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Without them, the page shows a message instead of the machine.

- **Local test** (Emscripten's `emrun` sends the headers), then open http://localhost:8093/continuum.html:
  ```
  docker run --rm -p 8093:8093 -v "<repo>/build/web:/web" -w /web emscripten/emsdk:6.0.13 emrun --no_browser --hostname 0.0.0.0 --port 8093 continuum.html
  ```
- **Netlify / Cloudflare Pages**: a `_headers` file next to the page:
  ```
  /*
    Cross-Origin-Opener-Policy: same-origin
    Cross-Origin-Embedder-Policy: require-corp
  ```
- **nginx**: `add_header Cross-Origin-Opener-Policy same-origin;` and `add_header Cross-Origin-Embedder-Policy require-corp;`
- **GitHub Pages** (and any host without header control): the site's `coi-sw.js` adds them in the browser.

## How it differs from the desktop build

- **Saved files are lost on reload.** The guest file system lives in memory.
  (shortcut: mount IDBFS on `/Data/filesystem` to keep them.)
- **The CPU is slower.** It runs at about 160–215 M instructions/s, against 400–1000 natively
  (`conformance --bench`; docs/PROGRESS.md, 2026-11-04). Halcyon boots and runs at that speed.
- **Float SIN/COS/TAN/CBR** can differ from Windows in the last bit (D-31), as on Linux.
- **Sound starts on the first click or key press.** Browsers do not allow sound before that.
- **The browser keeps some keys**, such as Ctrl+W, Ctrl+T and F11 (browser fullscreen). F1 opens service mode.
- **Needs a current browser**: WebAssembly threads, exceptions and tail calls
  (Chrome/Edge 112+, Firefox 121+, Safari 18.2+).
- **The log goes to the browser console** (F12).
