#include "modules/investigations/bookmarks.hpp"
#include <gtest/gtest.h>
namespace bacuri::vms::control::investigations
{
TEST(CaseBookmarks, AcceptsTheStartAndRejectsTheEndOfTheInterval)
{
    const Window span{0, 1000000};
    auto marks = Json::array({{{"at", utc_text(span.start)}, {"name", "start"}}});
    EXPECT_NO_THROW(validate_bookmarks(marks, span));
    marks.at(0)["at"] = utc_text(span.end);
    EXPECT_THROW(validate_bookmarks(marks, span), Failure);
}
} // namespace bacuri::vms::control::investigations
