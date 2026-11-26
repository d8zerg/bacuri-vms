#include "storage/postgresql/database.hpp"
#include "modules/maps/operations.hpp"

// This target is linked with access alone. The link must fail on the foreign operation.
int main()
{
    bacuri::vms::postgresql::Database db;
    return bacuri::vms::control::point_on_plan(db, "site", "point", "plan") ? 0 : 1;
}
