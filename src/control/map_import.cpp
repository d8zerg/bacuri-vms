#include "control/maps.hpp"
#include <png.h>
#include <sqlite3.h>
#include <charconv>
#include <cmath>

namespace bacuri::vms::control
{
namespace
{
constexpr std::size_t image_limit = 4uz * 1024 * 1024;
void require(bool valid)
{
    if (!valid)
        throw std::invalid_argument("Invalid map package or PNG image");
}

class Statement
{
  public:
    Statement(sqlite3* db, const char* query)
    {
        sqlite3_stmt* raw{};
        require(sqlite3_prepare_v2(db, query, -1, &raw, nullptr) == SQLITE_OK);
        stmt.reset(raw);
    }
    bool next()
    {
        const int result = sqlite3_step(stmt.get());
        require(result == SQLITE_ROW || result == SQLITE_DONE);
        return result == SQLITE_ROW;
    }
    std::string_view bytes(int column) const
    {
        const auto* ptr = static_cast<const char*>(sqlite3_column_blob(stmt.get(), column));
        require(ptr != nullptr);
        return {ptr, static_cast<std::size_t>(sqlite3_column_bytes(stmt.get(), column))};
    }
    enum class TileColumn { zoom, x, tms_y };
    int integer(TileColumn field, int maximum) const
    {
        const auto column = static_cast<int>(field);
        require(sqlite3_column_type(stmt.get(), column) == SQLITE_INTEGER);
        const auto n = sqlite3_column_int64(stmt.get(), column);
        require(n >= 0 && n <= maximum);
        return static_cast<int>(n);
    }

  private:
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt{nullptr, sqlite3_finalize};
};
Json parse_bounds(std::string_view csv)
{
    Json values = Json::array();
    for (int i = 0; i < 4; ++i) {
        double coordinate{};
        const char* first = csv.data();
        const char* end = first + csv.size();
        auto [next, ec] = std::from_chars(first, end, coordinate);
        require(ec == std::errc{} && std::isfinite(coordinate) &&
                std::abs(coordinate) <= (i % 2 ? 85.051129 : 180));
        values.push_back(coordinate);
        if (i == 3)
            require(next == end);
        else {
            require(next != end && *next == ',');
            csv.remove_prefix(next - first + 1);
        }
    }
    require(values[0] < values[2] && values[1] < values[3]);
    return values;
}
} // namespace

MapImage read_map_png(std::string_view bytes, bool tile)
{
    require(bytes.size() >= 33 && bytes.size() <= image_limit &&
            png_sig_cmp(reinterpret_cast<png_const_bytep>(bytes.data()), 0, 8) == 0);
    // Only pass rendering chunks to the decoder. Ancillary text and profiles may be compressed independently.
    std::string clean{bytes.substr(0, 8)};
    bytes.remove_prefix(8);
    bool ended = false;
    while (!bytes.empty()) {
        require(bytes.size() >= 12);
        const auto length = png_get_uint_32(reinterpret_cast<png_const_bytep>(bytes.data()));
        require(length <= bytes.size() - 12);
        const auto type = bytes.substr(4, 4);
        if (type == "IHDR" || type == "PLTE" || type == "tRNS" || type == "IDAT" || type == "IEND")
            clean.append(bytes.substr(0, 12 + length));
        else
            require(type[0] >= 'a' && type[0] <= 'z' && type != "acTL");
        bytes.remove_prefix(12 + length);
        if (type == "IEND") {
            require(length == 0 && bytes.empty());
            ended = true;
        }
    }
    require(ended);
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    const std::unique_ptr<png_image, decltype(&png_image_free)> guard{&image, png_image_free};
    require(png_image_begin_read_from_memory(&image, clean.data(), clean.size()) != 0);
    require(image.width && image.height && image.width <= 4096 && image.height <= 4096 &&
            image.width * image.height <= 8 * 1024 * 1024 &&
            (!tile || (image.width == 256 && image.height == 256)));
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    require(png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) != 0 && !image.warning_or_error);
    return {image.width, image.height, std::move(clean)};
}

void import_map(const std::filesystem::path& file)
{
    require(std::filesystem::is_regular_file(file) &&
            std::filesystem::file_size(file) <= 8ULL * 1024 * 1024 * 1024);
    sqlite3* raw{};
    const auto result = sqlite3_open_v2(file.c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> handle{raw, sqlite3_close};
    require(result == SQLITE_OK &&
            sqlite3_exec(raw, "PRAGMA trusted_schema=OFF; BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_limit(raw, SQLITE_LIMIT_LENGTH, image_limit + 1024);
    Statement metadata(
        raw, "SELECT name,value FROM metadata WHERE name IN ('name','bounds','format','osm_base_timestamp')");
    Json meta = Json::object();
    while (metadata.next()) {
        std::string name{metadata.bytes(0)}, value{metadata.bytes(1)};
        require(value.size() <= 256 && !value.empty() && !meta.contains(name));
        meta[name] = value;
    }
    require(meta.at("format") == "png");
    const auto bounds = parse_bounds(meta.at("bounds").get<std::string>());
    auto stamp = meta.at("osm_base_timestamp").get<std::string>();
    std::unique_ptr<GDateTime, decltype(&g_date_time_unref)> time{
        g_date_time_new_from_iso8601(stamp.c_str(), nullptr), g_date_time_unref};
    require(time != nullptr);
    Database db;
    const auto site = setting("BACURI_SITE_ID");
    db.exec("BEGIN");
    require(db.exec("SELECT FROM sites WHERE site_id=$1 FOR UPDATE", {site}).size() == 1);
    db.exec("DELETE FROM map_regions WHERE site_id=$1", {site});
    db.exec("INSERT INTO map_regions(site_id,name,source_time,bounds,min_zoom,max_zoom) VALUES ($1,$2,$3,$4,0,0)",
            {site, meta.at("name").get<std::string>(), stamp, bounds.dump()});
    Statement tiles(raw, "SELECT zoom_level,tile_column,tile_row,tile_data FROM tiles");
    int min_zoom = 23, max_zoom = -1;
    while (tiles.next()) {
        const int z = tiles.integer(Statement::TileColumn::zoom, 22), last = (1 << z) - 1,
                  x = tiles.integer(Statement::TileColumn::x, last),
                  y = last - tiles.integer(Statement::TileColumn::tms_y, last);
        auto tile = read_map_png(tiles.bytes(3), true);
        std::unique_ptr<char, decltype(&g_free)> encoded{
            g_base64_encode(reinterpret_cast<const guchar*>(tile.png.data()), tile.png.size()), g_free};
        db.exec("INSERT INTO map_tiles VALUES ($1,$2,$3,$4,decode($5,'base64'))",
                {site, std::to_string(z), std::to_string(x), std::to_string(y), encoded.get()});
        min_zoom = std::min(z, min_zoom);
        max_zoom = std::max(z, max_zoom);
    }
    require(max_zoom >= min_zoom);
    db.exec("UPDATE map_regions SET min_zoom=$2,max_zoom=$3 WHERE site_id=$1",
            {site, std::to_string(min_zoom), std::to_string(max_zoom)});
    db.exec("COMMIT");
}
} // namespace bacuri::vms::control
