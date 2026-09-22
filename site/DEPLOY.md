# Deploying the Island site

The site under `site/` is fully static (no build step, no dependencies) and all
page links are relative, so it runs unchanged on any static host.

## Vercel (fastest path)

```bash
npm i -g vercel          # once
cd site
vercel --prod
```

Deploying the `site/` folder directly means no project settings are needed;
`vercel.json` in the folder adds cache and security headers automatically.

## After the first deploy: point the SEO URLs at the new domain

Canonical, `og:url`, JSON-LD, the sitemap and robots entries ship pointing at
the GitHub Pages URL. Rewrite them to the deployed domain once, then commit:

```bash
python3 scripts/site_set_domain.py https://<your-project>.vercel.app
git commit -am "Point the site SEO URLs at <your-project>.vercel.app"
```

Running the script again with a different domain (or the original
`https://impelixx.github.io/island`) rewrites safely — only the exact current
domain is replaced.

## GitHub Pages (already wired)

`.github/workflows/pages.yml` deploys `site/` to GitHub Pages on every push to
`main` — no extra setup beyond enabling Pages once in the repository settings.
If you use Vercel as the primary host, the Pages copy still works; update the
SEO URLs to whichever domain should be canonical.
