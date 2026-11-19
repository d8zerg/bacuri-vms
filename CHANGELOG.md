# Changelog

Формат журнала основан на Keep a Changelog. Номера выпусков следуют Semantic Versioning. [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/) · [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)

## [Unreleased]

### Added

- Каркас `bacuri-vms-core` на C++23 с командой `--version` и пресетами CMake для сборки через Ninja на GCC и Clang.
- Локальный стенд Docker Compose с PostgreSQL, синтетической камерой с двумя потоками RTSP/H.264, постоянным томом архива и ограниченным томом для испытаний заполнения.
- Задачи Go Task для сборки, CTest, статического анализа и управления Compose-стендом. `task check` запускает полный набор проверок, включая ASan/UBSan, запрос к PostgreSQL и декодирование обоих RTSP-потоков.
- Концепт и архитектура Bacuri VMS, план разработки PoC и паспорт испытательного стенда.

### Changed

- Правила `.gitignore` учитывают артефакты CMake, Ninja и CTest и локальные настройки Docker Compose.
