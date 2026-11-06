# Install Mac Precision Touchpad v0.3.1

This fork supports Magic Trackpad 3 USB-C over USB and Bluetooth on Windows
10/11 x64 and ARM64. Download the `setup.exe` asset from
https://github.com/Sager1145/mac-precision-touchpad/releases/tag/v0.3.1
and the matching SHA256SUMS.txt. Setup selects the native architecture.
Separate `x64-driver.zip` and `arm64-driver.zip` downloads support manual installation.

## Signing and Windows preparation

The installer, uninstaller, helper scripts, driver binaries and catalogs are
signed with the local WDK test certificate. This is NOT a Microsoft/WHQL or
attestation production-signed release. It requires Windows test-signing mode.
It will not install normally on an unprepared Secure Boot production machine.

The public certificate thumbprint is:

    94A13BEE28D788FC4A7FA91C568968C7DBADDC69

Only the public certificate is included. The private key is never distributed.
See Microsoft's preparation instructions:
https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option

Prepare the test machine and reboot BEFORE running Setup. Setup checks the
active Code Integrity policy rather than assuming a pending boot setting has
taken effect. It never changes boot configuration, Secure Boot, BitLocker,
Memory Integrity or pairing. The certificate trust task is optional and
unchecked; select it if this public certificate is not already trusted.

### What certificate trust allows

The trust task adds the certificate to the machine's Trusted Root and Trusted
Publisher stores. Windows then treats anything signed with this key as coming
from a trusted publisher, not only this driver: programs, installers,
PowerShell scripts and driver packages. The certificate is self-signed, valid
until 2036 and cannot be revoked. Its key stays on the developer's build
machine, so anyone who obtained that key could sign software this machine
would trust.

If Setup added the trust, uninstall removes it again. Trust that already
existed before Setup ran is left alone. To remove it yourself from an
administrator terminal:

    certutil -delstore Root 94A13BEE28D788FC4A7FA91C568968C7DBADDC69
    certutil -delstore TrustedPublisher 94A13BEE28D788FC4A7FA91C568968C7DBADDC69

Installed drivers keep working after the trust is removed while Windows is in
test-signing mode. Installing or updating them again needs the trust.

## Install or upgrade

1. Verify the EXE hash against SHA256SUMS.txt.
2. Run Setup, approve its administrator prompt and read the test-signing notice.
3. Select certificate trust if needed, then install. Existing packages from this
   driver family are exported before installing the update.
4. Pair the trackpad using Windows Bluetooth settings, or connect USB-C.
   Unplug USB when testing Bluetooth. Reconnect after an upgrade if necessary.
5. Check pointing, click-dragging, two-finger scrolling, flick momentum and
   three/four-finger gestures. Configure gestures in Windows Touchpad settings.

The wizard creates an Installed apps entry, a Touchpad settings shortcut and
a copy of this guide. Files are always installed in Program Files\Mac Precision
Touchpad; Setup and the uninstaller run the installed scripts as administrator,
so the folder cannot be changed to one ordinary users could modify.
Driver backups, ownership records and logs are in:

    C:\Program Files\Mac Precision Touchpad State

Only SYSTEM and Administrators can open this folder, because the logs record
user and machine names. Each log keeps one previous copy once it passes 1 MB.

Setup also installs the `AmtPtpBattery` service, which shows the trackpad's
battery level over USB in Settings › Bluetooth & devices. It reads the level
from the trackpad's interface 0 (bound to WinUSB by the driver package) every
five minutes and when the trackpad is plugged in. Over Bluetooth the driver's
Bluetooth filter shows the level from the trackpad's own reports; nothing asks
a Bluetooth trackpad for it, because that request can crash Windows' Bluetooth
HID driver. It runs as LocalSystem with only the basic service privilege,
because storing the level for Settings needs administrator rights. Setup
stops it before an upgrade, and uninstall removes it. If it cannot be
installed, the driver install still succeeds without the battery display.

If a newer or higher-ranking driver is already selected, Windows may keep it.
Check Device Manager's Driver tab before assuming the active driver changed.
The installer propagates failures and asks for a reboot when Windows requests it.

## Uninstall and recovery

Use Windows Settings > Apps > Installed apps > Mac Precision Touchpad
(Sager1145) > Uninstall. Keep another input device available: removing a driver
can briefly disconnect the trackpad. Removal checks the recorded OEM INF name
and its SHA-256 hash and removes only packages owned by this installer. If
Windows has since reused a recorded OEM INF name for another package, that
entry is skipped and logged. If the ownership record is missing, no drivers
are removed and the application is still uninstalled. Drivers are removed
only after you confirm; if removal fails, the uninstall stops with the app
still installed. Removal does not force-delete unrelated packages, remove
certificate trust that Setup did not add, or change Windows boot settings.

Logs and backups are retained. To restore an exported driver from an elevated
terminal, supply the actual backup directory:

    pnputil /add-driver "C:\Program Files\Mac Precision Touchpad State\backup-YYYYMMDD-HHMMSS\*.inf" /subdirs /install

An old manually installed standalone Bluetooth package can remain in the
driver store after uninstall and may be selected again by Windows. The
uninstaller intentionally does not remove packages it did not install.
Setup also leaves identical packages that were staged before installation
outside its ownership record. Use Device Manager or the original OEM INF to
manage those packages.

Preview versions recorded ownership in
`C:\ProgramData\Sager1145\MacPrecisionTouchpad`. When upgrading, Setup adopts
those entries only if each package still matches its recorded hash and is
signed by this project's certificate. The old record is left in place for
reference.

After upgrading from v0.2.0 or earlier, reboot once. Those versions patched
the Bluetooth HID transport's dispatch table in memory, and the patch stays
until Windows restarts.

## Manual / unattended installation

Choose the driver ZIP matching the native Windows architecture listed in
Settings > System > About. Extract it and follow `INSTALL-DRIVER.txt` to prepare
test signing and trust the bundled public certificate. In an administrator
terminal opened in the extracted folder, run:

    pnputil /add-driver .\AmtPtpDevice.inf /install

Save the published `oemNN.inf` name reported by PnPUtil for later removal.
The driver-only archives contain the INF, catalogs, binaries and public
certificate. The installer wizard's backup and uninstall entry apply to Setup installs.

For unattended Setup on an already prepared machine, use:

    setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS="trustcert" /LOG="setup.log"

Trust is an explicit unattended option too. Remove /TASKS="trustcert" if it is
not wanted. A failed prerequisite/install returns a nonzero exit status; do
not treat a staged package as proof that disconnected hardware was tested.

## Build the installer

Build both driver targets with Enterprise WDK 10.0.28000.0. Install Inno Setup
6.7.3 and run:

    .\tests\build-release-x64.ps1 -Platform x64
    .\tests\build-release-x64.ps1 -Platform ARM64
    .\installer\Build-Installer.ps1

Compiler and SignTool paths are configurable parameters. The signing
certificate with its private key must already exist in the build user's
Personal certificate store. The build verifies every driver signature and
catalog membership, signs helper scripts, and uses Inno's SignTool integration
to sign Setup, its temporary copies and the uninstaller. It emits one EXE,
two architecture-specific driver ZIPs, SHA256SUMS.txt and payload manifests
with the source commit.

Installer API references:
https://jrsoftware.org/ishelp/topic_setup_signtool.htm
https://jrsoftware.org/ishelp/topic_scriptevents.htm
https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation
