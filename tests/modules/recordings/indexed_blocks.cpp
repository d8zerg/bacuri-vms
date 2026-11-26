#include "modules/recordings/operations.hpp"
#include "storage/postgresql/query_mock.hpp"

namespace bacuri::vms::control::recordings
{
TEST(IndexedBlocks, PreservesBoundsClockAccuracyAndMotionSampleOrder)
{
    postgresql::test::Queries db;
    const auto index =
        R"({"samples":[{"at":0,"cells":"first","image":"preview1"},{"at":7,"cells":"second","image":"preview2"}],"frames":[0,3,7]})";
    db.expect("ORDER BY begin_utc_us,block_id LIMIT $5", {"site", "point", "5", "30", "17"},
              {{"0", "10", "arrival", "4", "0", index}, {"10", "25", "source", "5", std::nullopt, std::nullopt}});
    const auto blocks = indexed_blocks(db, "site", "point", 5, 30, 17);
    ASSERT_EQ(blocks.size(), 2u);
    const auto& first = blocks.front();
    EXPECT_EQ(first.begin, 0);
    EXPECT_EQ(first.end, 10);
    EXPECT_EQ(first.basis, "arrival");
    EXPECT_EQ(first.binding, "4");
    EXPECT_EQ(first.uncertainty, 0);
    if (!first.index)
        FAIL() << "The stored motion index is missing";
    EXPECT_THAT(first.index->frames, testing::ElementsAre(0, 3, 7));
    ASSERT_EQ(first.index->samples.size(), 2u);
    EXPECT_EQ(first.index->samples[0].cells, "first");
    EXPECT_EQ(first.index->samples[1].offset, 7);
    EXPECT_EQ(first.index->samples[1].image, "preview2");
    EXPECT_EQ(blocks.back().begin, 10);
    EXPECT_FALSE(blocks.back().uncertainty);
    EXPECT_FALSE(blocks.back().index);
}
TEST(IndexedBlocks, DistinguishesAnAnalyzedBlockWithoutSamplesFromAnAbsentIndex)
{
    postgresql::test::Queries db;
    db.expect("LEFT JOIN bacuri_archive.motion_indexes", {"site", "point", "0", "10", "65"},
              {{"0", "10", "arrival", "1", "15", R"({"samples":[],"frames":[]})"}});
    const auto blocks = indexed_blocks(db, "site", "point", 0, 10, 65);
    ASSERT_EQ(blocks.size(), 1u);
    const auto& index = blocks.front().index;
    if (!index)
        FAIL() << "An empty motion index was discarded";
    EXPECT_TRUE(index->samples.empty());
    EXPECT_TRUE(index->frames.empty());
}
} // namespace bacuri::vms::control::recordings
