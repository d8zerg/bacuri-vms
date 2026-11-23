import type { ComponentProps } from "react";
import { useNavigate, type To } from "react-router";
import { Button } from "../ui-kit/FormControls";

export function RouteButton({
  to,
  ...props
}: Omit<ComponentProps<typeof Button>, "onClick" | "type"> & { to: To }) {
  const navigate = useNavigate();
  return <Button {...props} onClick={() => void navigate(to)} />;
}
