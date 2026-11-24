# Publishing these files as a GitHub Wiki

The Markdown files in `docs/wiki/` are ready to be used as a GitHub Wiki. A GitHub Wiki is a **separate Git repository** attached to your project, so copying the files into the main repository alone does not publish them.

## One-time setup

1. Go to your repository on GitHub.
2. Open the **Wiki** tab and click **Create the first page** (this initializes the wiki repository).
3. On the wiki page, find **Clone this wiki locally** and copy the URL. It ends in `.wiki.git`:

   ```
   https://github.com/<user>/<repository>.wiki.git
   ```

## Copy the pages

In a terminal, clone the wiki and copy the Markdown files into it:

```bash
git clone https://github.com/<user>/<repository>.wiki.git
cd <repository>.wiki
cp ../<repository>/docs/wiki/*.md .
git add .
git commit -m "Add Lua API documentation"
git push
```

On Windows PowerShell:

```powershell
git clone https://github.com/<user>/<repository>.wiki.git
cd <repository>.wiki
Copy-Item ..\<repository>\docs\wiki\*.md .
git add .
git commit -m "Add Lua API documentation"
git push
```

Reload the Wiki tab — the pages will be there.

## Special file names GitHub recognizes

| File | Purpose |
| --- | --- |
| `Home.md` | Wiki landing page. |
| `_Sidebar.md` | Navigation shown beside every page (included here). |
| `_Footer.md` | Text shown at the bottom of every page (included here). |

All other `.md` files become regular wiki pages. Links such as `[Lua API Reference](Lua-API-Reference.md)` work because the file names match the links.

## Keeping the wiki in sync

Treat `docs/wiki/` in the main repository as the **source of truth**:

1. edit the Markdown files there, alongside the code;
2. copy them to the wiki clone and push when the documentation changes.

This way the docs can be reviewed with pull requests in the main repository, and the GitHub Wiki is just a published copy.

> If you prefer not to use the separate Wiki feature, the same files also work as a `docs/` folder in the main repository: GitHub renders Markdown there automatically, and you can link to it from `README.md`.
