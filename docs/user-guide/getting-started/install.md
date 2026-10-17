# Install Island

Island is distributed as **unsigned prereleases** on GitHub. This page shows how to download one and open it.

{% hint style="warning" %}
Builds are not code-signed or notarized. macOS Gatekeeper and Windows SmartScreen will warn you the first time. Only macOS on Apple Silicon (arm64) has been verified by hand; the other platforms build in CI but have not been checked on a real machine.
{% endhint %}

## Download

1. Open [GitHub Releases](https://github.com/island-browser/island/releases).
2. Pick a release:
   * `vX.Y.Z` is one prerelease per version.
   * `nightly` is rebuilt from the latest `main`, so it can be newer and less tested.
3. Download the file for your platform.

| Platform | Installer | Portable archive |
| --- | --- | --- |
| macOS, Apple Silicon | `island_browser-<version>-macosarm64.dmg` | `island_browser-<version>-macosarm64.zip` |
| macOS, Intel | `island_browser-<version>-macosx64.dmg` | `island_browser-<version>-macosx64.zip` |
| Windows, x64 | `island_browser-<version>-windows64-setup.exe` | `island_browser-<version>-windows64.zip` |
| Windows, ARM64 | `island_browser-<version>-windowsarm64-setup.exe` | `island_browser-<version>-windowsarm64.zip` |
| Linux, x64 | `island_browser-<version>-linux64.deb` | `island_browser-<version>-linux64.tar.gz` |
| Linux, ARM64 | `island_browser-<version>-linuxarm64.deb` | `island_browser-<version>-linuxarm64.tar.gz` |

{% hint style="info" %}
While the repository is private, only people with access to it can see releases and download assets.
{% endhint %}

## Verify the download (optional)

Each release has a `SHA256SUMS.txt`. To check a file on macOS or Linux:

```bash
shasum -a 256 <file>
```

Compare the result with the line for that file in `SHA256SUMS.txt`.

## Install on macOS

1. Open the `.dmg` and drag **Island** onto **Applications**. (With the `.zip`, unzip it and move the app instead.)
2. Open Island. macOS says it cannot verify the app.
3. Open **System Settings > Privacy & Security**, scroll down, and click **Open Anyway** next to Island.

To skip that dialog, remove the quarantine flag once from Terminal:

```bash
xattr -dr com.apple.quarantine /Applications/Island.app
```

## Install on Windows

1. Run `island_browser-<version>-windows64-setup.exe` (or `windowsarm64`). It installs for your user only.
2. If SmartScreen warns you, click **More info**, then **Run anyway**.

With the `.zip`, extract it anywhere and run the executable inside.

## Install on Linux

* Debian and Ubuntu: `sudo apt install ./island_browser-<version>-linux64.deb`. Island installs to `/opt/island-browser`.
* Other distributions: extract the `.tar.gz` and run the executable inside.

## Update Island

Island checks GitHub for new versions on its own and installs them for you. See [About and updates](../settings/about.md).

## Next

[First launch](first-launch.md)
