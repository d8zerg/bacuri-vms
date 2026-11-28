#include "catalog_workspace.hpp"
namespace bacuri::vms::client
{
void CatalogWorkspace::setScenario(int value)
{
    if (value < 0 || value > 5 || value == scenario_)
        return;
    scenario_ = value;
    const QVariantList samples{
        QVariantMap{{"id", "north"},
                    {"name", "North terminal / Складской комплекс / Восточная погрузочная галерея, наружный "
                             "периметр, ворота № 14"},
                    {"connection", "online"},
                    {"recording", "current"},
                    {"alarm", true}},
        QVariantMap{
            {"id", "west"}, {"name", "West entrance"}, {"connection", "offline"}, {"recording", "stopped"}},
        QVariantMap{{"id", "yard"}, {"name", "Freight yard"}, {"connection", "online"}, {"recording", "delayed"}}};
    points()->apply(value == 0 || value == 4 ? samples : QVariantList{});
    switch (value) {
    case 1:
        update(Loading);
        break;
    case 3:
        update(Failed, "dependency_unavailable");
        break;
    case 5:
        update(Failed, "forbidden");
        break;
    default:
        update(Ready);
    }
}
} // namespace bacuri::vms::client
