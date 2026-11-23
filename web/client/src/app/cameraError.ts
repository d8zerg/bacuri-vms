import { HttpFailure } from "../adapters/api";

const failures: Record<string, string> = {
  revision_conflict: "Конфигурация изменилась. Заново сравните версии.",
  camera_image_unsupported: "Канал не поддерживает выбранную яркость.",
  camera_image_unconfirmed: "Камера не подтвердила значение яркости.",
  camera_batch_limit: "В одну партию можно включить до 64 каналов.",
  camera_authentication_failed: "Камера отклонила имя или пароль.",
  camera_audio_missing: "В выбранный момент звук не записан.",
  camera_audio_unavailable:
    "Камера не подтвердила доступность звукового канала.",
  camera_audio_unsupported: "Камера не поддерживает это направление звука.",
  camera_ptz_busy: "Камерой управляет другой оператор.",
  camera_command_unsent: "Соединение недоступно. Команда не отправлена.",
  camera_command_rejected: "Камера отклонила команду.",
  camera_ptz_unknown:
    "Нет подтверждения камеры. Проверьте её положение в эфире.",
  camera_ptz_unsupported: "Профиль камеры не поддерживает эту команду.",
  camera_tls_error: "Проверьте имя камеры и доверенный сертификат.",
  camera_incompatible_profiles:
    "Для подключения нужны два разных профиля H.264.",
  camera_transport_denied:
    "Адрес или транспорт камеры запрещён настройками площадки.",
  camera_protocol_error: "Камера вернула неподдерживаемый ответ ONVIF.",
  camera_already_registered: "Эта камера уже подключена на площадке.",
  camera_binding_conflict: "Камера точки изменилась. Выберите точку заново.",
  camera_selection_expired: "Повторите обнаружение и выбор профилей.",
  camera_discovery_unavailable:
    "Нет доступного интерфейса в разрешённом сегменте камер.",
  camera_unavailable: "Камера не ответила вовремя.",
};
export function cameraError(error: unknown) {
  return error instanceof HttpFailure ? failures[error.code] : undefined;
}
