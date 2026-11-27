// Apply the selected client's theme before the first paint.
const choices = ["light", "dark", "system"];
const supplied = new URLSearchParams(location.hash.slice(1)).get("theme");
let theme = supplied;
if (!choices.includes(theme)) {
  try {
    theme = [
      localStorage.getItem("bacuri.vms.help.theme"),
      localStorage.getItem("bacuri.vms.theme"),
    ].find((value) => choices.includes(value));
  } catch {
    // Use the operating system setting when browser storage is unavailable.
  }
}
if (theme === "light" || theme === "dark")
  document.documentElement.dataset.workspaceTheme = theme;
