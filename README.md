# Bacuri VMS Platform

## Локальный стенд

Требуются Linux x86-64, нативный Docker Engine, плагин Docker Compose и Go Task. [Установка Go Task](https://taskfile.dev/docs/installation/)

```sh
task check
```

Команда проверяет сборки GCC и Clang, выполняет CTest, ASan/UBSan и статический анализ. Затем она запускает Compose-стенд, запрашивает PostgreSQL и декодирует оба RTSP-потока. CMake и Ninja работают внутри контейнера разработки. Результаты сборки сохраняются в `build/`.

| Команда | Действие |
| --- | --- |
| `task build` | Собрать C++ цели с пресетом `gcc-debug` |
| `task build PRESET=clang-debug` | Собрать с указанным пресетом CMake |
| `task test` | Собрать и запустить CTest. Поддерживает `PRESET` |
| `task lint` | Запустить clang-tidy |
| `task stand:up` | Запустить PostgreSQL и тестовую камеру |
| `task stand:check` | Проверить PostgreSQL, RTSP-потоки и тома |
| `task stand:status` | Показать состояние контейнеров |
| `task stand:logs` | Показать последние журналы PostgreSQL и камеры |
| `task stand:down` | Остановить стенд с сохранением постоянных томов |
| `task shell` | Открыть оболочку в контейнере разработки |

`task --list` выводит список задач. `task build PRESET=clang-tsan` собирает бинарные файлы с TSan. На проверенном хосте штатный профиль Docker блокирует вызов `personality(ADDR_NO_RANDOMIZE)` при инициализации TSan.

В сети Compose RTSP-адреса - `rtsp://rtsp-camera:8554/main` и `rtsp://rtsp-camera:8554/sub`. С хоста используется `127.0.0.1` и порт `8554`. Порт на хосте можно переопределить через `BACURI_RTSP_PORT`. Доступ к PostgreSQL предусмотрен внутри сети Compose с тестовой учётной записью стенда. Том архива и ограниченный том для испытаний сбоев подключаются к контейнеру разработки.

Содержимое тестового тома заполнения удаляется при завершении контейнера разработки.

- [План разработки PoC](docs/devplan-poc.md)
- [Паспорт стенда и условия испытаний](docs/poc-test-profile.md)
- [Журнал изменений](CHANGELOG.md)
