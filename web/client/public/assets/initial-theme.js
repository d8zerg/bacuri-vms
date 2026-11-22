try {
  const theme = localStorage.getItem("bacuri.vms.theme");
  if (theme === "light" || theme === "dark") {
    document.documentElement.dataset.workspaceTheme = theme;
  }
} catch {
  // The document uses the system theme when storage is unavailable.
}
