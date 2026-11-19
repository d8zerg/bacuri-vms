# Bacuri VMS Platform

## Локальный стенд

Требуются Linux x86-64, нативный Docker Engine, плагин Docker Compose и Go Task. [Установка Go Task](https://taskfile.dev/docs/installation/)

```sh
task check
```

Команда проверяет сборки GCC и Clang, выполняет CTest с ASan/UBSan и TSan, запускает статический анализ и проверяет контракты и типы TypeScript. Затем она запускает Compose-стенд, применяет схемы хранилищ на временных базах и декодирует оба RTSP-потока. CMake и Ninja работают внутри контейнера разработки. Результаты сборки сохраняются в `build/`. Базовые образы закреплены по digest, пакеты сборки берутся из фиксированного снимка Debian. [Архив Debian Snapshot](https://snapshot.debian.org/)

| Команда | Действие |
| --- | --- |
| `task build` | Собрать C++ цели с пресетом `gcc-debug` |
| `task build PRESET=clang-debug` | Собрать с указанным пресетом CMake |
| `task test` | Собрать и запустить CTest. Поддерживает `PRESET` |
| `task lint` | Запустить clang-tidy |
| `task control:up` | Запустить HTTPS-сервер и процесс записи, применить миграции PostgreSQL |
| `task control:check` | Проверить вход, реестр, применение и восстановление конфигурации в изолированной базе |
| `task api:check` | Проверить схемы и примеры REST, gRPC и WebSocket |
| `task api:types` | Сгенерировать и проверить типы TypeScript в `build/contracts/api.d.ts` |
| `task check:report` | Выполнить `task check` и сохранить сведения об окружении, журнал и код завершения в `build/reports/check-*/` |
| `task stand:up` | Запустить PostgreSQL и тестовую камеру |
| `task stand:check` | Проверить PostgreSQL, RTSP-потоки и тома |
| `task stand:check-storage` | Проверить сохранность PostgreSQL и архива после перезапуска и лимит тестового тома в отдельном временном проекте Compose |
| `task schema:check` | Проверить манифест и начальные миграции PostgreSQL и SQLite на временных базах |
| `task stand:status` | Показать состояние контейнеров |
| `task stand:logs` | Показать последние журналы PostgreSQL и камеры |
| `task stand:down` | Остановить стенд с сохранением постоянных томов |
| `task shell` | Открыть оболочку в контейнере разработки |

`task --list` выводит список задач. `task test PRESET=clang-tsan` запускает CTest в отдельном контейнере без сети. Контейнер использует тестовый профиль seccomp, который разрешает вызов `personality` с флагом `ADDR_NO_RANDOMIZE` для инициализации TSan.

В сети Compose RTSP-адреса - `rtsp://rtsp-camera:8554/main` и `rtsp://rtsp-camera:8554/sub`. С хоста используется `127.0.0.1` и порт `8554`. Порт на хосте можно переопределить через `BACURI_RTSP_PORT`. Доступ к PostgreSQL предусмотрен внутри сети Compose с тестовой учётной записью стенда. Том архива и ограниченный том для испытаний сбоев подключаются к контейнеру разработки.

Содержимое тестового тома заполнения удаляется при завершении контейнера разработки.

- [План разработки PoC](docs/devplan-poc.md)
- [Паспорт стенда и условия испытаний](docs/poc-test-profile.md)
- [Доменная модель и границы хранения PoC](docs/poc-domain-model.md)
- [Контракты API, сеансы и доступ](docs/poc-api-contracts.md)
- [Журнал изменений](CHANGELOG.md)

## Сервер управления

`task control:up` запускает API на `https://localhost:8443/api/v1`. Порт задаётся через `BACURI_HTTPS_PORT`, разрешённые сети камер - через список CIDR в `BACURI_CAMERA_NETWORKS`. HTTPS использует Boost.Asio, Boost.Beast и OpenSSL. Синтетическая камера доступна серверу по адресам `rtsp://rtsp-camera:8554/main` и `rtsp://rtsp-camera:8554/sub`.

При первом запуске стенд создаёт локальный сертификат и отдельные ключи шифрования ядра и узла записи. Публичный сертификат можно получить командой `docker compose exec -T core cat /identity/tls.crt` и использовать для проверки TLS. Том каждого ключа требуется сохранять вместе с соответствующей базой. Процессы работают под разными UID и передают снимки конфигурации через gRPC по Unix-сокету. Узел восстанавливает последнюю применённую версию из зашифрованного снимка в SQLite.

Для создания учётной записи используйте `task control:create-user USERNAME=admin ROLE=administrator`, передав пароль одной строкой через stdin. Для оператора укажите `ROLE=operator` и выдайте доступ к зарегистрированной точке командой `task control:grant-point USERNAME=operator POINT=<UUID>`. Для входа браузер отправляет JSON в `POST /api/v1/session` с текущим HTTPS Origin. Запись конфигурации использует сессионную cookie и CSRF-токен. [Контракты и модель доступа](docs/poc-api-contracts.md)

## Прототип интерфейса

Команда `task prototype:up` запускает прототип на [http://127.0.0.1:8088](http://127.0.0.1:8088). Для остановки используйте `task prototype:down`. Порт можно задать через `BACURI_PROTOTYPE_PORT`.

Прототип использует тестовое видео и смоделированные состояния связи, записи и тревог. [Сценарии и протокол проверки](docs/poc-ui-prototype.md)
