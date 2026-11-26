#pragma once
#include "storage/postgresql/database.hpp"
#include <gmock/gmock.h>
#include <optional>

namespace bacuri::vms::postgresql::test
{
// Construct the same libpq result type that production code reads, without opening a connection.
using Cells = std::vector<std::vector<std::optional<std::string>>>;
inline Rows rows(Cells data)
{
    std::unique_ptr<PGresult, decltype(&PQclear)> result{PQmakeEmptyPGresult(nullptr, PGRES_TUPLES_OK), PQclear};
    if (!result)
        throw std::bad_alloc{};
    if (!data.empty()) {
        char name[] = "value";
        std::vector<PGresAttDesc> columns(data.front().size());
        for (auto& column : columns)
            column.name = name;
        if (!PQsetResultAttrs(result.get(), static_cast<int>(columns.size()), columns.data()))
            throw std::runtime_error("Cannot initialize test columns");
        int row = 0;
        for (auto& values : data) {
            if (values.size() != columns.size())
                throw std::logic_error("Inconsistent test row");
            int col = 0;
            for (auto& value : values)
                if (!PQsetvalue(result.get(), row, col++, value ? value->data() : nullptr,
                                value ? static_cast<int>(value->size()) : -1))
                    throw std::runtime_error("Cannot initialize test row");
            ++row;
        }
    }
    return Rows{result.release()};
}

class MockQueries : public QuerySession
{
  public:
    MOCK_METHOD(Rows, exec, (const char*, std::initializer_list<std::string>), (override));
    void expect(const std::string& statement, const std::vector<std::string>& arguments, Cells data = {})
    {
        EXPECT_CALL(*this, exec(testing::HasSubstr(statement), testing::ElementsAreArray(arguments)))
            .WillOnce(testing::Return(rows(std::move(data))));
    }
};
using Queries = testing::StrictMock<MockQueries>;
} // namespace bacuri::vms::postgresql::test
