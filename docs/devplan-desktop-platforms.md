# Bacuri VMS Client support plan for Windows and macOS

[Русская версия](ru/devplan-desktop-platforms.md)

The current desktop release targets Linux. This plan covers porting the same client to Windows and macOS. Each port has its own workstation, package and acceptance results. The Linux release is accepted independently. The shared client workflows and their acceptance criteria are defined in the [Linux client plan](devplan-desktop.md).

| Area | Windows | macOS |
| --- | --- | --- |
| Video and map | Evaluate D3D11 hardware decoding, a GStreamer D3D11 sink and shared D3D11 textures with Qt Quick | Evaluate VideoToolbox decoding, IOSurface frame transfer and Metal output with Qt Quick |
| Keyboard | Open the command palette with Ctrl+K | Open the command palette with Cmd+K |
| Offline delivery | Signed MSI installer | Signed application in a DMG |

For each platform, complete the following steps.

1. **Record the workstation and build.** Record the OS version, CPU architecture, GPU and driver, monitors and scaling, controller and FIDO2 key. Have the Linux Docker Compose pipeline dispatch SDK and signing jobs to a runner for the target OS and collect its artifacts. Use the same camera profiles, numbered reference recordings and server build when comparing with Linux.
2. **Verify video, map and input.** Test the candidate graphics path with H.264 and H.265 live video and archive playback next to the map. Measure frame copies, seek accuracy and input latency under load. Test software fallback, graphics device loss and moving a window between displays at different scales. Record the graphics path and measured limits for that OS. [Linux measurements and graphics decision](adr/022-desktop-video-path.md)
3. **Verify sign-in and workstation integration.** Test the system browser, return to the initiating client instance, certificate trust, WebAuthn with a physical key and corporate sign-in through Keycloak. Check local administrator access during a provider outage. Verify native shortcuts, accessibility, the physical controller and both interface languages.
4. **Package and qualify the port.** On a clean offline machine, test installation, update, rollback, signature verification, help and export playback. Run the shared parity scenarios, load and failure tests and administrator and operator tasks on the installed package. Record the package hash, client and server versions, hardware and results for each required configuration. Accept each platform on its own evidence. [Shared parity scenarios](../desktop/parity.yaml)
