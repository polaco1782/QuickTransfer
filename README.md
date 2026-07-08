# QuickTransfer

QuickTransfer is a small Windows wxWidgets application for encrypted file transfer between two machines. The same executable can run in client or server mode from separate tabs. Once connected, either side can send files.

## Build

Requirements:

- Visual Studio 2022 with the C++ desktop workload
- vcpkg integrated with MSBuild, or `VCPKG_ROOT` configured for manifest mode

Open `QuickTransfer.sln`, select `Debug|x64` or `Release|x64`, and build. The solution contains one executable project, `QuickTransfer.App`. Dependencies are declared in `vcpkg.json`:

- wxWidgets
- OpenSSL
- standalone Asio
- nlohmann-json

`Release|x64` is configured for a fully static build through the `x64-windows-static` vcpkg triplet and the static MSVC runtime (`/MT`). `Debug|x64` keeps the default dynamic triplet for faster local iteration.

## Use

1. Start the app on both machines.
2. On one machine, open the Server tab, enter a key, choose a listen port, and click `Start Listening`.
3. On the other machine, open the Client tab, enter the same key, enter the host and port, and click `Connect`.
4. Use `Send File...` from either side.

Received files are written to the destination folder configured in the Settings tab. The app writes partial files with a `.qtpartial` suffix and renames them only after a successful transfer.

The key is intentionally not persisted in `settings.json`.
