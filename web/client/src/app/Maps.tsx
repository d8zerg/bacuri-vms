import { useScreenHelp } from "./HelpLink";
import "../i18n/extended";
import { lazy, Suspense } from "react";

import { SubsectionMenu } from "./SubsectionMenu";

import type { Services, Point, Feed } from "../contracts/workspace";

import "./maps.css";

const PlanGeoreference = lazy(() => import("./PlanGeoreference"));
type Props = {
  services: Services;
  site: string;
  admin: boolean;
  points: Point[];
  feed: Feed;
};
import { useMapsState } from "./maps/state";
import { MapSurface } from "./maps/MapSurface";
import { PointPanel } from "./maps/PointPanel";
export default function Maps(props: Props) {
  const m = useMapsState(props);
  const {
    t,
    reading,
    scene,
    failed,
    tell,
    editingPlan,
    setEditingPlan,
    surface,
    previousPlan,
    editPlanButton,
    region,
    plan,
    changeSurface,
    services,
    site,
    admin,
    points,
  } = m;
  useScreenHelp(
    admin && plan && editingPlan === plan.id
      ? "map-placement"
      : "camera-on-map",
  );
  return (
    <>
      {scene && !(admin && plan && editingPlan === plan.id) && (
        <SubsectionMenu
          label={t("maps.sections")}
          items={[
            {
              label: t("map.region"),
              icon: "map",
              active: !surface,
              select: () => changeSurface(""),
            },
            {
              label: t("maps.plans"),
              icon: "layers",
              active: !!surface,
              disabled: !scene.plans.length,
              select: () => {
                const next =
                  plan ??
                  scene.plans.find((p) => p.id === previousPlan.current) ??
                  scene.plans[0];
                if (next) changeSurface(next.id);
              },
            },
          ]}
        />
      )}
      {!scene ? (
        <p role="status">{failed ? t("maps.loadFailed") : t("maps.loading")}</p>
      ) : admin && plan && editingPlan === plan.id ? (
        <Suspense fallback={<p>{t("maps.loadingGeoreference")}</p>}>
          <PlanGeoreference
            key={site + plan.id}
            plan={plan}
            scene={scene}
            initialRegion={region?.id}
            points={points}
            site={site}
            services={services}
            close={(saved) => {
              setEditingPlan("");
              requestAnimationFrame(() => editPlanButton.current?.focus());
              if (saved) {
                reading.reload();
                tell(t("maps.georeferenceUpdated"));
              }
            }}
          />
        </Suspense>
      ) : (
        <div className="ws-maps">
          <MapSurface m={m} scene={scene} />
          <PointPanel m={m} scene={scene} />
        </div>
      )}
    </>
  );
}
