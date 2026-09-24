# Release Process

> Status: automated release pipeline candidate.

ESPNetworkSerial uses a single project version for the Arduino library release tag and the packaged host monitor.

## Dry-run the release pipeline

Before creating a public tag, open **Actions -> Release -> Run workflow** and run the workflow from `main`.

The default dry-run label is:

~~~text
0.0.18-dev
~~~

A dry run:

- validates the supplied version label;
- runs all Go monitor tests and the race detector;
- compiles BasicMonitor, CustomInstance, and StressEcho against ESP32 Arduino core 3.3.10;
- builds the host monitor for all supported release targets;
- injects the selected version into `espnetworkserial-monitor --version`;
- packages each binary with the monitor README, example config, and MIT license;
- builds the per-user Windows installer and runs its integration-script tests;
- creates `SHA256SUMS.txt`;
- uploads a complete release-assets workflow artifact;
- does **not** create a GitHub Release.

## Public tagged release

For a real release, first set `library.properties` to the desired semantic version, for example:

~~~text
version=0.1.0
~~~

Commit that change, ensure CI is green, then create and push the matching tag:

~~~text
v0.1.0
~~~

The release workflow strips the leading `v` and refuses to publish if the tag version does not exactly match `library.properties`.

A successful tagged workflow publishes a GitHub Release containing:

~~~text
espnetworkserial-monitor-<version>-windows-amd64.zip
espnetworkserial-monitor-<version>-linux-amd64.tar.gz
espnetworkserial-monitor-<version>-linux-arm64.tar.gz
espnetworkserial-monitor-<version>-macos-amd64.tar.gz
espnetworkserial-monitor-<version>-macos-arm64.tar.gz
espnetworkserial-setup-<version>-windows-amd64.exe
SHA256SUMS.txt
~~~

GitHub also provides the normal source archives for the tag. Those source tags are the artifacts relevant to Arduino Library Manager.

## Version injection

Local development builds use the fallback monitor version embedded in `monitor/main.go`, currently a `-dev` version.

Release builds override it at link time:

~~~text
-X main.monitorVersion=<release-version>
~~~

This keeps source development builds identifiable while ensuring packaged binaries report the exact release version.

## Current signing status

Release assets have SHA-256 checksums but are not code-signed yet. Windows Authenticode and macOS signing/notarization remain separate future work and must not be implied by the existence of the checksum file.
