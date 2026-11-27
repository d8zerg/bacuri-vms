try {
  if (localStorage.getItem("bacuri.vms.language") === "ru") {
    document.documentElement.lang = "ru";
  }
  const theme = localStorage.getItem("bacuri.vms.theme");
  if (theme === "light" || theme === "dark") {
    document.documentElement.dataset.workspaceTheme = theme;
  }
} catch {
  // The document starts in English with the system theme when storage is unavailable.
}
