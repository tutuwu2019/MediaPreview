# Contributing

Thanks for your interest in contributing to MediaPreviewClient.

## Development Environment

- OS: Windows 11
- Qt: 6.5+ (`Widgets`, `Multimedia`, `MultimediaWidgets`)
- CMake: 3.16+
- Compiler: MinGW or MSVC

## Getting Started

1. Fork and clone the repository.
2. Configure Qt path and build with CMake preset.

```powershell
cmake --preset mingw-debug
cmake --build --preset build-mingw-debug
```

3. Run and verify the app launches correctly.

## Coding Guidelines

- Keep changes focused and minimal.
- Preserve existing module boundaries:
  - `mainwindow_ui.cpp`: UI layout and signal wiring
  - `mainwindow_interaction.cpp`: interaction and navigation
  - `mainwindow_media.cpp`: media resolving and playback
  - `mainwindow_thumbnails.cpp`: thumbnail rendering and cache/queue
- Prefer clear, descriptive names over abbreviations.
- Add comments only where logic is non-obvious.

## Testing Checklist

Before opening a PR, manually verify:

- Basic image/video preview works.
- LIVP switch (`LIVP优先视频`) behavior is correct.
- Album panel scrolling and centering do not jump.
- History browser grouping/collapse/selection works.
- App builds with no compile errors.

## Pull Request Guidelines

- Use a concise title and include motivation.
- Describe behavior changes and affected modules.
- Include screenshots or short clips for UI changes when possible.
- Mention any limitations or follow-up items.

## Asset and Copyright Notes

- Ensure icons/images added to the repo are legally reusable.
- Do not include private or non-redistributable media.
