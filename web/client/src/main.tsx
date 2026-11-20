import { createRoot } from "react-dom/client";
import { WorkspaceRouter } from "./app/Workspace";
import { createServices } from "./adapters/services";
const root = document.getElementById("root");
if (!root) throw new Error("Missing application root");
createRoot(root).render(<WorkspaceRouter services={createServices()} />);
