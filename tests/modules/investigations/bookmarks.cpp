#include "modules/investigations/bookmarks.hpp"
#include <gtest/gtest.h>
namespace bacuri::vms::control::investigations
{
TEST(CaseBookmarks, AcceptsTheStartAndRejectsTheEndOfTheInterval)
{
    const Window span{0, 1000000};
    std::vector<CaseBookmark> marks{{utc_text(span.start), "start"}};
    EXPECT_NO_THROW(validate_bookmarks(marks, span));
    marks.front().at = utc_text(span.end);
    EXPECT_THROW(validate_bookmarks(marks, span), Failure);
}
} // namespace bacuri::vms::control::investigations
