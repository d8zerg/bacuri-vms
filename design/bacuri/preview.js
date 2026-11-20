const screens = document.querySelectorAll(".screen");
const modes = document.querySelectorAll("[data-mode]");
for (const button of modes) {
  button.addEventListener("click", () => {
    const mode = button.dataset.mode;
    for (const option of modes)
      option.setAttribute("aria-pressed", String(option === button));
    for (const screen of screens)
      screen.hidden = mode !== "both" && screen.dataset.theme !== mode;
    document
      .querySelector(".compare")
      .classList.toggle("single", mode !== "both");
  });
}
for (const screen of screens) {
  const button = screen.querySelector(".cta");
  button.addEventListener("click", () => {
    const archive = screen.querySelector(".kind").textContent === "Эфир";
    screen.querySelector(".kind").textContent = archive ? "Архив" : "Эфир";
    screen
      .querySelector(".ol use")
      .setAttribute("href", archive ? "#i-archive" : "#i-radio");
    screen
      .querySelector(".track")
      .style.setProperty("--cursor", archive ? "55%" : "calc(100% - 2px)");
    screen.querySelector(".or").textContent = archive ? "14:23:18" : "14:26:00";
    screen.querySelector(".context").textContent = archive
      ? "Кадр тревоги · UTC+3"
      : "Архив · UTC+3";
    button.querySelector("span").textContent = archive
      ? "Вернуться в эфир"
      : "Открыть архив";
    button
      .querySelector("use")
      .setAttribute("href", archive ? "#i-radio" : "#i-archive");
  });
}
