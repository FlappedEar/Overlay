# User guide sources

This folder holds the end-user guide published to GitHub Pages at
<https://arekkozuch.github.io/VBOOverlay/>.

- `pages/*.html` contains one HTML body fragment per page. The first line of each page
  is a metadata comment: `<!-- title: Page title | nav: Sidebar label -->`.
- `assets/` holds the stylesheet and images. `overlay-sample.png` is a copy of the
  application-rendered `docs/assets/motorsport-broadcast-acceptance.png`.
- `build.py` uses only the Python standard library. It wraps each fragment with the
  shared navigation and writes the site. The build fails if a page is missing from
  `NAV`, or if an internal link points to a missing page or anchor.

Build and preview locally:

```bash
python3 docs/user-guide/build.py          # writes docs/user-guide/_site (ignored by Git)
open docs/user-guide/_site/index.html
```

The [User Guide workflow](../../.github/workflows/user-guide.yml) builds the guide on
pull requests that change it. It deploys the guide to GitHub Pages on pushes to `main`.
Publishing requires a one-time repository setting: **Settings → Pages → Build and
deployment → Source: GitHub Actions**.

Update the guide when a change affects anything a user sees: labels, defaults,
limits, messages, supported formats or workflows. Describe only what the application
does at that revision. Put gaps on `limitations.html`, not as promises elsewhere.
