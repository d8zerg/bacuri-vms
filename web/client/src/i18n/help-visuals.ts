import settingsEn from "../help/screenshots/settings-en-100.png";
import settingsRu from "../help/screenshots/settings-ru-100.png";
import nodeAcceleration from "../help/illustrations/node-acceleration.svg";
import alarmTimeline from "../help/illustrations/alarm-timeline.svg";
import archiveBackfill from "../help/illustrations/archive-backfill.svg";
import archiveInterval from "../help/illustrations/archive-interval.svg";
import cameraAudio from "../help/illustrations/camera-audio.svg";
import cameraReplacement from "../help/illustrations/camera-replacement.svg";
import configurationRevision from "../help/illustrations/configuration-revision.svg";
import diagnosticTree from "../help/illustrations/diagnostic-tree.svg";
import exportChain from "../help/illustrations/export-chain.svg";
import floorDirection from "../help/illustrations/floor-direction.svg";
import manualRecording from "../help/illustrations/manual-recording.svg";
import monitorGrid from "../help/illustrations/monitor-grid.svg";
import offlineInstall from "../help/illustrations/offline-install.svg";
import privacyScope from "../help/illustrations/privacy-scope.svg";
import ptzLease from "../help/illustrations/ptz-lease.svg";
import recordingPath from "../help/illustrations/recording-path.svg";
import regionTiles from "../help/illustrations/region-tiles.svg";
import siteSignIn from "../help/illustrations/site-sign-in.svg";
import storageRetention from "../help/illustrations/storage-retention.svg";
import syncArchive from "../help/illustrations/sync-archive.svg";
import upgradeRollback from "../help/illustrations/upgrade-rollback.svg";
import videoRule from "../help/illustrations/video-rule.svg";

export const workstationSettings = {
  en: {
    src: settingsEn,
    alt: "Workstation settings",
    caption:
      "Open workstation settings to choose the language, theme and text size for all windows.",
  },
  ru: {
    src: settingsRu,
    alt: "Настройки рабочего места",
    caption:
      "В настройках рабочего места можно выбрать язык, тему и размер текста для всех окон.",
  },
};

type FigureText = {
  caption: string;
  keys: string[];
};

export const helpVisuals: Record<
  string,
  { src: string; en: FigureText; ru: FigureText }
> = {
  "node-acceleration": {
    src: nodeAcceleration,
    en: {
      caption: "Measured capacity and running jobs",
      keys: [
        "A hardware check measures capacity for each supported task.",
        "Jobs that fit can run on the GPU and consume part of its budget.",
        "When capacity is exhausted, additional jobs use CPU or wait in the queue.",
      ],
    },
    ru: {
      caption: "Измеренный бюджет и работающие задачи",
      keys: [
        "Проверка оборудования измеряет стоимость каждой поддерживаемой задачи.",
        "Задачи, которые укладываются в бюджет, могут занять часть ресурсов GPU.",
        "Когда бюджет исчерпан, дополнительные задачи переходят на CPU или ждут в очереди.",
      ],
    },
  },
  "access-privacy": {
    src: privacyScope,
    en: {
      caption: "Permission scope and a protected image",
      keys: [
        "Match the user or group to the intended observation point.",
        "The highlighted interval bounds access. Check each permitted action separately.",
        "The dark rectangle covers a selected region. Verify the visible result under the target role.",
      ],
    },
    ru: {
      caption: "Область разрешения и изображение с маской",
      keys: [
        "Сопоставьте пользователя или группу с нужной точкой наблюдения.",
        "Выделен интервал доступа. Проверьте каждое разрешённое действие отдельно.",
        "Тёмный прямоугольник закрывает выбранную область. Проверьте итоговое изображение под нужной ролью.",
      ],
    },
  },
  "activity-journal": {
    src: alarmTimeline,
    en: {
      caption: "Checking a journal entry against the archive",
      keys: [
        "Locate the time of the selected event on the timeline.",
        "Check whether recording covers that time. A hatched area marks missing video.",
        "Compare the selected frame with the journal entry before returning to the event history.",
      ],
    },
    ru: {
      caption: "Сверка записи журнала с архивом",
      keys: [
        "Найдите на шкале время выбранного события.",
        "Проверьте, есть ли запись за нужное время. Штриховка обозначает отсутствие видео.",
        "Сопоставьте кадр с записью журнала, затем вернитесь к истории события.",
      ],
    },
  },
  "apply-camera-batch": {
    src: configurationRevision,
    en: {
      caption: "Compare, apply and verify a batch of changes",
      keys: [
        "Review the current values of the selected channels before applying a template.",
        "Inspect the proposed differences and apply them to the intended selection.",
        "Read each channel’s result. Recheck unfinished changes before repeating the operation.",
      ],
    },
    ru: {
      caption: "Сравнение, применение и проверка пакетных изменений",
      keys: [
        "Перед применением шаблона просмотрите текущие значения выбранных каналов.",
        "Проверьте предлагаемые изменения и примените их к нужной выборке.",
        "Прочитайте результат по каждому каналу. Перед повтором проверьте незавершённые изменения.",
      ],
    },
  },
  "archive-playback": {
    src: archiveInterval,
    en: {
      caption: "Reading a gap, a bookmark and an interval",
      keys: [
        "The hatched span contains no available video. Check the recordings on both sides.",
        "The flag marks a bookmark at one instant. Use the time cursor to inspect its frame.",
        "The bracket spans a time interval. Check both endpoints separately.",
      ],
    },
    ru: {
      caption: "Как читать пропуск, закладку и интервал",
      keys: [
        "Заштрихованный участок обозначает отсутствие доступной записи. Проверьте кадры с обеих сторон.",
        "Флажок отмечает закладку в один момент. Проверьте её кадр указателем времени.",
        "Скобка охватывает интервал времени. Проверьте каждую границу отдельно.",
      ],
    },
  },
  "camera-audio": {
    src: cameraAudio,
    en: {
      caption: "Listening, intercom and stored audio",
      keys: [
        "The upper arrow runs from the camera to the workstation when listening.",
        "The reverse arrow carries permitted speech from the workstation to the camera.",
        "The separate storage branch represents the recording of audio when the policy and permissions allow it.",
      ],
    },
    ru: {
      caption: "Прослушивание, обратная связь и запись звука",
      keys: [
        "Верхняя стрелка направлена от камеры к рабочему месту при прослушивании.",
        "Обратная стрелка обозначает разрешённую передачу речи с рабочего места к камере.",
        "Отдельная ветвь к диску обозначает запись звука, когда её разрешают политика и права.",
      ],
    },
  },
  "camera-on-map": {
    src: floorDirection,
    en: {
      caption: "A floor selection and a camera’s field of view",
      keys: [
        "The highlighted floor determines which plan and cameras you see.",
        "The point marker locates the camera on that floor. Select it to open its events.",
        "The sector extends from the lens into the area it covers. Its centre line shows direction.",
      ],
    },
    ru: {
      caption: "Выбор этажа и сектор обзора камеры",
      keys: [
        "Выбранный этаж определяет, какой план и какие камеры видны.",
        "Метка показывает положение камеры на этаже. Выберите её, чтобы открыть события.",
        "Сектор начинается у объектива и охватывает видимую область. Осевая линия показывает направление.",
      ],
    },
  },
  "changes-not-saved": {
    src: configurationRevision,
    en: {
      caption: "Recheck a change against the current revision",
      keys: [
        "Read the current saved values again if another operation has changed the object.",
        "Compare the intended change with the current values before submitting it again.",
        "Read back the result to confirm which settings actually took effect.",
      ],
    },
    ru: {
      caption: "Сверка изменения с текущей версией",
      keys: [
        "Ещё раз прочитайте сохранённые значения, если объект изменила другая операция.",
        "Сопоставьте желаемое изменение с текущими значениями перед повторным применением.",
        "Повторно откройте результат и проверьте фактически применённые настройки.",
      ],
    },
  },
  "compare-recordings": {
    src: syncArchive,
    en: {
      caption: "Two camera tracks on the same time axis",
      keys: [
        "Each row belongs to a separate observation point.",
        "A gap on one track can coincide with valid recording on the other.",
        "The shared cursor selects one instant across rows. Compare frame times and any clock uncertainty.",
      ],
    },
    ru: {
      caption: "Две дорожки камер на общей шкале времени",
      keys: [
        "Каждая строка относится к отдельной точке наблюдения.",
        "Пропуск на одной дорожке может совпасть с сохранной записью на другой.",
        "Общий курсор выбирает один момент на всех дорожках. Сверьте время кадров и возможную неточность часов.",
      ],
    },
  },
  "connect-camera": {
    src: cameraReplacement,
    en: {
      caption: "Stream checks and the boundary of a camera replacement",
      keys: [
        "Check the recording and live profiles, then confirm a newly recorded frame.",
        "The camera on the left belongs to the earlier binding of the observation point.",
        "The vertical boundary separates the old and new recordings. Open a frame on each side.",
      ],
    },
    ru: {
      caption: "Проверка потоков и момент замены камеры",
      keys: [
        "Проверьте профили записи и эфира, затем подтвердите новый архивный кадр.",
        "Слева показана камера прежней привязки точки наблюдения.",
        "Вертикальная граница разделяет записи прежней и новой камер. Откройте по кадру с каждой стороны.",
      ],
    },
  },
  "connection-lost": {
    src: diagnosticTree,
    en: {
      caption: "Distinguish a single camera outage from a site outage",
      keys: [
        "Start with the view showing the problem. Check whether another permitted point is reachable.",
        "If another point works, focus diagnosis on the affected camera and its connection.",
        "If the site and several views are unavailable, check the workstation’s connection to the site.",
      ],
    },
    ru: {
      caption: "Как отличить отказ одной камеры от недоступности площадки",
      keys: [
        "Начните с проблемного просмотра. Проверьте, доступна ли другая разрешённая точка.",
        "Если другая точка работает, проверяйте проблемную камеру и её соединение.",
        "Если недоступны площадка и несколько просмотров, проверьте связь рабочего места с площадкой.",
      ],
    },
  },
  "export-fragment": {
    src: exportChain,
    en: {
      caption: "The source interval, package and recipient check",
      keys: [
        "Set the boundaries around the required episode and check the point.",
        "Prepare the permitted copy and keep the supplied package together.",
        "Verify the package and play the fragment to check its point and interval.",
      ],
    },
    ru: {
      caption: "Исходный интервал, комплект и проверка у получателя",
      keys: [
        "Задайте границы вокруг нужного эпизода и сверьте точку.",
        "Подготовьте разрешённую копию и сохраните комплект целиком.",
        "Проверьте комплект и воспроизведите фрагмент, чтобы сверить точку и интервал.",
      ],
    },
  },
  "install-site": {
    src: offlineInstall,
    en: {
      caption: "Three checks on an offline installation",
      keys: [
        "Verify the installation kit using the trusted signing information supplied separately.",
        "Install and configure the local node, then confirm that the site responds at its HTTPS address.",
        "After connecting a camera, open a newly recorded frame to confirm the recording path.",
      ],
    },
    ru: {
      caption: "Три проверки при автономной установке",
      keys: [
        "Проверьте установочный комплект с помощью отдельно полученных доверенных данных подписи.",
        "Установите и настройте локальный узел, затем проверьте доступность площадки по её HTTPS-адресу.",
        "После подключения камеры откройте новый архивный кадр, чтобы подтвердить работу записи.",
      ],
    },
  },
  "investigate-export": {
    src: exportChain,
    en: {
      caption: "From a retained interval to a verified copy",
      keys: [
        "The bracket selects part of the recording. Check the point and UTC boundaries before protecting the interval.",
        "The package contains the released media and its signed description. Choose the permitted output.",
        "The recipient uses an independently trusted public key, then checks the actual frames.",
      ],
    },
    ru: {
      caption: "От защищённого интервала до проверенной копии",
      keys: [
        "Скобка выделяет часть записи. Проверьте точку и границы по UTC перед включением защиты.",
        "В комплект входят выданное видео и его подписанное описание. Выберите разрешённый вариант.",
        "Получатель использует отдельно доверенный открытый ключ, затем сверяет сами кадры.",
      ],
    },
  },
  "manual-recording": {
    src: manualRecording,
    en: {
      caption: "Check both ends of a manual recording",
      keys: [
        "Start recording for the selected point. Note the actual start time.",
        "Stop the manual recording when the required interval has ended.",
        "Open an archive frame between those times to confirm that video was stored.",
      ],
    },
    ru: {
      caption: "Проверка начала и конца ручной записи",
      keys: [
        "Запустите запись выбранной точки. Зафиксируйте фактическое время начала.",
        "Остановите ручную запись по окончании нужного интервала.",
        "Откройте архивный кадр между этими отметками и убедитесь, что видео сохранилось.",
      ],
    },
  },
  "map-placement": {
    src: floorDirection,
    en: {
      caption: "Checking the floor, camera position and viewing direction",
      keys: [
        "Select the floor whose plan matches the camera’s physical location.",
        "Compare the marker position with a known location on the plan.",
        "Check the viewing sector against a visible landmark before saving the calibration.",
      ],
    },
    ru: {
      caption: "Проверка этажа, положения камеры и направления обзора",
      keys: [
        "Выберите этаж, план которого соответствует месту установки камеры.",
        "Сопоставьте метку с известным местом на плане.",
        "Перед сохранением калибровки сверяйте сектор обзора с видимым ориентиром.",
      ],
    },
  },
  "map-regions": {
    src: regionTiles,
    en: {
      caption: "A selected region and its tile coverage",
      keys: [
        "The outlined area is the region required for the worksite.",
        "Install the chosen region before relying on the map without external access.",
        "The hatched tile marks missing coverage. Check the affected area and zoom level.",
      ],
    },
    ru: {
      caption: "Выбранный регион и покрытие тайлами",
      keys: [
        "Рамка выделяет регион, необходимый для работы площадки.",
        "Установите выбранный регион, прежде чем использовать карту без внешней сети.",
        "Штриховка обозначает отсутствующий тайл. Проверьте нужную область и масштаб.",
      ],
    },
  },
  "privacy-masks": {
    src: privacyScope,
    en: {
      caption: "Where a privacy mask applies",
      keys: [
        "Choose the intended point and consider the permissions of its viewer.",
        "Check which outputs the policy covers. Permission to view an interval and its mask are separate settings.",
        "Check the covered region in the relevant live view, archive or exported copy.",
      ],
    },
    ru: {
      caption: "Где действует маска приватности",
      keys: [
        "Выберите нужную точку и учтите разрешения зрителя.",
        "Сверьте виды выдачи, к которым применяется политика. Право просмотра интервала и его маска задаются отдельно.",
        "Проверьте закрытую область в нужном эфире, архиве или выданной копии.",
      ],
    },
  },
  "ptz-busy": {
    src: ptzLease,
    en: {
      caption: "One camera under one operator’s control",
      keys: [
        "The solid path connects the owner to the camera. Check the owner shown for your point.",
        "The ownership time is limited. Wait for release or expiry according to the situation.",
        "The dashed command stops at the barrier. Recheck availability before sending another command.",
      ],
    },
    ru: {
      caption: "У одной камеры один владелец управления",
      keys: [
        "Сплошная линия обозначает путь команды владельца к камере. Проверьте, кто указан владельцем вашей точки.",
        "Время владения ограничено. В зависимости от ситуации дождитесь освобождения или истечения срока.",
        "Пунктирная команда остановлена у черты. Перед повторной отправкой проверьте доступность управления.",
      ],
    },
  },
  "ptz-control": {
    src: ptzLease,
    en: {
      caption: "Control of a movable camera",
      keys: [
        "The upper operator holds control and can issue movement commands.",
        "The clock and bar represent the limited control time. Release the direction control and check that movement stops.",
        "The second operator waits while the point is busy. Check the current owner before trying again.",
      ],
    },
    ru: {
      caption: "Управление поворотной камерой",
      keys: [
        "Верхний оператор владеет управлением и может перемещать камеру.",
        "Часы и полоса обозначают ограниченное время управления. Отпустите кнопку направления и убедитесь, что движение остановилось.",
        "Второй оператор ждёт, пока точка занята. Перед повтором проверьте, у кого управление.",
      ],
    },
  },
  "recording-missing": {
    src: recordingPath,
    en: {
      caption: "Live video and recording take different paths",
      keys: [
        "The camera can still supply a live picture through the upper branch.",
        "The lower branch needs the recording stream, an active recording policy and space on the archive volume.",
        "After fixing the cause, open a frame beyond the hatched gap with a time later than the repair.",
      ],
    },
    ru: {
      caption: "Эфир и запись проходят разными путями",
      keys: [
        "Камера может продолжать передавать эфир по верхней ветви.",
        "Для нижней ветви нужны поток записи, действующая политика записи и место на архивном томе.",
        "После устранения причины откройте кадр за штрихованным пропуском со временем позже исправления.",
      ],
    },
  },
  "recording-policy": {
    src: storageRetention,
    en: {
      caption: "Volume reserve and retained time cover different limits",
      keys: [
        "The upper bar represents volume capacity. Check how much space is in use.",
        "The hatched end is reserved free space. Recording can stop at the reserve boundary.",
        "The bracket on the time axis shows retained video. Open a frame at the required time to check actual depth.",
      ],
    },
    ru: {
      caption: "Резерв тома и глубина хранения задают разные пределы",
      keys: [
        "Верхняя полоса обозначает ёмкость тома. Проверьте, сколько места занято.",
        "Заштрихованный конец полосы обозначает резерв свободного места. У его границы запись может остановиться.",
        "Скобка на шкале времени обозначает сохранившуюся запись. Откройте кадр за нужное время для проверки фактической глубины.",
      ],
    },
  },
  "recover-camera-archive": {
    src: archiveBackfill,
    en: {
      caption: "Recovering part of a gap from device storage",
      keys: [
        "Check that the recording for the missing interval is available on the camera.",
        "The upper timeline shows the original gap in the site archive.",
        "The lower timeline distinguishes the recovered part from the hatched remainder. Open a recovered frame.",
      ],
    },
    ru: {
      caption: "Частичная догрузка пропуска из архива устройства",
      keys: [
        "Проверьте, есть ли на камере запись за пропущенный интервал.",
        "Верхняя шкала показывает исходный пропуск в архиве площадки.",
        "На нижней шкале видны догруженная часть и заштрихованный остаток пропуска. Откройте догруженный кадр.",
      ],
    },
  },
  "review-alarm": {
    src: alarmTimeline,
    en: {
      caption: "An alarm matched to a recorded frame",
      keys: [
        "The diamond marks the event time. The vertical line places it on the recording.",
        "Hatching marks a gap. A gap before the alarm differs from a gap at the event itself.",
        "The preview is linked to the event marker. Check its point and time before recording the outcome.",
      ],
    },
    ru: {
      caption: "Тревога и соответствующий ей архивный кадр",
      keys: [
        "Ромб обозначает время события. Вертикальная линия указывает его положение на записи.",
        "Штриховка обозначает пропуск. Важно различать пропуск до тревоги и отсутствие кадра в момент события.",
        "Кадр связан с отметкой события. Перед записью результата сверьте точку и время.",
      ],
    },
  },
  "sign-in": {
    src: siteSignIn,
    en: {
      caption: "Completing an administrator sign-in",
      keys: [
        "Start at the trusted site address and check the displayed account.",
        "Use the registered security key when the browser asks for the administrator’s factor.",
        "Check that the intended workspace opens with the expected access.",
      ],
    },
    ru: {
      caption: "Подтверждение входа администратора",
      keys: [
        "Откройте доверенный адрес площадки и проверьте указанную учётную запись.",
        "При запросе браузера подтвердите вход администратора зарегистрированным ключом безопасности.",
        "Убедитесь, что открылось нужное рабочее место с ожидаемым доступом.",
      ],
    },
  },
  "site-diagnostics": {
    src: diagnosticTree,
    en: {
      caption: "Choosing the scope of a diagnostic check",
      keys: [
        "Record the symptom and its time at the workstation.",
        "A healthy comparison point narrows the check to a camera or one recording path.",
        "Several failed points call for checking the site connection and service or volume status.",
      ],
    },
    ru: {
      caption: "Как выбрать область диагностики",
      keys: [
        "Зафиксируйте симптом и время его появления на рабочем месте.",
        "Если контрольная точка исправна, сузьте проверку до камеры или одного тракта записи.",
        "При отказе нескольких точек проверьте связь с площадкой и состояние служб или тома.",
      ],
    },
  },
  "start-monitoring": {
    src: monitorGrid,
    en: {
      caption: "A monitoring layout with one selected point",
      keys: [
        "The outlined tile is the selected point. Check its name before opening controls.",
        "The recording strip shows stored video. Open a recent frame to confirm recording.",
        "A crossed-out screen marks an unavailable view. The other tiles can keep playing.",
      ],
    },
    ru: {
      caption: "Раскладка мониторинга с одной выбранной точкой",
      keys: [
        "Рамка выделяет выбранную точку. Сверьте её название, прежде чем открывать управление.",
        "Полоса записи показывает сохранённое видео. Откройте недавний кадр, чтобы подтвердить запись.",
        "Перечёркнутый экран обозначает недоступное изображение. Остальные ячейки могут продолжать показ.",
      ],
    },
  },
  "update-restore": {
    src: upgradeRollback,
    en: {
      caption: "Safeguards and checks around a version change",
      keys: [
        "Keep the pre-change backup and its matching kit available to the responsible administrator.",
        "Verify the target kit before applying it. Keep the documented rollback path available.",
        "After upgrade or recovery, check access, an existing archive frame and a newly recorded frame.",
      ],
    },
    ru: {
      caption: "Подготовка и проверка при смене версии",
      keys: [
        "Сохраните резервную копию до изменений и соответствующий ей комплект у ответственного администратора.",
        "Проверьте целевой комплект перед применением. Сохраните возможность штатного отката.",
        "После обновления или восстановления проверьте доступ, прежний архивный кадр и новый записанный кадр.",
      ],
    },
  },
  "users-and-groups": {
    src: privacyScope,
    en: {
      caption: "Checking who can use a point and when",
      keys: [
        "Start with the user or group and the point involved.",
        "Compare the interval and individual permissions before granting or revoking an action.",
        "Check the image available to that account, including any applicable privacy mask.",
      ],
    },
    ru: {
      caption: "Кому и когда разрешён доступ к точке",
      keys: [
        "Начните с пользователя или группы и нужной точки.",
        "Сверьте интервал и отдельные разрешения перед выдачей или отзывом права.",
        "Проверьте изображение под этой учётной записью, в том числе применяемые маски.",
      ],
    },
  },
  "verify-export": {
    src: exportChain,
    en: {
      caption: "What to compare when checking an export",
      keys: [
        "Use the requested point and interval as the reference for the recipient’s check.",
        "Keep the supplied files together. A changed or missing file affects verification.",
        "Check the signature with the independently trusted public key, then inspect the video and allowed sound.",
      ],
    },
    ru: {
      caption: "Что сверять при проверке экспорта",
      keys: [
        "Используйте запрошенные точку и интервал как образец для проверки у получателя.",
        "Храните выданные файлы вместе. Изменение или отсутствие файла влияет на результат проверки.",
        "Проверьте подпись отдельно доверенным открытым ключом, затем просмотрите кадры и проверьте разрешённый звук.",
      ],
    },
  },
  "video-rules": {
    src: videoRule,
    en: {
      caption: "A detection zone, a crossing and a test trace",
      keys: [
        "The shaded polygon is the area being tested. Its corners define the boundary.",
        "The sample track crosses the dashed line. Compare the direction with the rule.",
        "The trace shows where a test episode exceeds a threshold. Check the corresponding frames before enabling the rule.",
      ],
    },
    ru: {
      caption: "Зона детекции, пересечение и результат проверки",
      keys: [
        "Закрашенный многоугольник обозначает проверяемую зону. Его вершины задают границу.",
        "Пример траектории пересекает пунктирную линию. Сверьте направление с правилом.",
        "На схеме видно, где в проверочном эпизоде превышен порог. До включения правила сверьте соответствующие кадры.",
      ],
    },
  },
};
