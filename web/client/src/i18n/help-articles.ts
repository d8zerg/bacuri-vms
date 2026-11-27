type ArticleText = {
  before: string;
  diagnosis?: { symptom: string; action: string }[];
  title: string;
  summary: string;
  keywords: string[];
  steps: string[];
  result: string;
};

export type Article = {
  id: string;
  section: "operator" | "administrator" | "troubleshooting";
  codes: string[];
  en: ArticleText;
  ru: ArticleText;
};

export const articles: Article[] = [
  {
    id: "start-monitoring",
    section: "operator",
    codes: [
      "video_session_limit",
      "video_bandwidth_limit",
      "video_transcoder_limit",
    ],
    en: {
      before:
        "Sign in as an operator. Open Monitoring with at least one point you may view.",
      diagnosis: [
        {
          symptom: "The site reports a limit.",
          action:
            "Choose a lower-bandwidth profile or close a spare tile. If the only remaining view still fails, give the administrator the message and number of active views.",
        },
      ],
      title: "Start a monitoring shift",
      summary:
        "Open a live camera, check the recording indicator and arrange several views on one screen.",
      keywords: [
        "first shift",
        "live video",
        "black tile",
        "grid",
        "layout",
        "stream quality",
        "all connections in use",
      ],
      steps: [
        "Open Monitoring, choose Live and select an observation point. Check the point name and wait for a moving image.",
        "Check the recording state beside the point. If it shows a delay or a stop, note the time and open Archive to check the latest recorded frame.",
        "Use the multi-camera view to choose a layout. Expand a camera to examine it, then press Escape to return to the grid.",
        "If the site reports a capacity limit, close an unused view. If the bandwidth limit is reached, choose the secondary stream under Live quality and open the view again.",
      ],
      result:
        "Your layout shows the permitted cameras. You can identify which points have live video and which are recording.",
    },
    ru: {
      before:
        "Войдите как оператор. Откройте «Мониторинг» с хотя бы одной доступной точкой.",
      diagnosis: [
        {
          symptom: "Площадка сообщает о лимите.",
          action:
            "Выберите поток с меньшим битрейтом или закройте ненужную ячейку. Если единственный просмотр по-прежнему недоступен, передайте администратору сообщение и число открытых просмотров.",
        },
      ],
      title: "Начать смену на посту",
      summary:
        "Откройте эфир камеры, проверьте состояние записи и разместите несколько камер на одном экране.",
      keywords: [
        "первое дежурство",
        "живое видео",
        "чёрная ячейка",
        "сетка",
        "раскладка",
        "качество эфира",
        "заняты все соединения",
      ],
      steps: [
        "Откройте «Мониторинг», выберите «Эфир» и точку наблюдения. Сверьте название точки и дождитесь движущегося изображения.",
        "Проверьте состояние записи у точки. Если видна задержка или остановка, отметьте время и откройте «Архив», чтобы проверить последний записанный кадр.",
        "Откройте многоканальный эфир и выберите раскладку. Разверните камеру для осмотра, затем нажмите Escape для возврата к сетке.",
        "Если площадка сообщает, что соединения заняты, закройте ненужный просмотр. При исчерпании полосы в меню качества эфира выберите дополнительный поток и откройте камеру повторно.",
      ],
      result:
        "В раскладке видны доступные камеры. Вы можете определить, у каких точек есть эфир и у каких ведётся запись.",
    },
  },
  {
    id: "camera-on-map",
    section: "operator",
    codes: [],
    en: {
      before:
        "Open Maps and plans as an operator with access to a point placed on the map or plan.",
      diagnosis: [
        {
          symptom: "The expected camera is missing from a plan.",
          action:
            "Check the selected building and floor and confirm that the point is available in Monitoring. If it appears there, ask the administrator to check its plan placement. Report the screen and symptom through the site channel without sharing plan images.",
        },
      ],
      title: "Find a camera on a map or floor plan",
      summary:
        "Locate a point, review its latest event on the map and return to the same place.",
      keywords: [
        "floor",
        "building",
        "marker",
        "camera events",
        "location",
        "preview",
      ],
      steps: [
        "Open Maps and plans. Select Region map or Plans. If the site has several plans, choose the building or floor in the Plan field.",
        "Select a camera marker. Open Camera events and choose Check beside the event you want to review. Use Archive and Live to compare the event with the current view.",
        "To handle an alarm, open it in monitoring. Use Maps and plans to return to the same point and map position.",
      ],
      result:
        "The preview or the alarm card shows the selected point. A gap or uncertain event time is identified on the recording.",
    },
    ru: {
      before:
        "Откройте «Карты и планы» как оператор с доступом к точке, размещённой на карте или плане.",
      diagnosis: [
        {
          symptom: "На плане нет ожидаемой камеры.",
          action:
            "Сверьте здание и этаж, проверьте доступность точки в «Мониторинге». Если там она видна, попросите администратора проверить размещение на плане. Передайте экран и признак сбоя по принятому каналу без изображения плана.",
        },
      ],
      title: "Найти камеру на карте или плане этажа",
      summary:
        "Найдите точку, посмотрите её событие на карте и вернитесь к тому же месту.",
      keywords: [
        "этаж",
        "здание",
        "метка",
        "события камеры",
        "местоположение",
        "предпросмотр",
      ],
      steps: [
        "Откройте «Карты и планы». Выберите карту региона или планы. Если планов несколько, выберите нужное здание или этаж в поле «План».",
        "Выберите метку камеры. Откройте «События камеры» и нажмите «Проверить» рядом с нужным событием. Переключайте «Архив» и «Эфир», чтобы сравнить запись с текущим изображением.",
        "Чтобы обработать тревогу, откройте её в мониторинге. Вернитесь через «Карты и планы» к той же точке и позиции карты.",
      ],
      result:
        "Окно просмотра или карточка тревоги показывает выбранную точку. Пропуск записи или неточное время события обозначены на экране.",
    },
  },
  {
    id: "ptz-control",
    section: "operator",
    codes: [
      "camera_ptz_unsupported",
      "camera_command_unsent",
      "camera_command_rejected",
      "camera_ptz_unknown",
    ],
    en: {
      before:
        "Open Live under Monitoring. You need a PTZ-capable camera and permission to control that point.",
      diagnosis: [
        {
          symptom: "The camera gave no confirmation.",
          action:
            "Check its actual position and the last command in the log. If movement cannot be confirmed, report the action, time and displayed state before another attempt.",
        },
      ],
      title: "Move and stop a PTZ camera",
      summary:
        "Use the live console to turn, zoom or recall a saved position and verify the movement.",
      keywords: [
        "pan",
        "tilt",
        "zoom",
        "focus",
        "preset",
        "patrol",
        "outcome unknown",
      ],
      steps: [
        "In Monitoring, open Live and choose the point. Check the live image before using the controls under the frame.",
        "Tap a direction for a single step or hold it to continue. Release to stop. With the console focused, the arrow keys move the camera and Escape stops pan and zoom. A patrol has its own stop control.",
        "Choose a preset by name to move to a saved position. If a command is rejected, check the available controls and permissions for this camera. If its outcome is unknown, check the live image and command log before trying again.",
      ],
      result:
        "The live view shows the expected direction and the movement stops when you release the control.",
    },
    ru: {
      before:
        "Откройте «Эфир» в «Мониторинге». Нужны поворотная камера и право управления этой точкой.",
      diagnosis: [
        {
          symptom: "Камера не подтвердила команду.",
          action:
            "Сверьте фактическое положение и последнюю команду в журнале. Если движение подтвердить не удалось, передайте действие, время и видимое состояние до повтора.",
        },
      ],
      title: "Повернуть и остановить PTZ-камеру",
      summary:
        "Управляйте поворотом и зумом в эфире, переходите к предустановкам и проверяйте результат движения.",
      keywords: [
        "поворот",
        "наклон",
        "зум",
        "фокус",
        "предустановка",
        "патруль",
        "исход неизвестен",
      ],
      steps: [
        "В разделе «Мониторинг» откройте «Эфир» и выберите точку. Перед работой с пультом под кадром проверьте изображение.",
        "Нажмите направление для одного шага или удерживайте для продолжения. Отпустите для остановки. Когда фокус на пульте, стрелки перемещают камеру, Escape останавливает поворот и зум. Для патруля предусмотрена своя кнопка остановки.",
        "Для перехода в сохранённую позицию нажмите название предустановки. При отказе проверьте доступные для камеры команды и права. Если исход команды неизвестен, сверьте изображение и журнал команд до повтора.",
      ],
      result:
        "В эфире виден выбранный ракурс. После отпускания кнопки движение прекращается.",
    },
  },
  {
    id: "review-alarm",
    section: "operator",
    codes: [
      "alert_state_conflict",
      "alert_result_required",
      "camera_clock_uncertain",
    ],
    en: {
      before:
        "Open the alarm feed in Monitoring. You need access to the point, its archive and alarm handling.",
      diagnosis: [
        {
          symptom: "Close is unavailable or the card changed.",
          action:
            "Reopen the card, check its owner and save a result before closing. Report the time, action and displayed error if the current owner still cannot close it.",
        },
      ],
      title: "Check an alarm against its recording",
      summary:
        "Review the frame at the time of the event, record what you saw and close the alarm.",
      keywords: [
        "alert",
        "take into work",
        "alarm owner",
        "activity journal",
        "false alarm",
        "record the result",
      ],
      steps: [
        "In Monitoring choose the alarm. Check the observation point and UTC time beside the archive frame. If the recording is being indexed, wait and retry. Record a visible gap in your findings. If the camera clock is uncertain, compare nearby frames before assigning an event time.",
        "Choose Take into work and check the owner on the card. If another operator claimed it, reopen the card to see the current state.",
        "Enter what you observed and choose Record the result. Enter the reason for closure and select Close the alarm.",
        "Open the log of this alarm to check its history, or return to the feed to continue your shift.",
      ],
      result:
        "The closed alarm retains its result, reason, responsible operator and link to the recording.",
    },
    ru: {
      before:
        "Откройте ленту тревог в «Мониторинге». Нужен доступ к точке, её архиву и обработке тревог.",
      diagnosis: [
        {
          symptom: "Закрытие недоступно или карточка изменилась.",
          action:
            "Откройте карточку заново, сверьте владельца и сохраните результат перед закрытием. Если действующий владелец по-прежнему не может закрыть тревогу, передайте время, действие и текст ошибки.",
        },
      ],
      title: "Проверить тревогу по записи",
      summary:
        "Посмотрите кадр на момент события, запишите наблюдение и закройте тревогу.",
      keywords: [
        "событие",
        "взять в работу",
        "владелец тревоги",
        "журнал действий",
        "ложная тревога",
        "результат проверки",
      ],
      steps: [
        "В «Мониторинге» выберите тревогу. Сверьте точку и время UTC рядом с архивным кадром. Если запись добавляется в каталог, подождите и повторите проверку. Видимый пропуск укажите в результате. Если время камеры неточно, сравните соседние кадры до привязки события ко времени.",
        "Нажмите «Взять в работу» и проверьте владельца в карточке. Если тревогу уже взял другой оператор, заново откройте карточку, чтобы увидеть её состояние.",
        "Опишите увиденное и нажмите «Записать результат». Введите причину закрытия и нажмите «Закрыть тревогу».",
        "Откройте журнал этой тревоги, чтобы проверить историю, или вернитесь в ленту для продолжения дежурства.",
      ],
      result:
        "У закрытой тревоги сохранены результат проверки, причина, ответственный оператор и связь с записью.",
    },
  },
  {
    id: "investigate-export",
    section: "operator",
    codes: ["export_too_large"],
    en: {
      before:
        "Open Investigations under an account allowed to view the selected recordings and protect case material. Release requires separate permission.",
      diagnosis: [
        {
          symptom: "The recipient cannot open the material.",
          action:
            "Check the permitted points, interval and representation with the administrator. Give the affected time range and error code through the site's support channel.",
        },
      ],
      title: "Protect a case and release its materials",
      summary:
        "Find an interval on several cameras, retain its recordings and hand over a verified copy.",
      keywords: [
        "case",
        "investigation",
        "bookmark",
        "hold",
        "evidence",
        "mask",
        "signature",
        "offline verification",
      ],
      steps: [
        "In Investigations select up to four points and enter the search interval. Choose a thumbnail, move along the shared timeline and check gaps on each track.",
        "Add a bookmark at the required time. In Cases enter the number, owner, reason and retention term. Save the case to protect its intervals.",
        "In Case package select the permitted original or masked copy, enter the reason for release and include audio only when permitted for every selected point. Download the signed case package. If the requested package is too large, reduce the intervals.",
        "Give the recipient the package and have them verify it offline with the site's independently trusted public key. Check the point, times, reported gaps, visible masking and permitted sound against the request.",
      ],
      result:
        "The retained interval can be reopened from the case. The recipient can confirm the signature and the contents of the exported copy.",
    },
    ru: {
      before:
        "Откройте «Расследования» под учётной записью с правами просмотра выбранных записей и защиты материалов. Для выдачи нужно отдельное разрешение.",
      diagnosis: [
        {
          symptom: "Получатель не может открыть материал.",
          action:
            "Сверьте разрешённые точки, интервал и вид выдачи с администратором. По каналу поддержки площадки передайте границы интервала и код ошибки.",
        },
      ],
      title: "Защитить дело и выдать материалы",
      summary:
        "Найдите интервал на нескольких камерах, защитите запись от очистки и передайте проверенную копию.",
      keywords: [
        "дело",
        "расследование",
        "закладка",
        "удержание",
        "материалы",
        "маски",
        "подпись",
        "автономная проверка",
      ],
      steps: [
        "В «Расследованиях» выберите до четырёх точек и задайте интервал поиска. Откройте миниатюру, переместитесь по общей шкале и сверьте пропуски на каждой дорожке.",
        "Добавьте закладку на нужном времени. В «Делах» укажите номер, ответственного, основание и срок защиты. Сохраните дело, чтобы защитить интервалы от очистки.",
        "В комплекте дела выберите разрешённый оригинал или копию с масками, укажите основание выдачи. Для звука нужны права на каждую точку. Скачайте подписанный комплект дела. Если запрошенный объём слишком велик, сократите интервалы.",
        "Передайте комплект получателю. Для автономной проверки он использует открытый ключ площадки, полученный по независимому доверенному каналу. Сверьте точку, время, пропуски, маски и разрешённый звук с запросом.",
      ],
      result:
        "Защищённый интервал открывается из дела. Получатель может подтвердить подпись и содержание выданной копии.",
    },
  },
  {
    id: "connect-camera",
    section: "administrator",
    codes: [
      "camera_selection_expired",
      "camera_already_registered",
      "camera_binding_conflict",
      "camera_tls_error",
      "camera_transport_denied",
      "camera_authentication_failed",
      "camera_incompatible_profiles",
      "camera_discovery_unavailable",
    ],
    en: {
      before:
        "In Cameras, sign in as a site administrator with the camera details and permission to replace the device if required.",
      diagnosis: [
        {
          symptom: "Discovery succeeds but recording is unconfirmed.",
          action:
            "Check the main profile separately from the live profile. Send the administrator the failing stage and error code, without stream addresses or camera credentials.",
        },
      ],
      title: "Connect a camera or replace a device",
      summary:
        "Choose two stream profiles and verify a newly recorded frame. Keep the point history when replacing a camera.",
      keywords: [
        "commission",
        "discovery",
        "ONVIF",
        "RTSP",
        "credentials",
        "certificate",
        "duplicate",
        "main and secondary",
      ],
      steps: [
        "In Cameras, search the connected device catalog before adding a camera. For a new device open Connection and choose ONVIF discovery or Manual. If discovery expired, run it again.",
        "Choose the recording and live profiles and select Check both streams. For an authentication error, have the administrator verify the camera account. For a trust error, ask the administrator to verify the camera certificate and name before continuing.",
        "For a replacement select the existing observation point in the ONVIF replacement workflow. Verify the new device and its profiles, apply the change and wait for the recorder to confirm it.",
        "Choose Create a test recording, then Open the recording. Confirm the frame after you see the new recording. After replacement also open the previous recording at the same point.",
      ],
      result:
        "The point shows a new archive frame. A replacement leaves earlier recordings available on the existing point.",
    },
    ru: {
      before:
        "Откройте «Камеры» с правами администратора площадки. Подготовьте данные подключения и согласуйте замену устройства, если она нужна.",
      diagnosis: [
        {
          symptom: "Камера найдена, но запись не подтверждена.",
          action:
            "Проверьте основной профиль отдельно от профиля эфира. Передайте администратору этап сбоя и код ошибки без адресов потоков и реквизитов камеры.",
        },
      ],
      title: "Подключить или заменить камеру",
      summary:
        "Выберите два профиля потока и проверьте новый архивный кадр. При замене сохраните историю точки.",
      keywords: [
        "ввод в эксплуатацию",
        "обнаружение",
        "ONVIF",
        "RTSP",
        "реквизиты",
        "сертификат",
        "дубликат",
        "основной и дополнительный поток",
      ],
      steps: [
        "В разделе «Камеры» сначала найдите устройство в каталоге подключённых. Для нового устройства в «Подключение» выберите поиск ONVIF или ручной ввод. Если результат поиска устарел, повторите поиск.",
        "Выберите профили записи и эфира и нажмите «Проверить два потока». При ошибке входа проверьте учётную запись камеры. При ошибке доверия проверьте сертификат камеры и имя, на которое он выдан, до продолжения подключения.",
        "При замене выберите существующую точку в разделе поиска ONVIF и замены камеры. Проверьте новое устройство и его профили, примените изменение и дождитесь подтверждения рекордера.",
        "Нажмите «Создать тестовую запись», затем «Открыть запись». Подтвердите кадр после появления нового изображения. При замене откройте также предыдущую запись той же точки.",
      ],
      result:
        "В архиве точки виден новый кадр. После замены на той же точке открываются и прежние записи.",
    },
  },
  {
    id: "access-privacy",
    section: "administrator",
    codes: [
      "forbidden",
      "grant_conflict",
      "account_conflict",
      "group_conflict",
      "identity_conflict",
      "last_administrator",
    ],
    en: {
      before:
        "Open Access as a site administrator. Identify the account, point or group, actions and recording interval covered by the request.",
      diagnosis: [
        {
          symptom: "The user can view a camera but cannot export.",
          action:
            "Check both viewing and export grants for the entire requested interval and the permitted representation. Report the action and interval without attaching the material.",
        },
      ],
      title: "Set viewing permissions and privacy masks",
      summary:
        "Grant access to the right points and intervals, and check what the operator can see or export.",
      keywords: [
        "access denied",
        "hidden camera",
        "black picture",
        "masking",
        "sound permissions",
        "point group",
        "blocked",
      ],
      steps: [
        "In Access open Users and check the user's site role and account status. An account or group with the same name may already exist. Check existing entries and keep at least one active site administrator. If a corporate identity is linked elsewhere, verify the intended account before changing its binding.",
        "Open Permissions and select the user and point or group. Check the action and time range before granting access. Export needs viewing and export permissions for the entire interval. Sound and PTZ have separate permissions.",
        "In Privacy select the point and output to protect. Set the mask regions and choose Apply the policy. A protected output can be fully blocked when a masked image is unavailable.",
        "Verify the result using an account with the intended role. If a policy changed while an image was open, reopen the view to see the current representation.",
      ],
      result:
        "The operator can open the permitted points and times. Restricted regions and outputs stay protected in the specified views.",
    },
    ru: {
      before:
        "Откройте «Доступ» как администратор площадки. Уточните пользователя, точку или группу, разрешённые действия и интервал записи.",
      diagnosis: [
        {
          symptom: "Пользователь видит камеру, но не может выгрузить запись.",
          action:
            "Проверьте права просмотра и выдачи на весь запрошенный интервал и разрешённый вид копии. Укажите действие и интервал, не прикладывая материал.",
        },
      ],
      title: "Настроить права просмотра и маски изображения",
      summary:
        "Разрешите нужные точки и интервалы, проверьте, что оператор видит и может выдать.",
      keywords: [
        "отказ в доступе",
        "скрытая камера",
        "чёрное изображение",
        "маскирование",
        "права на звук",
        "группа точек",
        "заблокировано",
      ],
      steps: [
        "В разделе «Доступ» откройте «Пользователи» и проверьте роль и состояние учётной записи. Если имя занято, сверьте существующие записи пользователей и групп. Сохраните хотя бы одного действующего администратора. Если корпоративная учётная запись уже привязана, проверьте, какому пользователю она принадлежит, до изменения привязки.",
        "Откройте «Разрешения», выберите пользователя и точку или группу. Перед выдачей проверьте действие и интервал. Для экспорта нужны права просмотра и экспорта на весь интервал. Звук и PTZ требуют отдельных прав.",
        "Во вкладке «Приватность» выберите точку и вид выдачи. Задайте области маски и примените политику. При недоступности маскированного изображения выдача может быть полностью закрыта.",
        "Проверьте результат с учётной записью нужной роли. Если политика изменилась во время просмотра, откройте изображение заново.",
      ],
      result:
        "Оператор открывает разрешённые точки и интервалы. Защищённые области и виды выдачи закрыты в выбранных режимах просмотра.",
    },
  },
  {
    id: "video-rules",
    section: "administrator",
    codes: [],
    en: {
      before:
        "Open Cameras as a site administrator. Choose a configured point with an archive interval containing an example event.",
      diagnosis: [
        {
          symptom: "A trial finds no event.",
          action:
            "Verify the archive coverage, source, region and active hours on a known example before changing thresholds. Report the trial interval and its stated coverage.",
        },
      ],
      title: "Configure a video rule",
      summary:
        "Choose which changes at a camera trigger a recording or an alarm and check the rule against recorded footage.",
      keywords: [
        "video rules",
        "motion",
        "zones",
        "schedule",
        "alarm at the post",
      ],
      steps: [
        "In Cameras, open Video rules and select the point. Enable the rule and choose its source.",
        "For local detection, set the change threshold and the zones to include or exclude. Choose Record on event or Alarm at the post as needed.",
        "Review the schedule and time zone. Under Check against the recording, choose a recorded interval and run the check. Review the matches and any notice about incomplete footage.",
        "Select Save the rule and verify the saved revision.",
      ],
      result:
        "The selected point shows the saved rule revision and the configured actions.",
    },
    ru: {
      before:
        "Откройте «Камеры» как администратор площадки. Выберите подключённую точку с архивным интервалом, на котором виден пример события.",
      diagnosis: [
        {
          symptom: "Проверка на записи не нашла событие.",
          action:
            "На известном примере сверьте покрытие архива, источник, область и расписание перед изменением порогов. Для разбора укажите интервал проверки и показанное покрытие.",
        },
      ],
      title: "Настроить видеоправило",
      summary:
        "Выберите, какие изменения в кадре запускают запись или тревогу, и проверьте правило на архивной записи.",
      keywords: [
        "видеоправила",
        "движение",
        "зоны",
        "расписание",
        "тревога на посту",
      ],
      steps: [
        "В разделе «Камеры» откройте «Видеоправила» и выберите точку. Включите правило и выберите источник.",
        "Для локальной детекции задайте порог изменения и зоны включения или исключения. При необходимости включите «Запись по событию» или «Тревога на посту».",
        "Сверьте расписание и часовой пояс. В разделе «Проверка на записи» выберите записанный интервал и нажмите «Проверить на записи». Просмотрите срабатывания и сообщение о неполной записи, если оно появилось.",
        "Нажмите «Сохранить правило» и проверьте номер сохранённой версии.",
      ],
      result:
        "У выбранной точки отображается сохранённая версия правила с заданными действиями.",
    },
  },
  {
    id: "recording-policy",
    section: "administrator",
    codes: ["recording_policy_managed", "manual_recording_disabled"],
    en: {
      before:
        "As a site administrator open Cameras and select Recording and storage. Determine whether the change should apply to one camera or a group.",
      diagnosis: [
        {
          symptom: "Manual recording is disabled.",
          action:
            "Check the effective policy and the operator's recording permission. If the policy comes from a group, review the effect on other cameras before changing it.",
        },
      ],
      title: "Set recording schedules and retention",
      summary:
        "Check which policy applies, when video should be recorded and how much space is reserved.",
      keywords: [
        "continuous recording",
        "manual recording",
        "motion",
        "schedule",
        "time zone",
        "retention",
        "disk space",
      ],
      steps: [
        "In Cameras open Recording and storage. Choose a camera and inspect its active modes, Settings source and recording depth.",
        "If the source is a group, open the group to change the shared policy or choose Configure separately for this camera. Check the affected group members before saving.",
        "Select the required modes. For a weekly schedule set the local days, times and time zone. For motion recording set the time before and after the event. Save and check the effective policy.",
        "On Volume check available space, reserve and retention. Shortening retention can remove older recordings. Verify the first and last available times and play a new interval inside the schedule.",
      ],
      result:
        "The selected camera uses the intended policy and a frame from the scheduled or event interval plays in Archive.",
    },
    ru: {
      before:
        "С правами администратора площадки откройте «Камеры» и «Запись и хранение». Определите, относится изменение к одной камере или ко всей группе.",
      diagnosis: [
        {
          symptom: "Ручная запись недоступна.",
          action:
            "Сверьте действующую политику и право оператора на запись. Если настройки наследует группа, проверьте влияние на остальные камеры до изменения.",
        },
      ],
      title: "Задать расписание записи и срок хранения",
      summary:
        "Проверьте, какая политика действует, когда должна идти запись и сколько места остаётся в резерве.",
      keywords: [
        "непрерывная запись",
        "ручная запись",
        "движение",
        "расписание",
        "часовой пояс",
        "глубина архива",
        "место на диске",
      ],
      steps: [
        "В разделе «Камеры» откройте «Запись и хранение». Выберите камеру, проверьте действующие режимы, источник настроек и глубину архива.",
        "При наследовании от группы откройте группу для изменения общей политики или нажмите «Настроить отдельно» для этой камеры. Перед сохранением общей политики сверьте состав группы.",
        "Выберите нужные режимы. Для недельного расписания задайте местные дни, время и часовой пояс. Для записи по движению укажите интервалы до и после события. Сохраните и проверьте действующую политику.",
        "Во вкладке «Том» сверьте доступное место, резерв и срок хранения. Сокращение срока может удалить старые записи. Проверьте границы доступной записи и воспроизведите новый интервал внутри расписания.",
      ],
      result:
        "У камеры действует выбранная политика. В «Архиве» открывается кадр за интервал расписания или события.",
    },
  },
  {
    id: "install-site",
    section: "administrator",
    codes: [],
    en: {
      before:
        "Start at the target node with the person authorized to install software. You need the installation kit for that system, its trusted signing key and dedicated archive storage.",
      diagnosis: [
        {
          symptom: "Kit verification or HTTPS trust fails.",
          action:
            "Stop before setup. Ask the supplier or node administrator to verify the kit, independent signing key or configured site certificate. Report the failed check without disclosing keys.",
        },
      ],
      title: "Prepare an offline installation",
      summary:
        "Check the kit, the archive volume and the first sign-in before handing the site to an operator.",
      keywords: [
        "installation",
        "first setup",
        "signed kit",
        "offline",
        "server",
        "security key",
        "certificate",
      ],
      steps: [
        "Obtain the signed kit for the node's operating system and architecture. Verify the signing key through an independent trusted channel and follow the installation procedure supplied with that kit.",
        "Have the node administrator prepare a dedicated archive volume, the site name and its trusted certificate. Verify the node's clock and that workstations can reach the site on the local network.",
        "The node administrator installs the kit and performs initial setup at the local console. At the site address, sign in as the initial administrator, enter the enrollment code when prompted and register the security key.",
        "Connect one camera and check its first archive frame. Sign in with an operator account and verify that it sees the expected point.",
      ],
      result:
        "The local site opens over trusted HTTPS, the administrator can sign in and an operator can view a recorded frame.",
    },
    ru: {
      before:
        "Начните на целевом узле вместе с сотрудником, уполномоченным устанавливать ПО. Нужны комплект для этой системы, доверенный ключ подписи и выделенное хранилище архива.",
      diagnosis: [
        {
          symptom: "Не прошла проверка комплекта или доверия HTTPS.",
          action:
            "Остановитесь до настройки. Попросите поставщика или администратора узла сверить комплект, независимый ключ подписи или сертификат площадки. Укажите этап проверки без передачи ключей.",
        },
      ],
      title: "Подготовить автономную установку",
      summary:
        "Проверьте комплект, том архива и первый вход перед передачей площадки оператору.",
      keywords: [
        "установка",
        "первый запуск",
        "подписанный комплект",
        "без интернета",
        "сервер",
        "ключ безопасности",
        "сертификат",
      ],
      steps: [
        "Получите подписанный комплект для ОС и архитектуры узла. Сверьте ключ подписи по независимому доверенному каналу и используйте процедуру установки из этого комплекта.",
        "Администратор узла готовит выделенный том архива, имя площадки и доверенный сертификат. Проверьте часы узла и доступность площадки для рабочих станций по локальной сети.",
        "Администратор узла устанавливает комплект и проводит начальную настройку в локальной консоли. Откройте адрес площадки, войдите как первый администратор, по запросу введите код регистрации и зарегистрируйте ключ безопасности.",
        "Подключите одну камеру и проверьте первый архивный кадр. Войдите под учётной записью оператора и проверьте видимость нужной точки.",
      ],
      result:
        "Площадка открывается по доверенному HTTPS, администратор может войти, а оператор видит записанный кадр.",
    },
  },
  {
    id: "update-restore",
    section: "administrator",
    codes: [],
    en: {
      before:
        "The authorized node administrator works from the signed kit. Have an independent backup and the release information for the installed node.",
      diagnosis: [
        {
          symptom: "The upgrade was interrupted.",
          action:
            "Have the node administrator follow the signed kit's resume procedure. Record the kit version, failed stage and time. Keep recovery keys and backup files on their protected media.",
        },
      ],
      title: "Verify an upgrade, rollback or recovery",
      summary:
        "Prepare a recovery point and make sure access, recordings and camera bindings work after maintenance.",
      keywords: [
        "upgrade",
        "update",
        "rollback",
        "restore",
        "backup",
        "system disk",
        "interrupted migration",
      ],
      steps: [
        "Before maintenance, schedule the interruption and retain the current and new signed kits. Have the node administrator verify the kits and create a protected backup on independent storage. Preserve archive media and the recovery key separately as specified in the kit.",
        "Use the maintenance procedure for the selected kit. After an interrupted update, use its documented resume procedure. For rollback, select the package version corresponding to the backup.",
        "After recovery, follow the local administrator enrollment procedure if prompted. Check the restored user's permissions, camera bindings and recording policies. Review permission changes made since the backup.",
        "Open an older recording and one made after service resumed. Verify the point and times and check that an operator's access matches the expected scope.",
      ],
      result:
        "The expected product version is running. Both archive intervals play and access checks match the restored configuration.",
    },
    ru: {
      before:
        "Работает администратор узла с подписанным комплектом. Подготовьте независимую резервную копию и сведения о выпуске, установленном на узле.",
      diagnosis: [
        {
          symptom: "Обновление прервалось.",
          action:
            "Администратор узла продолжает операцию по инструкции подписанного комплекта. Запишите версию, этап и время сбоя. Ключи восстановления и резервные копии оставьте на защищённых носителях.",
        },
      ],
      title: "Проверить обновление, откат или восстановление",
      summary:
        "Подготовьте точку восстановления и проверьте доступ, привязки камер и запись после обслуживания.",
      keywords: [
        "обновление",
        "откат",
        "восстановление",
        "резервная копия",
        "системный диск",
        "прерванная миграция",
      ],
      steps: [
        "Назначьте окно обслуживания и сохраните текущий и новый подписанные комплекты. Администратор узла проверяет комплекты и создаёт защищённую копию на независимом носителе. Сохраните архивные носители и отдельно ключ восстановления согласно инструкции комплекта.",
        "Используйте процедуру обслуживания выбранного комплекта. При прерывании обновления продолжите его по инструкции. Для отката выберите версию пакета, соответствующую сохранённой копии.",
        "После восстановления по запросу пройдите локальную регистрацию ключа администратора. Сверьте права пользователя, привязки камер, политики записи и изменения прав, выполненные после создания копии.",
        "Откройте прежнюю запись и запись, созданную после возобновления работы. Сверьте точку и время, проверьте область доступа оператора.",
      ],
      result:
        "Работает ожидаемая версия продукта, оба интервала воспроизводятся, а права соответствуют восстановленной конфигурации.",
    },
  },
  {
    id: "recording-missing",
    section: "troubleshooting",
    codes: [
      "recording_gap",
      "pending_catalog",
      "media_unavailable",
      "video_source_unavailable",
      "edge_recording_unavailable",
    ],
    en: {
      before:
        "Start in Monitoring with the affected point and the time of the missing recording. An operator can check the archive. The recording settings and volume require an administrator.",
      diagnosis: [
        {
          symptom: "Only live video is available.",
          action:
            "Check the main recording stream first. Then compare the active recording mode at that time with the volume status.",
        },
        {
          symptom: "The volume is at its reserve or unavailable.",
          action:
            "Escalate to the node administrator with the time, displayed free space, reserve and recording state. Keep the active archive intact while capacity is restored.",
        },
        {
          symptom: "New frames play, but an old interval is missing.",
          action:
            "Check the device archive for that gap. Note both UTC bounds if the camera has no copy.",
        },
      ],
      title: "Live video works but the recording is missing",
      summary:
        "Check the recording stream, effective policy and archive volume, then play a frame recorded after the repair.",
      keywords: [
        "live video but no recording",
        "camera online, no recording",
        "camera online",
        "empty archive",
        "missing recording",
        "disk full",
        "archive gap",
      ],
      steps: [
        "In Monitoring note the point, time and recording state. Open Archive at that time. If the catalog is pending, wait and retry. If a gap is reported, note its boundaries.",
        "As administrator, open Cameras and check the main stream used for recording. A working live view may be using the secondary stream. Restore the main stream connection if it is unavailable.",
        "Open Recording and storage, choose the camera and check the active policy. Verify that the schedule, event or manual recording should have been active at that time.",
        "Open Volume and compare free space with the reserve. If the volume is unavailable or at its reserve, have the node administrator restore the volume or capacity using the site's maintenance procedure.",
        "Once recording resumes, open Archive on the same point at a time after the repair and play the newly recorded frame. If the earlier interval is still missing, check the device archive for a recoverable copy.",
      ],
      result:
        "A new archive frame from after the repair plays on the expected point. Any earlier gap remains visible until a saved copy is recovered.",
    },
    ru: {
      before:
        "Начните в «Мониторинге» с проблемной точки и времени пропуска. Оператор может проверить архив. Настройки записи и том проверяет администратор.",
      diagnosis: [
        {
          symptom: "Доступен только эфир.",
          action:
            "Сначала проверьте основной поток записи. Затем сопоставьте режим записи на нужное время с состоянием тома.",
        },
        {
          symptom: "Том недоступен или достигнут резерв.",
          action:
            "Передайте администратору узла время, показания свободного места и резерва, а также состояние записи. При восстановлении ёмкости сохраните действующий архив.",
        },
        {
          symptom: "Новые кадры открываются, но прежнего интервала нет.",
          action:
            "Проверьте архив устройства за время пропуска. Если копии нет, зафиксируйте обе границы пропуска в UTC.",
        },
      ],
      title: "Камера в сети, запись отсутствует",
      summary:
        "Если эфир работает, проверьте основной поток, политику записи и том. Откройте кадр, записанный после устранения причины.",
      keywords: [
        "камера в сети, запись отсутствует",
        "эфир есть записи нет",
        "камера онлайн",
        "пустой архив",
        "нет записи",
        "диск заполнен",
        "пропуск архива",
      ],
      steps: [
        "В «Мониторинге» отметьте точку, время и состояние записи. Откройте «Архив» на это время. Если запись добавляется в каталог, подождите и повторите. Если показан пропуск, отметьте его границы.",
        "С правами администратора откройте «Камеры» и проверьте основной поток записи. Рабочий эфир может идти по дополнительному потоку. Если основной поток недоступен, восстановите его подключение.",
        "Откройте «Запись и хранение», выберите камеру и проверьте действующую политику. Уточните, должна ли была идти запись по расписанию, событию или ручной команде в нужное время.",
        "Во вкладке «Том» сравните свободное место с резервом. Если том недоступен или достигнут резерв, администратор узла восстанавливает доступность тома или свободное место по процедуре обслуживания площадки.",
        "Когда запись возобновится, откройте «Архив» на той же точке, выберите время после исправления и воспроизведите новый кадр. Для более раннего пропуска проверьте, есть ли копия в архиве устройства.",
      ],
      result:
        "На нужной точке воспроизводится архивный кадр, записанный после исправления. Прежний пропуск остаётся на шкале до восстановления сохранённой копии.",
    },
  },
  {
    id: "ptz-busy",
    section: "troubleshooting",
    codes: ["camera_ptz_busy"],
    en: {
      before:
        "Open the affected PTZ camera in Monitoring under Live. You should have permission to control it.",
      diagnosis: [
        {
          symptom: "Another operator owns the camera.",
          action:
            "Coordinate release with that operator. For an urgent takeover give the administrator the point through an approved channel and the time of the busy indication.",
        },
        {
          symptom: "Control is available but no motion is confirmed.",
          action:
            "Inspect the live image and command log first. Report whether it was rejected or the outcome is unknown.",
        },
      ],
      title: "The PTZ camera is controlled by another operator",
      summary:
        "Check who has control and coordinate a handover before moving the camera.",
      keywords: [
        "PTZ busy",
        "camera in use",
        "control taken",
        "cannot pan",
        "ownership",
        "another operator",
      ],
      steps: [
        "In Monitoring open Live and select the camera. Check the console for the current control owner and the state of the last command.",
        "If another operator is using the camera, coordinate with that operator and wait for control to be released. If the situation requires a takeover, ask the site administrator to review it.",
        "When control is available, give one short movement command, release the control and check the live image. If the outcome is unknown, check the command log before sending another command.",
      ],
      result:
        "The operator with control can move and stop the camera, and the result is visible in the live image.",
    },
    ru: {
      before:
        "В «Мониторинге» откройте «Эфир» нужной PTZ-камеры. У вашей учётной записи должно быть право управления.",
      diagnosis: [
        {
          symptom: "Камерой управляет другой оператор.",
          action:
            "Согласуйте освобождение камеры. Для срочной передачи укажите администратору точку по принятому каналу и время появления признака занятости.",
        },
        {
          symptom: "Управление доступно, движение не подтверждено.",
          action:
            "Сначала проверьте эфир и журнал команд. Укажите, была команда отклонена или её исход неизвестен.",
        },
      ],
      title: "PTZ-камерой управляет другой оператор",
      summary:
        "Проверьте владельца управления и согласуйте передачу камеры, прежде чем её поворачивать.",
      keywords: [
        "PTZ занята",
        "камера занята",
        "управление занято",
        "не поворачивается",
        "владелец управления",
        "другой оператор",
      ],
      steps: [
        "В «Мониторинге» откройте «Эфир» и выберите камеру. На пульте проверьте, кто управляет камерой и в каком состоянии последняя команда.",
        "Если камерой пользуется другой оператор, свяжитесь с ним и дождитесь освобождения управления. Если ситуация требует перехвата, обратитесь к администратору площадки.",
        "После освобождения камеры подайте короткую команду движения, отпустите кнопку и сверьте изображение. При неизвестном исходе проверьте журнал команд перед повтором.",
      ],
      result:
        "Оператор, получивший управление, может повернуть и остановить камеру. Результат виден в эфире.",
    },
  },
  {
    id: "sign-in",
    section: "troubleshooting",
    codes: [
      "unauthenticated",
      "invalid_credentials",
      "enrollment_required",
      "invalid_factor",
      "invalid_identity",
      "identity_unavailable",
      "desktop_code_invalid",
      "rate_limited",
    ],
    en: {
      before:
        "Start from the site's sign-in screen or launch sign-in from the desktop application. Use your own account and its registered factor when required.",
      diagnosis: [
        {
          symptom: "The security key is lost or the provider is unavailable.",
          action:
            "Contact the authorized administrator. Provide the sign-in method and displayed error code. Keep passwords, one-time codes and security keys private.",
        },
      ],
      title: "Sign in or resume an expired session",
      summary:
        "Choose your site's sign-in method and complete a security-key or desktop confirmation when requested.",
      keywords: [
        "password",
        "session expired",
        "corporate account",
        "Keycloak",
        "WebAuthn",
        "security key",
        "desktop sign-in",
      ],
      steps: [
        "Open the trusted site address. The first sign-in opens in English. Select RU beside Bacuri VMS for Russian, or EN to return to English. Use your local account or the corporate sign-in option. If attempts have been limited, wait before trying again.",
        "For administrator sign-in follow the browser's security-key prompt. Initial enrollment also needs the one-time code issued by the node administrator. If the key is lost, contact the administrator responsible for account recovery.",
        "For desktop sign-in, start in the application, check the account in the system browser and approve the request you just started. If the request expired, start a new sign-in from the application.",
        "If the corporate provider is unavailable, tell the site administrator. An authorized local administrator can use the site's local sign-in and their registered security key.",
      ],
      result:
        "The site or desktop application opens with the permissions of the confirmed account.",
    },
    ru: {
      before:
        "Начните на экране входа площадки или запустите вход из настольного приложения. Используйте свою учётную запись и зарегистрированное средство подтверждения, если оно требуется.",
      diagnosis: [
        {
          symptom: "Ключ потерян или провайдер недоступен.",
          action:
            "Обратитесь к уполномоченному администратору, укажите способ входа и код ошибки. Пароль, одноразовые коды и ключ безопасности остаются у вас.",
        },
      ],
      title: "Войти после завершения сеанса",
      summary:
        "Выберите способ входа площадки и при запросе подтвердите вход ключом безопасности или в системном браузере.",
      keywords: [
        "пароль",
        "сеанс истёк",
        "корпоративная учётная запись",
        "Keycloak",
        "WebAuthn",
        "ключ безопасности",
        "вход в приложение",
      ],
      steps: [
        "Откройте доверенный адрес площадки. При первом посещении форма входа открывается на английском. Нажмите RU рядом с Bacuri VMS для перехода на русский или EN для возврата на английский. Войдите с локальной учётной записью или через корпоративный вход. При ограничении числа попыток подождите перед повтором.",
        "При входе администратора следуйте запросу браузера на подтверждение ключом безопасности. Для первичной регистрации нужен одноразовый код от администратора узла. При утрате ключа обратитесь к сотруднику, отвечающему за восстановление учётных записей.",
        "Для входа из настольного клиента начните операцию в приложении, проверьте учётную запись в системном браузере и подтвердите свой запрос. Если время запроса истекло, начните новый вход из приложения.",
        "При недоступности корпоративного провайдера сообщите администратору площадки. Уполномоченный локальный администратор может войти с локальной учётной записью и зарегистрированным ключом.",
      ],
      result:
        "Площадка или настольное приложение открываются с правами подтверждённой учётной записи.",
    },
  },
  {
    id: "connection-lost",
    section: "troubleshooting",
    codes: [
      "connection_lost",
      "dependency_unavailable",
      "authorization_unavailable",
      "internal_error",
      "media_service_unavailable",
      "camera_unavailable",
      "media_session_closed",
    ],
    en: {
      before:
        "Start at the workstation that reported the error. Note the time and whether the site page, live view or archive failed.",
      diagnosis: [
        {
          symptom: "Only one point fails.",
          action:
            "Check its live view and a known archive interval. Report which one works, with the time and error code.",
        },
        {
          symptom: "The whole workstation loses access.",
          action:
            "Check local connectivity and whether other workstations can reach the site. Report the scope through the approved support channel.",
        },
      ],
      title: "The site or camera stopped responding",
      summary:
        "Determine whether the problem affects the workstation, one camera or the whole site, and report useful details.",
      keywords: [
        "offline",
        "disconnected",
        "unreachable",
        "reconnecting",
        "timeout",
        "contact support",
        "technical support",
      ],
      steps: [
        "Check the workstation's connection to the site network. Open the site's normal address in the browser. If prompted, sign in again.",
        "If the site opens, check another permitted point. When only one point fails, note whether its live video, archive or both are affected. Reopen the original view after the connection returns.",
        "If several points or the whole site remain unavailable, contact the site administrator through your approved support channel. Include the time and time zone, screen name, displayed error code and the action you attempted. Keep passwords, tokens, recordings and private network details out of your initial report.",
      ],
      result:
        "The original view resumes, or the administrator has enough information to distinguish a workstation connection problem from a camera or site outage.",
    },
    ru: {
      before:
        "Начните на посту, где появилась ошибка. Отметьте время и уточните, пропала ли сама страница площадки, эфир или архив.",
      diagnosis: [
        {
          symptom: "Сбой у одной точки.",
          action:
            "Проверьте её эфир и известный архивный интервал. Укажите, какой режим работает, время и код ошибки.",
        },
        {
          symptom: "Пост полностью потерял доступ.",
          action:
            "Проверьте подключение и уточните, могут ли другие посты открыть площадку. По каналу поддержки сообщите, какие посты затронуты.",
        },
      ],
      title: "Площадка или камера перестала отвечать",
      summary:
        "Определите, касается ли сбой одного поста, камеры или всей площадки, и передайте нужные сведения администратору.",
      keywords: [
        "нет связи",
        "потеря подключения",
        "недоступна",
        "переподключение",
        "тайм-аут",
        "обратиться в поддержку",
        "техподдержка",
      ],
      steps: [
        "Проверьте подключение поста к сети площадки. Откройте обычный адрес площадки в браузере. Если появился запрос входа, войдите заново.",
        "Если площадка открывается, проверьте другую доступную точку. При сбое одной точки уточните, недоступны ли эфир, архив или оба режима. После восстановления связи откройте исходный просмотр повторно.",
        "Если несколько точек или весь узел остаются недоступны, обратитесь к администратору по принятому каналу поддержки. Укажите время с часовым поясом, название экрана, код ошибки и действие перед сбоем. Пароли, токены, записи и закрытые сетевые сведения в первичном обращении не нужны.",
      ],
      result:
        "Просмотр возобновлён либо у администратора есть сведения, чтобы различить потерю связи поста, отказ камеры и недоступность площадки.",
    },
  },
  {
    id: "camera-audio",
    section: "operator",
    codes: [
      "camera_audio_missing",
      "camera_audio_unavailable",
      "camera_audio_unsupported",
    ],
    en: {
      before:
        "Open Monitoring and a permitted live point with an audio-capable camera. Listening, audio recording and speech transmission have separate permissions.",
      diagnosis: [
        {
          symptom: "Listening works but talking does not.",
          action:
            "Check the camera output, the permission to talk and the browser microphone permission. Report the direction affected without sharing audio.",
        },
      ],
      title: "Listen to a camera or use its intercom",
      summary:
        "Check the audio direction, camera capability and permission for the task.",
      keywords: [
        "no sound",
        "speaker",
        "microphone",
        "intercom",
        "listen",
        "talk",
        "silent recording",
      ],
      steps: [
        "Open the point in Monitoring, choose Live and check the audio controls above the camera console. Listening, recording sound and talking require their own permissions.",
        "Choose Listen to hear the camera. If the camera has no audio input or the format is unsupported, ask the administrator to check the camera's audio profile.",
        "Choose Talk only if this camera has an audio output and you have permission. Allow microphone access in the browser when prompted. Turn off the microphone when finished.",
        "If the channel is temporarily unavailable, reopen it after the connection returns. For a silent archive, check whether sound was recorded and whether you may play it.",
      ],
      result:
        "The intended audio direction works, or you can identify the missing device capability or permission.",
    },
    ru: {
      before:
        "Откройте «Мониторинг» и доступную точку с камерой, поддерживающей звук. Для прослушивания, записи звука и передачи речи нужны отдельные права.",
      diagnosis: [
        {
          symptom: "Прослушивание работает, передача речи недоступна.",
          action:
            "Проверьте аудиовыход камеры, право на передачу речи и доступ браузера к микрофону. Для поддержки укажите проблемное направление без записи разговора.",
        },
      ],
      title: "Включить звук камеры или переговорную связь",
      summary:
        "Проверьте направление звука, возможности камеры и разрешение на нужное действие.",
      keywords: [
        "нет звука",
        "динамик",
        "микрофон",
        "переговорная связь",
        "слушать",
        "говорить",
        "архив без звука",
      ],
      steps: [
        "В «Мониторинге» откройте «Эфир» выбранной точки и проверьте кнопки звука над пультом камеры. Для прослушивания, записи звука и передачи речи нужны отдельные права.",
        "Включите прослушивание. Если у камеры нет входного звукового канала или формат не поддерживается, попросите администратора проверить профиль звука камеры.",
        "Включайте передачу речи, если у камеры есть аудиовыход и вам выдано разрешение. По запросу разрешите браузеру использовать микрофон. После разговора выключите его.",
        "При временной недоступности канала откройте его после восстановления связи. Для архива без звука проверьте, вёлся ли он со звуковой дорожкой и есть ли право на её прослушивание.",
      ],
      result:
        "Работает выбранное направление звука либо установлено, какой возможности камеры или какого права не хватает.",
    },
  },
  {
    id: "map-placement",
    section: "administrator",
    codes: [
      "invalid_georeference",
      "position_from_plan",
      "georeference_changed",
      "camera_position_changed",
    ],
    en: {
      before:
        "Open Maps and plans with administrator rights. Use a plan with recognizable landmarks and a camera already bound to a point.",
      diagnosis: [
        {
          symptom: "The map rejects the reference points.",
          action:
            "Check each paired landmark and the plan orientation. Send the error code and editing step to the map administrator without sending the plan image.",
        },
      ],
      title: "Correct a floor plan or camera position",
      summary:
        "Place a plan on the region map and align a camera's viewing sector with a known landmark.",
      keywords: [
        "georeference",
        "control points",
        "camera sector",
        "bearing",
        "wrong location",
        "calibration",
      ],
      steps: [
        "In Maps and plans select the plan you are editing. Choose distinct reference points that you can identify on both the plan and the map. Match each pair and review the placement before saving.",
        "If the placement changed while you were editing, reload the current plan and check the pairs again. When a camera's map position comes from the plan, edit its position on that plan.",
        "To align a camera sector, select the camera and a recognizable landmark visible in its image. Stop any PTZ movement before saving the calibration. Repeat calibration if the device or its position changed.",
      ],
      result:
        "The plan aligns with its reference points and the camera sector points toward the observed landmark.",
    },
    ru: {
      before:
        "Откройте «Карты и планы» с правами администратора. Нужны план с узнаваемыми ориентирами и камера, привязанная к точке.",
      diagnosis: [
        {
          symptom: "Карта отклоняет опорные точки.",
          action:
            "Сверьте пары ориентиров и ориентацию плана. Передайте администратору карт код и этап настройки без изображения плана.",
        },
      ],
      title: "Исправить положение плана или камеры",
      summary:
        "Разместите план на карте региона и сверьте направление сектора камеры по видимому ориентиру.",
      keywords: [
        "геопривязка",
        "опорные точки",
        "сектор обзора",
        "азимут",
        "неверная позиция",
        "калибровка",
      ],
      steps: [
        "В разделе «Карты и планы» выберите редактируемый план. Найдите разные ориентиры, которые можно уверенно указать и на плане, и на карте. Сопоставьте каждую пару и проверьте размещение до сохранения.",
        "Если привязка изменилась во время редактирования, заново откройте действующий план и сверьте пары. Если положение камеры на карте определяется планом, измените точку на этом плане.",
        "Чтобы настроить сектор, выберите камеру и узнаваемый ориентир, видимый в кадре. Перед сохранением остановите поворот PTZ. После замены или перемещения устройства повторите калибровку.",
      ],
      result:
        "План совпадает с опорными точками, сектор камеры направлен на наблюдаемый ориентир.",
    },
  },
  {
    id: "map-regions",
    section: "troubleshooting",
    codes: [
      "region_busy",
      "source_unavailable",
      "download_unavailable",
      "invalid_catalog",
      "invalid_package",
      "network_unavailable",
      "installation_failed",
      "checksum_mismatch",
      "invalid_range",
      "catalog_unavailable",
      "storage_unavailable",
    ],
    en: {
      before:
        "In Maps and plans, a site administrator opens Install regions and selects the affected map source.",
      diagnosis: [
        {
          symptom: "Integrity or installation failed.",
          action:
            "Obtain a verified file or catalog from the supplier. Send the displayed code, source type and installation stage. A private catalog address is unnecessary.",
        },
      ],
      title: "A map region will not download or install",
      summary:
        "Check the source, package and disk space, or install a region from a trusted local file.",
      keywords: [
        "maps unavailable",
        "map catalog",
        "region download",
        "MBTiles",
        "ZIP",
        "checksum",
        "map disk space",
      ],
      steps: [
        "As a site administrator, open Maps and plans and choose Install regions. Check the selected source and the status of the region. Wait for a running installation or removal to finish.",
        "If the catalog or download is unavailable, check the source with the map supplier. For an isolated site select a trusted ZIP or MBTiles file. If the source cannot resume a download, use a complete local file.",
        "If the checksum differs or the file or catalog is invalid, obtain a fresh verified package from the supplier. If the site cannot save the file, have the node administrator check free space before retrying.",
        "Install the selected region, wait for completion and open Region map. Zoom into the area covered by the package.",
      ],
      result:
        "Map tiles appear for the installed area at its supported zoom levels.",
    },
    ru: {
      before:
        "В «Картах и планах» администратор площадки открывает установку регионов и выбирает проблемный источник.",
      diagnosis: [
        {
          symptom: "Проверка целостности или установка завершилась ошибкой.",
          action:
            "Получите проверенный файл или каталог у поставщика. Передайте код, тип источника и этап установки. Закрытый адрес каталога для этого не нужен.",
        },
      ],
      title: "Регион карты не загружается или не устанавливается",
      summary:
        "Проверьте источник, пакет и место на диске или установите регион из доверенного локального файла.",
      keywords: [
        "карты недоступны",
        "каталог карт",
        "загрузка региона",
        "MBTiles",
        "ZIP",
        "контрольная сумма",
        "место на диске",
      ],
      steps: [
        "С правами администратора площадки откройте «Карты и планы», нажмите «Установить регионы». Проверьте выбранный источник и состояние региона. Дождитесь окончания уже запущенной установки или удаления.",
        "При недоступности каталога или загрузки уточните источник у поставщика карт. На изолированной площадке выберите доверенный локальный ZIP или MBTiles. Если источник не позволяет возобновить загрузку, используйте полный локальный файл.",
        "При несовпадении контрольной суммы, ошибке файла или каталога получите исправный проверенный пакет у поставщика. Если площадка не может сохранить файл, попросите администратора узла проверить свободное место перед повтором.",
        "Установите регион, дождитесь завершения и откройте карту региона. Увеличьте участок, который входит в пакет.",
      ],
      result:
        "В установленном регионе отображаются тайлы карты на масштабах, предусмотренных пакетом.",
    },
  },
  {
    id: "apply-camera-batch",
    section: "administrator",
    codes: [
      "camera_batch_limit",
      "camera_protocol_error",
      "camera_image_unsupported",
      "camera_image_unconfirmed",
    ],
    en: {
      before:
        "As a site administrator, open the connection workflow in Cameras. Identify the devices, channels and target points for the batch.",
      diagnosis: [
        {
          symptom: "Only some channels applied.",
          action:
            "Keep confirmed channels and retry unfinished entries after checking their current profiles. Report the counts, failed stage and code rather than credentials or stream addresses.",
        },
      ],
      title: "Apply a camera configuration to several channels",
      summary:
        "Compare the selected channels with the site template and resolve a partial application.",
      keywords: [
        "batch",
        "brightness",
        "template",
        "firmware",
        "retry unfinished",
        "partial failure",
      ],
      steps: [
        "In Cameras open Connection and choose Batch. Discover the devices, select the channels and check their target points. If the batch exceeds the limit, divide the channels into smaller batches.",
        "Compare configurations. Inspect the streams and the supported brightness range for each channel. Leave brightness empty to preserve the camera setting when a change is unsupported.",
        "Apply the batch and refresh its results. If a device gives an unexpected response or does not confirm its setting, verify the device profile and actual image before retrying the unfinished channels.",
      ],
      result:
        "The result for each selected channel shows whether the recorder applied the revision or which channels still require attention.",
    },
    ru: {
      before:
        "С правами администратора площадки откройте подключение в «Камерах». Определите устройства, каналы и точки назначения для массового ввода.",
      diagnosis: [
        {
          symptom: "Пакет применён частично.",
          action:
            "Сохраните подтверждённые каналы и повторите незавершённые после сверки текущих профилей. Передайте число каналов, этап сбоя и код без реквизитов и адресов потоков.",
        },
      ],
      title: "Применить конфигурацию к нескольким каналам",
      summary:
        "Сравните каналы с шаблоном площадки и определите, что делать при частичном применении.",
      keywords: [
        "пакет",
        "яркость",
        "шаблон",
        "прошивка",
        "повторить незавершённые",
        "частичный отказ",
      ],
      steps: [
        "В разделе «Камеры» откройте подключение и выберите массовый ввод. Найдите устройства, отметьте каналы и сверьте точки назначения. Если превышен предел, разделите каналы на несколько пакетов.",
        "Сравните конфигурации. Для каждого канала сверьте потоки и поддерживаемый диапазон яркости. Оставьте яркость пустой для сохранения текущего значения, если изменение не поддерживается.",
        "Примените пакет и обновите результаты. Если устройство вернуло неожиданный ответ или не подтвердило настройку, проверьте профиль устройства и фактическое изображение перед повтором незавершённых каналов.",
      ],
      result:
        "Для каждого выбранного канала видно, применил ли рекордер редакцию и какие каналы требуют проверки.",
    },
  },
  {
    id: "changes-not-saved",
    section: "troubleshooting",
    codes: [
      "invalid_request",
      "not_found",
      "revision_conflict",
      "precondition_failed",
      "conflict",
    ],
    en: {
      before:
        "Start on the form that reported the failure. You need permission to edit that item on the selected site.",
      diagnosis: [
        {
          symptom: "The item changed or disappeared.",
          action:
            "Reopen it from the current list, verify scope and compare revisions before editing again. Give the administrator the screen, time, operation and displayed code if the problem persists.",
        },
      ],
      title: "A change could not be saved",
      summary:
        "Recover from a conflicting edit, a missing item or a value that the form cannot accept.",
      keywords: [
        "save failed",
        "outdated",
        "revision",
        "changed by another user",
        "invalid value",
        "object missing",
      ],
      steps: [
        "Read the message next to the action. Check required fields and the values or ranges shown by the form.",
        "If the item changed, reopen it from the current list. Compare the saved values with the change you intended. Apply only the values that still need updating.",
        "If the item is missing, check the selected site and your access with the administrator. If a valid change keeps failing, report the screen name, time and displayed error code through the approved support channel.",
      ],
      result:
        "The reopened item shows the intended settings, or the administrator can identify the missing item or access.",
    },
    ru: {
      before:
        "Начните с формы, которая сообщила об ошибке. Для выбранной площадки у вас должно быть право изменять этот объект.",
      diagnosis: [
        {
          symptom: "Объект изменился или исчез.",
          action:
            "Заново откройте его из актуального списка, сверьте права и изменения перед повтором. При повторном сбое передайте администратору экран, время, операцию и показанный код.",
        },
      ],
      title: "Не удалось сохранить изменение",
      summary:
        "Проверьте конфликт правок, недоступность объекта или значение, которое форма не принимает.",
      keywords: [
        "сохранение не удалось",
        "устарело",
        "редакция",
        "изменил другой пользователь",
        "недопустимое значение",
        "объект не найден",
      ],
      steps: [
        "Прочтите сообщение рядом с действием. Проверьте обязательные поля и допустимые значения или диапазоны, указанные в форме.",
        "Если объект изменился, снова откройте его из актуального списка. Сравните сохранённые значения с теми, которые хотели задать. Примените только оставшиеся изменения.",
        "Если объекта нет, сверьте площадку и права с администратором. Если корректное изменение по-прежнему не сохраняется, передайте в поддержку название экрана, время и код ошибки по принятому каналу.",
      ],
      result:
        "При повторном открытии видны нужные значения либо администратор может установить причину недоступности объекта.",
    },
  },
  {
    id: "archive-playback",
    section: "operator",
    codes: [],
    en: {
      title: "Read the archive timeline",
      summary:
        "Distinguish available video, a recording gap and an interval waiting for indexing.",
      before:
        "In Monitoring select a point you may view and open Archive. For an incident, have its UTC time or open the archive from the alarm.",
      keywords: [
        "archive timeline",
        "time scale",
        "missing frames",
        "pending catalog",
        "playback",
        "gap reason",
      ],
      steps: [
        "Select the time or interval on the timeline. From an alarm, first confirm the selected point and time on its card.",
        "Select a recorded part of the timeline to play it. Check that the image and displayed time match the interval under review.",
        "Select a gap to read its bounds and reason. If cataloging is pending, wait and repeat the check. A frame from a nearby recorded interval can help establish what happened before or after the gap.",
        "If you opened the archive from an alarm, use Back to the selected alarm to return to the original card.",
      ],
      result:
        "You can identify which parts of the requested interval have video and report the boundaries and reason of any gap.",
      diagnosis: [
        {
          symptom:
            "The point has live video but the newest archive interval stays empty.",
          action:
            "Record the UTC interval, point and displayed recording state. Ask the administrator to check the main recording stream, policy and volume.",
        },
      ],
    },
    ru: {
      title: "Прочитать шкалу архива",
      summary:
        "Отличите доступную запись, пропуск и интервал, который ещё добавляется в каталог.",
      before:
        "В «Мониторинге» выберите доступную точку и откройте «Архив». Для проверки происшествия подготовьте время UTC или перейдите в архив из тревоги.",
      keywords: [
        "шкала архива",
        "шкала времени",
        "нет кадров",
        "ожидание каталога",
        "воспроизведение",
        "причина пропуска",
      ],
      steps: [
        "Выберите время или интервал на шкале. При переходе из тревоги сначала сверьте точку и время по карточке.",
        "Выберите записанный участок шкалы и воспроизведите его. Сверьте изображение и показанное время с проверяемым интервалом.",
        "Выберите пропуск и прочтите его границы и причину. Если запись добавляется в каталог, подождите и повторите проверку. По ближайшим записанным участкам можно установить, что произошло до или после пропуска.",
        "Если вы открыли архив из тревоги, вернитесь к исходной карточке кнопкой возврата к выбранной тревоге.",
      ],
      result:
        "Вы определили, для каких частей интервала есть видео, и можете указать границы и причину каждого пропуска.",
      diagnosis: [
        {
          symptom:
            "Эфир точки работает, но новый интервал архива остаётся пустым.",
          action:
            "Запишите границы UTC, точку и показанное состояние записи. Попросите администратора проверить основной поток, политику и том.",
        },
      ],
    },
  },
  {
    id: "compare-recordings",
    section: "operator",
    codes: [],
    en: {
      title: "Compare several cameras at the same moment",
      summary:
        "Search across cameras and use a shared timeline to place an exact bookmark.",
      before:
        "In Investigations open Archive search with viewing rights for the relevant points and times.",
      keywords: [
        "synchronized playback",
        "previous frame",
        "next frame",
        "common camera position",
        "bookmark",
        "motion search",
        "receive time",
      ],
      steps: [
        "Select up to four cameras and a search interval. Use All frames for a time search or Motion with Episode filters to specify the area and threshold. Narrow by plan, event or metadata when available.",
        "Choose a result thumbnail. The tracks move to the same moment. Use Synchronized playback, Pause, Previous frame and Next frame to compare the same change across cameras.",
        "Check each track's index and timing status. A gap belongs to that track. Receive time or an incomplete index limits what the timestamps or results establish.",
        "At the required frame choose Add a bookmark and the intervals to the case materials. Open Cases to save the materials and reopen the bookmark.",
      ],
      result:
        "The bookmark identifies the selected time and available intervals. You have accounted for missing video and timing uncertainty on individual tracks.",
      diagnosis: [
        {
          symptom: "One track gives no matches or appears offset.",
          action:
            "Try a known interval covered by the camera and compare the displayed indexing and timing state. Report that point, UTC interval and state if the discrepancy remains.",
        },
      ],
    },
    ru: {
      title: "Сравнить несколько камер в один момент",
      summary:
        "Найдите эпизод на нескольких камерах и сохраните закладку на точном кадре общей шкалы.",
      before:
        "В «Расследованиях» откройте поиск по архиву с правами просмотра нужных точек и интервалов.",
      keywords: [
        "синхронный просмотр",
        "предыдущий кадр",
        "следующий кадр",
        "общее положение камер",
        "закладка",
        "поиск движения",
        "время приёма",
      ],
      steps: [
        "Выберите до четырёх камер и интервал поиска. Для поиска по времени выберите все кадры, для движения задайте область и порог в фильтрах эпизода. При необходимости уточните план, событие или метаданные.",
        "Выберите миниатюру результата. Дорожки перейдут к одному моменту. Используйте синхронный просмотр, паузу и покадровые шаги назад и вперёд, чтобы сравнить одно изменение на разных камерах.",
        "Сверьте состояние индекса и времени каждой дорожки. Пропуск относится к своей дорожке. Время приёма и неполная индексация ограничивают точность выводов по меткам и результатам поиска.",
        "На нужном кадре добавьте закладку и интервалы в материалы дела. Откройте «Дела», чтобы сохранить материалы и снова открыть закладку.",
      ],
      result:
        "Закладка указывает выбранное время и доступные интервалы. Для отдельных камер учтены пропуски и неопределённость времени.",
      diagnosis: [
        {
          symptom:
            "На одной дорожке нет совпадений или время выглядит смещённым.",
          action:
            "Проверьте известный доступный интервал, состояние индекса и привязки времени. Если расхождение сохраняется, передайте точку, границы UTC и показанное состояние.",
        },
      ],
    },
  },
  {
    id: "export-fragment",
    section: "operator",
    codes: [],
    en: {
      title: "Export a fragment from one camera",
      summary:
        "Choose the exact interval and representation, then save the signed package for a recipient.",
      before:
        "In Monitoring open the point's archive, directly or from an alarm. You need viewing and export rights for the entire requested interval.",
      keywords: [
        "export a fragment",
        "prepare the package",
        "save the package",
        "UTC bounds",
        "original",
        "copy with masks",
      ],
      steps: [
        "Open Export a fragment. Enter the start and end UTC. The end is excluded. Check the point, the first frame and the material you intend to release.",
        "Choose an available representation. An original may include decoding context around the requested bounds. Choose a permitted masked copy when the recipient should receive only the selected frames with the applicable masks.",
        "Choose Prepare the package. If the size limit is reached, narrow the interval. Once preparation succeeds, choose Save the package.",
        "Ask the recipient to verify the package before use. Supply the trusted public verification key via the site's independent trusted channel.",
      ],
      result:
        "You have a signed package for the selected point and interval. The recipient can verify the material independently.",
      diagnosis: [
        {
          symptom: "Preparation is denied or a transfer stops.",
          action:
            "Recheck the session, allowed interval, representation and export permission. Report the UTC bounds, failed stage and error code to the administrator through the site's support channel.",
        },
      ],
    },
    ru: {
      title: "Выгрузить фрагмент с одной камеры",
      summary:
        "Выберите точный интервал и вид выдачи, затем сохраните подписанный комплект для получателя.",
      before:
        "В «Мониторинге» откройте архив точки напрямую или из тревоги. Нужны права просмотра и выдачи на весь запрашиваемый интервал.",
      keywords: [
        "экспорт фрагмента",
        "подготовить комплект",
        "сохранить комплект",
        "границы UTC",
        "оригинал",
        "копия с масками",
      ],
      steps: [
        "Раскройте экспорт фрагмента. Укажите начало и конец в UTC. Момент окончания в интервал не входит. Сверьте точку, первый кадр и материал, предназначенный к выдаче.",
        "Выберите доступный вид выдачи. Оригинал может включать соседние кадры для декодирования. Если получателю нужны только выбранные кадры с действующими масками, выберите разрешённую копию с масками.",
        "Нажмите «Подготовить комплект». Если превышен предел размера, сократите интервал. После подготовки нажмите «Сохранить комплект».",
        "Попросите получателя проверить комплект перед использованием. Доверенный открытый ключ для проверки передайте по независимому доверенному каналу площадки.",
      ],
      result:
        "Сохранён подписанный комплект нужной точки и интервала. Получатель может самостоятельно проверить материал.",
      diagnosis: [
        {
          symptom: "Подготовка отклонена или передача прервалась.",
          action:
            "Сверьте сеанс, разрешённый интервал, вид копии и право на выдачу. По каналу поддержки передайте администратору границы UTC, этап и код ошибки.",
        },
      ],
    },
  },
  {
    id: "verify-export",
    section: "operator",
    codes: [],
    en: {
      title: "Verify an exported package offline",
      summary:
        "Check file integrity before viewing a fragment or a multi-camera case and compare the verified material with the request.",
      before:
        "The recipient needs a trusted copy of the Bacuri verification utility, the signed package and the site's public key obtained independently. A site session is unnecessary.",
      keywords: [
        "offline verification",
        "verifier",
        "signature",
        "file integrity",
        "verify export",
        "signed package",
        "recipient",
      ],
      steps: [
        "Keep the original package. Unpack it as an ordinary user into a new, empty directory on the verification workstation.",
        "Run bacuri-vms-verify-export with the independently trusted public key file and the unpacked directory. Save the verified description after a successful result. For a case, the utility also verifies the individual materials.",
        "Compare the point and UTC interval with the request. For a single fragment play the segments in the order listed in the verified description. Check gaps, masks and the presence of permitted sound before handing over the copy.",
      ],
      result:
        "The utility reports a successful verification and the recipient has checked the actual material against the intended release.",
      diagnosis: [
        {
          symptom: "Verification fails or a file is missing.",
          action:
            "Keep the received copy separate. Ask the issuer to check the package and the independently distributed public key. Provide the verification result through the approved channel.",
        },
      ],
    },
    ru: {
      title: "Проверить выданный комплект без сети",
      summary:
        "Проверьте целостность фрагмента или многокамерного дела и сопоставьте подтверждённые материалы с запросом.",
      before:
        "Получателю нужны доверенная утилита проверки Bacuri, подписанный комплект и открытый ключ площадки, полученный независимо. Сеанс на площадке не требуется.",
      keywords: [
        "автономная проверка",
        "утилита проверки",
        "подпись",
        "целостность файлов",
        "проверка экспорта",
        "подписанный комплект",
        "получатель",
      ],
      steps: [
        "Сохраните исходный комплект. На машине проверки распакуйте его без повышенных прав в новый пустой каталог.",
        "Запустите bacuri-vms-verify-export, указав независимо полученный файл доверенного открытого ключа и каталог распаковки. После успешной проверки сохраните подтверждённое описание. Для дела утилита также проверяет каждый материал.",
        "Сверьте точку и интервал UTC с запросом. Для отдельного фрагмента открывайте сегменты в порядке, указанном в проверенном описании. Перед передачей проверьте пропуски, маски и наличие разрешённого звука.",
      ],
      result:
        "Утилита завершила проверку успешно, получатель сверил фактические материалы с запросом на выдачу.",
      diagnosis: [
        {
          symptom: "Проверка завершилась ошибкой или файл отсутствует.",
          action:
            "Сохраните полученную копию отдельно. Попросите отправителя сверить комплект и распространяемый независимым путём ключ. Передайте результат проверки по принятому каналу.",
        },
      ],
    },
  },
  {
    id: "recover-camera-archive",
    section: "administrator",
    codes: [],
    en: {
      title: "Recover a recording gap from the camera",
      summary:
        "Check for an on-device copy after the network connection returns and verify the recovered interval.",
      before:
        "As a site administrator open Cameras and its connected device catalog. The original camera must offer a supported archive and have saved the missing interval.",
      keywords: [
        "backfill",
        "device archive",
        "recover gap",
        "local camera recording",
        "interrupted download",
      ],
      steps: [
        "Expand the relevant camera in the catalog and choose Check the device archive. Compare the detected recordings with the gaps of the current point binding.",
        "Select the gap that overlaps a device recording. Start recovery and check its progress. If a transfer is interrupted, request that gap again. Already published blocks are available on the common timeline.",
        "Open the point's archive and play a recovered frame within both requested bounds. Check the remaining gap indicators and note any uncovered interval.",
      ],
      result:
        "Recovered video appears on the point's timeline. Parts without reliable camera time or stored video stay marked as gaps.",
      diagnosis: [
        {
          symptom: "The device lists no matching recording.",
          action:
            "Check that the same device recorded locally during the outage and has not overwritten that interval. Report UTC bounds, binding and device-archive state to the node administrator.",
        },
      ],
    },
    ru: {
      title: "Догрузить пропуск из архива камеры",
      summary:
        "После восстановления связи найдите копию на устройстве и проверьте восстановленный интервал.",
      before:
        "С правами администратора площадки откройте каталог подключённых устройств в «Камерах». Исходная камера должна предоставлять поддерживаемый архив и сохранить нужный интервал.",
      keywords: [
        "догрузка",
        "архив устройства",
        "восстановить пропуск",
        "запись на камере",
        "прерванная загрузка",
      ],
      steps: [
        "Раскройте нужную камеру в каталоге и нажмите «Проверить архив устройства». Сопоставьте найденные записи с пропусками текущей привязки точки.",
        "Выберите пропуск, который перекрывается записью устройства. Запустите восстановление и следите за состоянием. При прерывании повторите запрос для этого пропуска. Уже опубликованные блоки доступны на общей шкале.",
        "Откройте архив точки и воспроизведите восстановленный кадр внутри запрошенных границ. Посмотрите отметки оставшихся пропусков и зафиксируйте непокрытый интервал.",
      ],
      result:
        "Восстановленная запись видна на шкале точки. Участки без надёжного времени камеры или без сохранённой копии обозначены как пропуски.",
      diagnosis: [
        {
          symptom: "На устройстве нет подходящей записи.",
          action:
            "Уточните, велась ли локальная запись на той же камере при обрыве и сохранился ли интервал. Передайте администратору узла границы UTC, привязку и состояние архива устройства.",
        },
      ],
    },
  },
  {
    id: "users-and-groups",
    section: "administrator",
    codes: [],
    en: {
      title: "Create a user and grant access to a group",
      summary:
        "Set the account role and select the points that a group permission covers.",
      before:
        "Sign in as a site administrator. In Access, identify the user, required actions and intended points before granting access.",
      keywords: [
        "users",
        "roles",
        "groups",
        "operator account",
        "administrator",
        "group membership",
        "permissions",
      ],
      steps: [
        "Open Users, find the person or create their site account. Expand the user to review role and active status. Keep at least one active administrator.",
        "Open Groups. Create or select a resource group, review the included points and save the group. A membership change affects permissions already granted to that group.",
        "In Permissions choose the user and point or group, action and applicable time bounds. Save the permission. Viewing, control, sound, protection and export have separate grants.",
        "Test from an account with the intended role. Verify both a permitted point and a point outside its scope. A role change or blocked account ends its existing sessions.",
      ],
      result:
        "The user who signs in sees the intended points and can perform only the granted actions.",
      diagnosis: [
        {
          symptom:
            "An account, group or permission with that name already exists.",
          action:
            "Refresh the list and inspect the existing item before creating another. Share the action, site role and error code with an authorized administrator without sending credentials.",
        },
      ],
    },
    ru: {
      title: "Создать пользователя и выдать доступ к группе",
      summary:
        "Назначьте роль и выберите точки, на которые распространяется разрешение для группы.",
      before:
        "Войдите как администратор площадки. В «Доступе» уточните пользователя, нужные действия и точки до выдачи разрешения.",
      keywords: [
        "пользователи",
        "роли",
        "группы",
        "учётная запись оператора",
        "администратор",
        "состав группы",
        "разрешения",
      ],
      steps: [
        "В «Пользователях» найдите пользователя или создайте его учётную запись площадки. Раскройте строку, проверьте роль и состояние. Оставьте хотя бы одного активного администратора.",
        "Откройте «Группы». Создайте или выберите группу ресурсов, проверьте входящие точки и сохраните. Изменение состава действует и на ранее выданные разрешения группы.",
        "В «Разрешениях» выберите пользователя, точку или группу, действие и при необходимости границы времени. Сохраните разрешение. Просмотр, управление, звук, защита и выдача требуют соответствующих прав.",
        "Проверьте результат с учётной записью нужной роли на доступной точке и на точке вне области доступа. Изменение роли или блокировка завершает действующие сеансы пользователя.",
      ],
      result:
        "После входа пользователю видны нужные точки и доступны разрешённые действия.",
      diagnosis: [
        {
          symptom: "Такая учётная запись, группа или разрешение уже есть.",
          action:
            "Обновите список и проверьте существующую запись перед созданием новой. Для разбора укажите уполномоченному администратору действие, роль и код ошибки без реквизитов входа.",
        },
      ],
    },
  },
  {
    id: "privacy-masks",
    section: "administrator",
    codes: [],
    en: {
      title: "Mask parts of a camera image",
      summary:
        "Choose protected outputs and verify the image that the recipient will actually receive.",
      before:
        "Open Access with site administrator permissions and identify the point, the regions to cover and the outputs that must be protected.",
      keywords: [
        "privacy mask",
        "privacy region",
        "thumbnails",
        "redaction",
        "blacked out",
        "masked export",
      ],
      steps: [
        "Open Privacy, select the point and choose the relevant outputs. Live video, archive playback, thumbnails and export can have different restrictions.",
        "For live or archive video, add rectangular regions on the frame layout or enter their bounds as percentages. Check the placement and choose Apply the policy.",
        "Open the affected views under the intended permissions. Check each region in live video or archive and inspect any restricted thumbnail or issued copy. Verify audio separately.",
      ],
      result:
        "Protected areas are obscured in the chosen outputs. A fully blocked output cannot disclose the original image.",
      diagnosis: [
        {
          symptom: "The entire image is covered.",
          action:
            "Review the selected output and region settings. A protected view may close completely when conversion is unavailable or the quota is exhausted. Report the affected output and error code through the approved channel without sending an unmasked image.",
        },
      ],
    },
    ru: {
      title: "Закрыть области изображения масками",
      summary:
        "Выберите защищаемые виды выдачи и проверьте фактический кадр для получателя.",
      before:
        "Откройте «Доступ» с правами администратора и определите точку, закрываемые участки и виды выдачи, требующие защиты.",
      keywords: [
        "приватность",
        "область маски",
        "миниатюры",
        "обезличивание",
        "закрыто всё изображение",
        "экспорт с масками",
      ],
      steps: [
        "Откройте «Приватность», выберите точку и нужные виды выдачи. Для эфира, архива, миниатюр и экспорта ограничения могут различаться.",
        "Для эфира или архива добавьте прямоугольные области на схеме кадра либо укажите их границы в процентах. Сверьте расположение и примените политику.",
        "Откройте затронутые виды просмотра с нужными правами. Сверьте каждую область в эфире или архиве, проверьте защищённую миниатюру или выданную копию. Звук проверьте отдельно.",
      ],
      result:
        "Закрытые области скрыты в выбранных видах выдачи. При полной блокировке оригинальное изображение недоступно.",
      diagnosis: [
        {
          symptom: "Закрыто всё изображение.",
          action:
            "Проверьте вид выдачи и настройки областей. При недоступности преобразования или исчерпании квоты защищённый вид может закрыться полностью. Передайте вид выдачи и код ошибки по каналу поддержки без открытого изображения.",
        },
      ],
    },
  },
  {
    id: "node-acceleration",
    section: "administrator",
    codes: [
      "acceleration_probe_required",
      "acceleration_probe_stale",
      "acceleration_device_unavailable",
      "acceleration_budget_exceeded",
    ],
    en: {
      title: "Configure node acceleration",
      summary:
        "Choose which video tasks may use a GPU and see where running tasks are assigned.",
      before:
        "Sign in as the site administrator and open Node. The applied mode stays visible while you edit.",
      keywords: [
        "GPU",
        "CPU",
        "driver",
        "hardware check",
        "encoder sessions",
        "budget",
        "fallback",
      ],
      steps: [
        "Select Manual or Automatic. Leaving CPU only starts a background hardware check. Follow the current stage or cancel the check and continue working.",
        "In Manual, select the tasks to run on a GPU. Compare their measured cost with the available GPU time, encoder sessions and video memory. Clear a selection if the form reports that it exceeds the budget.",
        "In Automatic, read the expected path and its reason for each task. Apply changes. The Running tasks column shows the device, reason and number of active jobs. Multiple jobs of one kind can use different paths.",
      ],
      result:
        "The applied mode confirms the saved policy. A task with no active jobs has no current assignment.",
      diagnosis: [
        {
          symptom: "A running task moved to CPU.",
          action:
            "Read the warning and the reason beside the task. After the device or driver recovers, choose Check again. The scheduler can move work back to the GPU when capacity is available.",
        },
        {
          symptom: "The check stopped or failed.",
          action:
            "The previous settings remain active. Check the media service and retry the hardware check. Choose CPU only to keep processing without an available GPU.",
        },
      ],
    },
    ru: {
      title: "Настроить ускорение узла",
      summary:
        "Выберите задачи для GPU и проверьте, где выполняются работающие задачи.",
      before:
        "Войдите как администратор площадки и откройте «Узел». Во время редактирования действующий режим указан вверху.",
      keywords: [
        "GPU",
        "CPU",
        "драйвер",
        "проверка оборудования",
        "сессии кодера",
        "бюджет",
        "переход на CPU",
      ],
      steps: [
        "Выберите «Вручную» или «Авто». При выходе из режима «Только CPU» запустится фоновая проверка оборудования. Следите за этапами или отмените проверку и продолжайте работу.",
        "В режиме «Вручную» отметьте задачи для GPU. Сопоставьте стоимость задач со свободным временем GPU, сессиями кодера и видеопамятью. Если выбор превышает бюджет, снимите часть флажков.",
        "В режиме «Авто» посмотрите ожидаемый путь каждой задачи и причину выбора. Нажмите «Применить изменения». В столбце «Работающие задачи» показаны устройство, причина и число активных задач. Задачи одного вида могут одновременно выполняться на разных устройствах.",
      ],
      result:
        "Действующий режим подтверждает сохранение настроек. Фактическое устройство появляется, когда запускается работа этого вида.",
      diagnosis: [
        {
          symptom: "Работающая задача перешла на CPU.",
          action:
            "Прочитайте предупреждение и причину в строке задачи. После восстановления устройства или драйвера нажмите «Проверить снова». Планировщик сможет вернуть задачи на GPU при наличии свободного бюджета.",
        },
        {
          symptom: "Проверка отменена или завершилась ошибкой.",
          action:
            "Прежние настройки продолжают действовать. Проверьте доступность медиасервиса и повторите проверку. Если GPU недоступен, для продолжения работы можно применить «Только CPU».",
        },
      ],
    },
  },
  {
    id: "site-diagnostics",
    section: "administrator",
    codes: [],
    en: {
      title: "Diagnose a site or recording outage",
      summary:
        "Compare the scope of the interruption with recording and volume states before escalating.",
      before:
        "Start with the reporting workstation and the approximate incident time. A site administrator can inspect camera and volume state. Node diagnostics require an authorized node administrator.",
      keywords: [
        "site diagnostics",
        "monitoring outage",
        "disk reserve",
        "services",
        "recording stopped",
        "support handover",
      ],
      steps: [
        "In Monitoring, compare live video and a known archived interval for one affected point and a second permitted point. Check whether other workstations see the same problem.",
        "In Cameras open Recording and storage. Check the effective mode, latest frame and volume status. Distinguish an offline main stream, scheduled pause, unavailable volume and exhausted reserve.",
        "If the failure affects several points or the site itself, ask the node administrator to use the diagnostic procedure in the installed kit. Correlate service state and available disk space with the incident time.",
        "After recovery, reopen the site and check one older interval and one new frame. For support provide the time with time zone, affected function and scope, product version and displayed error code through the approved channel.",
      ],
      result:
        "The interruption is localized and recovery is confirmed by actual playback, or support has a minimal reproducible description.",
      diagnosis: [
        {
          symptom: "The volume was disconnected or data integrity is in doubt.",
          action:
            "Keep the original media for recovery and involve the node administrator before modifying it. Share the state and timestamps. Use your approved protected procedure if a diagnostic bundle is needed.",
        },
      ],
    },
    ru: {
      title: "Диагностировать сбой площадки или записи",
      summary:
        "Сопоставьте масштаб сбоя с состоянием записи и тома перед передачей в поддержку.",
      before:
        "Начните с поста, с которого поступило сообщение, и примерного времени сбоя. Администратор площадки проверяет камеры и том, диагностику узла проводит уполномоченный администратор узла.",
      keywords: [
        "диагностика площадки",
        "сбой поста",
        "резерв места",
        "службы",
        "запись остановлена",
        "обращение в поддержку",
      ],
      steps: [
        "В «Мониторинге» сравните эфир и известную запись проблемной точки и ещё одной доступной точки. Уточните у других постов, наблюдают ли они тот же сбой.",
        "В разделе «Камеры» откройте «Запись и хранение». Проверьте действующий режим, последний кадр и состояние тома. Различайте отсутствие основного потока, паузу по расписанию, недоступность тома и достижение резерва.",
        "Если затронуты несколько точек или сама площадка, администратор узла проводит диагностику по инструкции установленного комплекта. Сопоставьте состояние служб и свободное место со временем сбоя.",
        "После восстановления откройте площадку и проверьте прежний интервал и новый кадр. По каналу поддержки передайте время с часовым поясом, проблемную функцию, охват сбоя, версию и показанный код ошибки.",
      ],
      result:
        "Источник сбоя локализован и восстановление подтверждено просмотром, либо у поддержки есть минимальное воспроизводимое описание.",
      diagnosis: [
        {
          symptom: "Том отключался или есть подозрение на повреждение.",
          action:
            "Сохраните исходный носитель для восстановления. До изменений привлеките администратора узла. Передайте состояния и время. Чувствительные диагностические материалы передаются только по специально согласованной защищённой процедуре.",
        },
      ],
    },
  },
  {
    id: "manual-recording",
    section: "operator",
    codes: [],
    en: {
      title: "Start and check a manual recording",
      summary:
        "Start a time-limited recording at a point and verify the new video in its archive.",
      before:
        "Open Live in Monitoring. The point's effective policy must allow manual recording and your account must have viewing and recording rights. A policy with audio also needs the appropriate audio recording permission.",
      keywords: [
        "manual capture",
        "start recording",
        "stop recording",
        "recording until",
        "manual video",
        "record now",
      ],
      steps: [
        "Choose the intended point and check the moving image. In the live controls select Start manual recording for 15 minutes.",
        "Check the displayed end time. To finish earlier, select Stop manual recording. If the state could not be read, use Check the recording state before sending the command again.",
        "After the recorder publishes a block, open Archive on the same point and play a frame inside your recording interval.",
      ],
      result:
        "The displayed recording state changes and a newly recorded frame from the chosen interval plays.",
      diagnosis: [
        {
          symptom: "The control is missing or unavailable.",
          action:
            "Ask the site administrator to check the point's effective recording policy, your permissions and the recorder state. Give the time, selected mode and displayed message.",
        },
      ],
    },
    ru: {
      title: "Запустить и проверить ручную запись",
      summary:
        "Начните ограниченную по времени запись точки и проверьте новое видео в её архиве.",
      before:
        "Откройте «Мониторинг», затем «Эфир». Действующая политика точки должна разрешать ручную запись, а ваша учётная запись иметь права просмотра и записи. При записи со звуком требуется соответствующее разрешение.",
      keywords: [
        "ручная запись",
        "начать запись",
        "остановить запись",
        "запись до",
        "записать сейчас",
      ],
      steps: [
        "Выберите нужную точку и проверьте движущееся изображение. В управлении эфиром нажмите «Начать ручную запись на 15 минут».",
        "Проверьте показанное время окончания. Для досрочного завершения нажмите «Остановить ручную запись». Если состояние прочитать не удалось, выберите «Проверить состояние записи» до повтора команды.",
        "После публикации блока рекордером откройте «Архив» той же точки и воспроизведите кадр внутри интервала ручной записи.",
      ],
      result:
        "На экране меняется состояние записи, из выбранного интервала открывается новый архивный кадр.",
      diagnosis: [
        {
          symptom: "Кнопка отсутствует или недоступна.",
          action:
            "Попросите администратора площадки проверить действующую политику точки, ваши права и состояние рекордера. Передайте время, выбранный режим и видимое сообщение.",
        },
      ],
    },
  },
  {
    id: "activity-journal",
    section: "operator",
    codes: [],
    en: {
      title: "Trace the handling of an alarm",
      summary:
        "Find the recorded actions for one alarm and return to the corresponding archive frame.",
      before:
        "Open the alarm card in Monitoring under an account allowed to inspect its history, or open Activity journal from the main navigation.",
      keywords: [
        "audit",
        "activity journal",
        "alarm history",
        "actor",
        "reason for closing",
        "result",
        "search log",
      ],
      steps: [
        "Choose Open the log of this alarm on the card to limit the journal to that alarm. From the general journal, narrow the results by part of the result or closure reason.",
        "Start the search with Enter or the search button. Expand an entry and check its UTC time, actor, state transition and recorded result.",
        "Use the available archive link to review the frame. Compare the recorded observation with the action sequence before handing over to the next operator.",
      ],
      result:
        "You can identify who took the alarm, which result was recorded and when it was closed.",
      diagnosis: [
        {
          symptom: "You can see an alarm but no handling entry.",
          action:
            "Check the search filters and whether an operator has taken the alarm into work. Its first handling entry is created when it is claimed. Report the UTC time and missing action to the shift supervisor if the history is inconsistent.",
        },
      ],
    },
    ru: {
      title: "Проследить обработку тревоги в журнале",
      summary:
        "Найдите действия по одной тревоге и вернитесь к соответствующему архивному кадру.",
      before:
        "Откройте карточку тревоги в «Мониторинге» под учётной записью с доступом к её истории или выберите «Журнал действий» в главном меню.",
      keywords: [
        "аудит",
        "журнал действий",
        "история тревоги",
        "исполнитель",
        "причина закрытия",
        "результат",
        "поиск по журналу",
      ],
      steps: [
        "Откройте журнал этой тревоги из карточки, чтобы увидеть записи только по ней. В общем журнале уточните поиск по части текста результата или причины закрытия.",
        "Запустите поиск клавишей Enter или кнопкой с лупой. Раскройте запись, проверьте время UTC, исполнителя, переход состояния и сохранённый результат.",
        "По доступной ссылке перейдите к архивному кадру. Сопоставьте наблюдение с последовательностью действий для передачи дежурства.",
      ],
      result:
        "Вы можете определить, кто взял тревогу в работу, какой результат зафиксирован и когда она закрыта.",
      diagnosis: [
        {
          symptom: "Тревога видна, а записи об обработке нет.",
          action:
            "Проверьте фильтры поиска и факт взятия тревоги в работу. Первая запись обработки появляется при её принятии оператором. При расхождении укажите старшему смены время UTC и недостающее действие.",
        },
      ],
    },
  },
];
