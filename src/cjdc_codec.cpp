#include "cjdc_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace cjdc {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::array<std::uint8_t, 5> kMagic = {'C', 'J', 'D', 'C', '2'};
constexpr std::uint8_t kLayoutCanonical = 0;
constexpr std::uint8_t kLayoutRawFallback = 1;

enum ExtendedMask : std::uint8_t {
    kPlayerOverride = 1 << 0,
    kRunsOverride = 1 << 1,
    kExtrasOverride = 1 << 2,
    kReplacementsPresent = 1 << 3,
    kReviewPresent = 1 << 4,
    kRunsNonBoundary = 1 << 5,
    kExactDeliveryFallback = 1 << 6,
};

enum class DeliveryShape : std::uint8_t {
    kPlain = 0,
    kExtras = 1,
    kWickets = 2,
    kReplacements = 3,
    kReview = 4,
    kReviewWickets = 5,
    kExtrasReplacements = 6,
    kUnknown = 15,
};

enum class JsonTag : std::uint8_t {
    kNull = 0,
    kFalse = 1,
    kTrue = 2,
    kSigned = 3,
    kUnsigned = 4,
    kFloatText = 5,
    kStringRef = 6,
    kArray = 7,
    kObject = 8,
};

struct Writer {
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t value) { bytes.push_back(value); }

    void raw(const void* data, std::size_t size) {
        const auto* ptr = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), ptr, ptr + size);
    }

    void varuint(std::uint64_t value) {
        while (value >= 0x80) {
            u8(static_cast<std::uint8_t>(value | 0x80));
            value >>= 7;
        }
        u8(static_cast<std::uint8_t>(value));
    }

    void sint(std::int64_t value) {
        const auto zigzag = (static_cast<std::uint64_t>(value) << 1) ^
                            static_cast<std::uint64_t>(value >> 63);
        varuint(zigzag);
    }

    void bytes_blob(const std::vector<std::uint8_t>& value) {
        varuint(value.size());
        raw(value.data(), value.size());
    }

    void string_blob(const std::string& value) {
        varuint(value.size());
        raw(value.data(), value.size());
    }
};

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t pos = 0;

    explicit Reader(const std::vector<std::uint8_t>& input) : bytes(input) {}

    std::uint8_t u8() {
        if (pos >= bytes.size()) {
            throw std::runtime_error("unexpected end of file");
        }
        return bytes[pos++];
    }

    void raw(void* out, std::size_t size) {
        if (bytes.size() - pos < size) {
            throw std::runtime_error("unexpected end of file");
        }
        std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                  bytes.begin() + static_cast<std::ptrdiff_t>(pos + size),
                  static_cast<std::uint8_t*>(out));
        pos += size;
    }

    std::uint64_t varuint() {
        std::uint64_t value = 0;
        int shift = 0;
        while (shift <= 63) {
            const auto byte = u8();
            value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
            shift += 7;
        }
        throw std::runtime_error("varint is too large");
    }

    std::int64_t sint() {
        const auto value = varuint();
        return static_cast<std::int64_t>((value >> 1) ^
                                         (~(value & 1) + 1));
    }

    std::vector<std::uint8_t> bytes_blob() {
        const auto size = checked_size(varuint());
        if (bytes.size() - pos < size) {
            throw std::runtime_error("blob exceeds input size");
        }
        std::vector<std::uint8_t> out(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
        return out;
    }

    std::string string_blob() {
        const auto blob = bytes_blob();
        return std::string(blob.begin(), blob.end());
    }

    static std::size_t checked_size(std::uint64_t value) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("size does not fit this platform");
        }
        return static_cast<std::size_t>(value);
    }
};

struct StringTable {
    std::vector<std::string> values;
    std::unordered_map<std::string, std::uint64_t> ids;

    std::uint64_t add(const std::string& value) {
        const auto it = ids.find(value);
        if (it != ids.end()) {
            return it->second;
        }
        const auto id = static_cast<std::uint64_t>(values.size());
        values.push_back(value);
        ids.emplace(value, id);
        return id;
    }

    std::uint64_t id(const std::string& value) const {
        const auto it = ids.find(value);
        if (it == ids.end()) {
            throw std::runtime_error("string missing from table: " + value);
        }
        return it->second;
    }

    const std::string& at(std::uint64_t id) const {
        if (id >= values.size()) {
            throw std::runtime_error("string id out of range");
        }
        return values[static_cast<std::size_t>(id)];
    }
};

struct IdTable {
    std::vector<std::string> names;
    std::unordered_map<std::string, std::uint64_t> ids;

    void add(const std::string& name) {
        if (name.empty() || ids.count(name) != 0) {
            return;
        }
        const auto id = static_cast<std::uint64_t>(names.size());
        names.push_back(name);
        ids.emplace(name, id);
    }

    std::uint64_t id(const std::string& name) const {
        const auto it = ids.find(name);
        if (it == ids.end()) {
            throw std::runtime_error("name missing from id table: " + name);
        }
        return it->second;
    }

    const std::string& at(std::uint64_t id) const {
        if (id >= names.size()) {
            throw std::runtime_error("id table index out of range");
        }
        return names[static_cast<std::size_t>(id)];
    }
};

struct OverPlan {
    int over_number = 0;
    std::size_t delivery_count = 0;
    std::uint64_t start_batter = 0;
    std::uint64_t start_bowler = 0;
    std::uint64_t start_non_striker = 0;
};

struct InningKey {
    std::string key;
    bool is_overs = false;
    Json value;
};

struct InningPlan {
    std::vector<InningKey> keys;
    std::vector<OverPlan> overs;
};

struct WicketSidecar {
    std::size_t ball_index = 0;
    Json wickets = Json::array();
};

struct ExtendedSidecar {
    std::size_t ball_index = 0;
    DeliveryShape shape = DeliveryShape::kPlain;
    std::uint8_t mask = 0;
    std::uint64_t batter = 0;
    std::uint64_t bowler = 0;
    std::uint64_t non_striker = 0;
    int run_batter = 0;
    int run_extras = 0;
    int run_total = 0;
    Json extras = nullptr;
    Json replacements = nullptr;
    Json review = nullptr;
    Json fallback_delivery = nullptr;
};

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open input: " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_binary_file(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output: " + path.string());
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string as_string(const std::vector<std::uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

void collect_strings(const Json& value, StringTable& table) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            table.add(it.key());
            collect_strings(it.value(), table);
        }
        return;
    }
    if (value.is_array()) {
        for (const auto& child : value) {
            collect_strings(child, table);
        }
        return;
    }
    if (value.is_string()) {
        table.add(value.get<std::string>());
        return;
    }
    if (value.is_number_float()) {
        table.add(value.dump());
    }
}

void write_json(Writer& out, const Json& value, const StringTable& strings) {
    if (value.is_null()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kNull));
    } else if (value.is_boolean()) {
        out.u8(static_cast<std::uint8_t>(value.get<bool>() ? JsonTag::kTrue
                                                           : JsonTag::kFalse));
    } else if (value.is_number_integer()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kSigned));
        out.sint(value.get<std::int64_t>());
    } else if (value.is_number_unsigned()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kUnsigned));
        out.varuint(value.get<std::uint64_t>());
    } else if (value.is_number_float()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kFloatText));
        out.varuint(strings.id(value.dump()));
    } else if (value.is_string()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kStringRef));
        out.varuint(strings.id(value.get<std::string>()));
    } else if (value.is_array()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kArray));
        out.varuint(value.size());
        for (const auto& child : value) {
            write_json(out, child, strings);
        }
    } else if (value.is_object()) {
        out.u8(static_cast<std::uint8_t>(JsonTag::kObject));
        out.varuint(value.size());
        for (auto it = value.begin(); it != value.end(); ++it) {
            out.varuint(strings.id(it.key()));
            write_json(out, it.value(), strings);
        }
    } else {
        throw std::runtime_error("unsupported JSON value");
    }
}

Json read_json(Reader& in, const StringTable& strings) {
    const auto tag_pos = in.pos;
    const auto raw_tag = in.u8();
    const auto tag = static_cast<JsonTag>(raw_tag);
    switch (tag) {
        case JsonTag::kNull:
            return nullptr;
        case JsonTag::kFalse:
            return false;
        case JsonTag::kTrue:
            return true;
        case JsonTag::kSigned:
            return in.sint();
        case JsonTag::kUnsigned:
            return in.varuint();
        case JsonTag::kFloatText:
            return Json::parse(strings.at(in.varuint()));
        case JsonTag::kStringRef:
            return strings.at(in.varuint());
        case JsonTag::kArray: {
            Json arr = Json::array();
            const auto count = Reader::checked_size(in.varuint());
            for (std::size_t i = 0; i < count; ++i) {
                arr.push_back(read_json(in, strings));
            }
            return arr;
        }
        case JsonTag::kObject: {
            Json obj = Json::object();
            const auto count = Reader::checked_size(in.varuint());
            for (std::size_t i = 0; i < count; ++i) {
                const auto key_id = in.varuint();
                auto child = read_json(in, strings);
                obj[strings.at(key_id)] = std::move(child);
            }
            return obj;
        }
    }
    throw std::runtime_error("unknown JSON tag " + std::to_string(raw_tag) +
                             " at byte " + std::to_string(tag_pos));
}

int extras_total(const Json& extras) {
    if (!extras.is_object()) {
        return 0;
    }
    int total = 0;
    for (auto it = extras.begin(); it != extras.end(); ++it) {
        if (it.value().is_number_integer() || it.value().is_number_unsigned()) {
            total += it.value().get<int>();
        }
    }
    return total;
}

int run_code(int runs) {
    switch (runs) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
        case 6:
            return 5;
        case 5:
            return 6;
        default:
            return 7;
    }
}

int run_from_code(int code) {
    switch (code) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
        case 5:
            return 6;
        case 6:
            return 5;
        default:
            throw std::runtime_error("run code requires extended sidecar");
    }
}

std::string compact_json(const Json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
}

std::vector<Json> build_extras_codebook(const Json& root) {
    std::map<std::string, std::pair<int, Json>> counts;
    for (const auto& inning : root.at("innings")) {
        for (const auto& over : inning.at("overs")) {
            for (const auto& delivery : over.at("deliveries")) {
                if (!delivery.contains("extras")) {
                    continue;
                }
                const auto& extras = delivery.at("extras");
                const auto key = compact_json(extras);
                auto& slot = counts[key];
                slot.first += 1;
                slot.second = extras;
            }
        }
    }

    std::vector<std::pair<int, Json>> ranked;
    ranked.reserve(counts.size());
    for (const auto& [_, count_and_json] : counts) {
        ranked.push_back(count_and_json);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    std::vector<Json> out;
    for (const auto& [_, value] : ranked) {
        if (out.size() == 6) {
            break;
        }
        out.push_back(value);
    }
    return out;
}

int extras_code(const Json& delivery, const std::vector<Json>& codebook) {
    if (!delivery.contains("extras")) {
        return 0;
    }
    const auto key = compact_json(delivery.at("extras"));
    for (std::size_t i = 0; i < codebook.size(); ++i) {
        if (compact_json(codebook[i]) == key) {
            return static_cast<int>(i) + 1;
        }
    }
    return 7;
}

Json extras_from_code(int code, const std::vector<Json>& codebook) {
    if (code == 0) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(code - 1);
    if (index >= codebook.size()) {
        throw std::runtime_error("extras code outside codebook");
    }
    return codebook[index];
}

void add_people_from_delivery(const Json& delivery, IdTable& persons) {
    persons.add(delivery.at("batter").get<std::string>());
    persons.add(delivery.at("bowler").get<std::string>());
    persons.add(delivery.at("non_striker").get<std::string>());
    if (delivery.contains("wickets")) {
        for (const auto& wicket : delivery.at("wickets")) {
            if (wicket.contains("player_out")) {
                persons.add(wicket.at("player_out").get<std::string>());
            }
            if (wicket.contains("fielders")) {
                for (const auto& fielder : wicket.at("fielders")) {
                    if (fielder.contains("name")) {
                        persons.add(fielder.at("name").get<std::string>());
                    }
                }
            }
        }
    }
    if (delivery.contains("replacements")) {
        for (auto it = delivery.at("replacements").begin();
             it != delivery.at("replacements").end(); ++it) {
            for (const auto& replacement : it.value()) {
                if (replacement.contains("in")) {
                    persons.add(replacement.at("in").get<std::string>());
                }
                if (replacement.contains("out")) {
                    persons.add(replacement.at("out").get<std::string>());
                }
            }
        }
    }
    if (delivery.contains("review")) {
        const auto& review = delivery.at("review");
        if (review.contains("batter")) {
            persons.add(review.at("batter").get<std::string>());
        }
        if (review.contains("umpire")) {
            persons.add(review.at("umpire").get<std::string>());
        }
    }
}

IdTable build_person_table(const Json& root) {
    IdTable persons;
    const auto& info = root.at("info");
    if (info.contains("players")) {
        for (auto team = info.at("players").begin(); team != info.at("players").end(); ++team) {
            for (const auto& player : team.value()) {
                persons.add(player.get<std::string>());
            }
        }
    }
    if (info.contains("registry") && info.at("registry").contains("people")) {
        for (auto it = info.at("registry").at("people").begin();
             it != info.at("registry").at("people").end(); ++it) {
            persons.add(it.key());
        }
    }
    if (info.contains("officials")) {
        for (auto role = info.at("officials").begin();
             role != info.at("officials").end(); ++role) {
            for (const auto& official : role.value()) {
                persons.add(official.get<std::string>());
            }
        }
    }
    for (const auto& inning : root.at("innings")) {
        for (const auto& over : inning.at("overs")) {
            for (const auto& delivery : over.at("deliveries")) {
                add_people_from_delivery(delivery, persons);
            }
        }
    }
    return persons;
}

IdTable build_team_table(const Json& root) {
    IdTable teams;
    const auto& info = root.at("info");
    if (info.contains("teams")) {
        for (const auto& team : info.at("teams")) {
            teams.add(team.get<std::string>());
        }
    }
    if (info.contains("players")) {
        for (auto team = info.at("players").begin(); team != info.at("players").end(); ++team) {
            teams.add(team.key());
        }
    }
    for (const auto& inning : root.at("innings")) {
        teams.add(inning.at("team").get<std::string>());
    }
    return teams;
}

Json make_runs(int batter_runs, int extras_runs) {
    Json runs = Json::object();
    runs["batter"] = batter_runs;
    runs["extras"] = extras_runs;
    runs["total"] = batter_runs + extras_runs;
    return runs;
}

Json make_delivery(const std::string& batter,
                   const std::string& bowler,
                   const std::string& non_striker,
                   int batter_runs,
                   const Json& extras,
                   const Json* wickets) {
    Json delivery = Json::object();
    delivery["batter"] = batter;
    delivery["bowler"] = bowler;
    const int extras_runs = extras.is_null() ? 0 : extras_total(extras);
    if (!extras.is_null()) {
        delivery["extras"] = extras;
    }
    delivery["non_striker"] = non_striker;
    delivery["runs"] = make_runs(batter_runs, extras_runs);
    if (wickets != nullptr) {
        delivery["wickets"] = *wickets;
    }
    return delivery;
}

void advance_state(const Json& delivery, std::string& striker, std::string& non_striker) {
    striker = delivery.at("batter").get<std::string>();
    non_striker = delivery.at("non_striker").get<std::string>();
    if (delivery.at("runs").at("total").get<int>() % 2 != 0) {
        std::swap(striker, non_striker);
    }
}

std::uint8_t make_ball_byte(const Json& delivery,
                            const std::vector<Json>& extras_codebook,
                            bool extended) {
    const int r_code = run_code(delivery.at("runs").at("batter").get<int>());
    const int e_code = extras_code(delivery, extras_codebook);
    const bool wicket = delivery.contains("wickets");
    std::uint8_t byte = 0;
    if (wicket) {
        byte |= 0x80;
    }
    if (extended || r_code == 7 || e_code == 7) {
        byte |= 0x40;
    }
    byte |= static_cast<std::uint8_t>((r_code & 0x07) << 3);
    byte |= static_cast<std::uint8_t>(e_code & 0x07);
    return byte;
}

bool has_wicket_bit(std::uint8_t byte) { return (byte & 0x80) != 0; }
int run_code_from_byte(std::uint8_t byte) { return (byte >> 3) & 0x07; }
int extras_code_from_byte(std::uint8_t byte) { return byte & 0x07; }

void write_id_table(Writer& out, const IdTable& table, const StringTable& strings) {
    out.varuint(table.names.size());
    for (const auto& name : table.names) {
        out.varuint(strings.id(name));
    }
}

IdTable read_id_table(Reader& in, const StringTable& strings) {
    IdTable table;
    const auto count = Reader::checked_size(in.varuint());
    for (std::size_t i = 0; i < count; ++i) {
        table.add(strings.at(in.varuint()));
    }
    return table;
}

void write_string_table(Writer& out, const StringTable& strings) {
    out.varuint(strings.values.size());
    for (const auto& value : strings.values) {
        out.string_blob(value);
    }
}

StringTable read_string_table(Reader& in) {
    StringTable strings;
    const auto count = Reader::checked_size(in.varuint());
    for (std::size_t i = 0; i < count; ++i) {
        strings.add(in.string_blob());
    }
    return strings;
}

void write_u64_fixed(Writer& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.u8(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

std::uint64_t read_u64_fixed(Reader& in) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(in.u8()) << (i * 8);
    }
    return value;
}

bool same_json(const Json& a, const Json& b) {
    return compact_json(a) == compact_json(b);
}

bool has_keys(const Json& value, const std::vector<std::string>& keys) {
    if (!value.is_object() || value.size() != keys.size()) {
        return false;
    }
    std::size_t index = 0;
    for (auto it = value.begin(); it != value.end(); ++it, ++index) {
        if (it.key() != keys[index]) {
            return false;
        }
    }
    return true;
}

DeliveryShape delivery_shape(const Json& delivery) {
    if (has_keys(delivery, {"batter", "bowler", "non_striker", "runs"})) {
        return DeliveryShape::kPlain;
    }
    if (has_keys(delivery, {"batter", "bowler", "extras", "non_striker", "runs"})) {
        return DeliveryShape::kExtras;
    }
    if (has_keys(delivery, {"batter", "bowler", "non_striker", "runs", "wickets"})) {
        return DeliveryShape::kWickets;
    }
    if (has_keys(delivery, {"batter", "bowler", "non_striker", "replacements", "runs"})) {
        return DeliveryShape::kReplacements;
    }
    if (has_keys(delivery, {"batter", "bowler", "non_striker", "review", "runs"})) {
        return DeliveryShape::kReview;
    }
    if (has_keys(delivery, {"batter", "bowler", "non_striker", "review", "runs", "wickets"})) {
        return DeliveryShape::kReviewWickets;
    }
    if (has_keys(delivery, {"batter", "bowler", "extras", "non_striker", "replacements", "runs"})) {
        return DeliveryShape::kExtrasReplacements;
    }
    return DeliveryShape::kUnknown;
}

int wicket_kind_code(const std::string& kind) {
    if (kind == "caught") return 0;
    if (kind == "bowled") return 1;
    if (kind == "lbw") return 2;
    if (kind == "caught and bowled") return 3;
    if (kind == "run out") return 4;
    if (kind == "stumped") return 5;
    if (kind == "hit wicket") return 6;
    return 7;
}

std::string wicket_kind_from_code(int code, const StringTable& strings, Reader& in) {
    switch (code) {
        case 0: return "caught";
        case 1: return "bowled";
        case 2: return "lbw";
        case 3: return "caught and bowled";
        case 4: return "run out";
        case 5: return "stumped";
        case 6: return "hit wicket";
        case 7: return strings.at(in.varuint());
        default: throw std::runtime_error("invalid wicket kind code");
    }
}

bool known_fielder_shape(const Json& fielder) {
    return has_keys(fielder, {"name"}) ||
           has_keys(fielder, {"substitute"}) ||
           has_keys(fielder, {"name", "substitute"});
}

bool known_wicket_shape(const Json& wicket) {
    if (has_keys(wicket, {"player_out", "kind"})) {
        return true;
    }
    if (!has_keys(wicket, {"player_out", "fielders", "kind"})) {
        return false;
    }
    for (const auto& fielder : wicket.at("fielders")) {
        if (!known_fielder_shape(fielder)) {
            return false;
        }
    }
    return true;
}

void write_wickets(Writer& out,
                   const Json& wickets,
                   const IdTable& persons,
                   const StringTable& strings) {
    out.varuint(wickets.size());
    for (const auto& wicket : wickets) {
        const bool known = known_wicket_shape(wicket);
        out.u8(known ? (wicket.contains("fielders") ? 1 : 0) : 15);
        if (!known) {
            write_json(out, wicket, strings);
            continue;
        }

        out.varuint(persons.id(wicket.at("player_out").get<std::string>()));
        if (wicket.contains("fielders")) {
            out.varuint(wicket.at("fielders").size());
            for (const auto& fielder : wicket.at("fielders")) {
                std::uint8_t flags = 0;
                if (fielder.contains("name")) flags |= 1;
                if (fielder.value("substitute", false)) flags |= 2;
                out.u8(flags);
                if ((flags & 1) != 0) {
                    out.varuint(persons.id(fielder.at("name").get<std::string>()));
                }
            }
        }
        const auto kind = wicket.at("kind").get<std::string>();
        const auto kind_code = wicket_kind_code(kind);
        out.u8(static_cast<std::uint8_t>(kind_code));
        if (kind_code == 7) {
            out.varuint(strings.id(kind));
        }
    }
}

Json read_wickets(Reader& in, const IdTable& persons, const StringTable& strings) {
    Json wickets = Json::array();
    const auto wicket_count = Reader::checked_size(in.varuint());
    for (std::size_t i = 0; i < wicket_count; ++i) {
        const auto shape = in.u8();
        if (shape == 15) {
            wickets.push_back(read_json(in, strings));
            continue;
        }
        if (shape != 0 && shape != 1) {
            throw std::runtime_error("invalid wicket shape");
        }

        Json wicket = Json::object();
        wicket["player_out"] = persons.at(in.varuint());
        if (shape == 1) {
            Json fielders = Json::array();
            const auto fielder_count = Reader::checked_size(in.varuint());
            for (std::size_t f = 0; f < fielder_count; ++f) {
                const auto flags = in.u8();
                Json fielder = Json::object();
                if ((flags & 1) != 0) {
                    fielder["name"] = persons.at(in.varuint());
                }
                if ((flags & 2) != 0) {
                    fielder["substitute"] = true;
                }
                fielders.push_back(fielder);
            }
            wicket["fielders"] = fielders;
        }
        const auto kind_code = in.u8();
        wicket["kind"] = wicket_kind_from_code(kind_code, strings, in);
        wickets.push_back(wicket);
    }
    return wickets;
}

void write_wicket_sidecars(Writer& out,
                           const std::vector<WicketSidecar>& sidecars,
                           const IdTable& persons,
                           const StringTable& strings) {
    out.varuint(sidecars.size());
    std::size_t previous = 0;
    for (const auto& sidecar : sidecars) {
        out.varuint(sidecar.ball_index - previous);
        previous = sidecar.ball_index;
        write_wickets(out, sidecar.wickets, persons, strings);
    }
}

std::unordered_map<std::size_t, Json> read_wicket_sidecars(Reader& in,
                                                           const IdTable& persons,
                                                           const StringTable& strings) {
    std::unordered_map<std::size_t, Json> sidecars;
    const auto count = Reader::checked_size(in.varuint());
    std::size_t index = 0;
    for (std::size_t i = 0; i < count; ++i) {
        index += Reader::checked_size(in.varuint());
        sidecars.emplace(index, read_wickets(in, persons, strings));
    }
    return sidecars;
}

ExtendedSidecar build_extended_sidecar(std::size_t ball_index,
                                       const Json& delivery,
                                       const Json& candidate,
                                       const IdTable& persons,
                                       int run_code_value,
                                       int extras_code_value) {
    ExtendedSidecar out;
    out.ball_index = ball_index;
    out.shape = delivery_shape(delivery);

    if (out.shape == DeliveryShape::kUnknown) {
        out.mask = kExactDeliveryFallback;
        out.fallback_delivery = delivery;
        return out;
    }

    const bool player_diff =
        delivery.at("batter") != candidate.at("batter") ||
        delivery.at("bowler") != candidate.at("bowler") ||
        delivery.at("non_striker") != candidate.at("non_striker");
    if (player_diff) {
        out.mask |= kPlayerOverride;
        out.batter = persons.id(delivery.at("batter").get<std::string>());
        out.bowler = persons.id(delivery.at("bowler").get<std::string>());
        out.non_striker = persons.id(delivery.at("non_striker").get<std::string>());
    }

    if (run_code_value == 7 || !same_json(delivery.at("runs"), candidate.at("runs"))) {
        out.mask |= kRunsOverride;
        out.run_batter = delivery.at("runs").at("batter").get<int>();
        out.run_extras = delivery.at("runs").at("extras").get<int>();
        out.run_total = delivery.at("runs").at("total").get<int>();
    }
    if (delivery.at("runs").value("non_boundary", false)) {
        out.mask |= kRunsNonBoundary;
    }

    const bool delivery_has_extras = delivery.contains("extras");
    const bool candidate_has_extras = candidate.contains("extras");
    if (extras_code_value == 7 || delivery_has_extras != candidate_has_extras ||
        (delivery_has_extras && !same_json(delivery.at("extras"), candidate.at("extras")))) {
        out.mask |= kExtrasOverride;
        out.extras = delivery_has_extras ? delivery.at("extras") : Json(nullptr);
    }

    if (delivery.contains("replacements")) {
        out.mask |= kReplacementsPresent;
        out.replacements = delivery.at("replacements");
    }
    if (delivery.contains("review")) {
        out.mask |= kReviewPresent;
        out.review = delivery.at("review");
    }
    return out;
}

void write_extended_sidecars(Writer& out,
                             const std::vector<ExtendedSidecar>& sidecars,
                             const StringTable& strings) {
    out.varuint(sidecars.size());
    std::size_t previous = 0;
    for (const auto& sidecar : sidecars) {
        out.varuint(sidecar.ball_index - previous);
        previous = sidecar.ball_index;
        out.u8(static_cast<std::uint8_t>(sidecar.shape));
        out.u8(sidecar.mask);
        if ((sidecar.mask & kExactDeliveryFallback) != 0) {
            write_json(out, sidecar.fallback_delivery, strings);
            continue;
        }
        if ((sidecar.mask & kPlayerOverride) != 0) {
            out.varuint(sidecar.batter);
            out.varuint(sidecar.bowler);
            out.varuint(sidecar.non_striker);
        }
        if ((sidecar.mask & kRunsOverride) != 0) {
            out.sint(sidecar.run_batter);
            out.sint(sidecar.run_extras);
            out.sint(sidecar.run_total);
        }
        if ((sidecar.mask & kExtrasOverride) != 0) {
            write_json(out, sidecar.extras, strings);
        }
        if ((sidecar.mask & kReplacementsPresent) != 0) {
            write_json(out, sidecar.replacements, strings);
        }
        if ((sidecar.mask & kReviewPresent) != 0) {
            write_json(out, sidecar.review, strings);
        }
    }
}

std::unordered_map<std::size_t, ExtendedSidecar> read_extended_sidecars(
    Reader& in,
    const StringTable& strings) {
    std::unordered_map<std::size_t, ExtendedSidecar> sidecars;
    const auto count = Reader::checked_size(in.varuint());
    std::size_t index = 0;
    for (std::size_t i = 0; i < count; ++i) {
        index += Reader::checked_size(in.varuint());
        ExtendedSidecar sidecar;
        sidecar.ball_index = index;
        sidecar.shape = static_cast<DeliveryShape>(in.u8());
        sidecar.mask = in.u8();
        if ((sidecar.mask & kExactDeliveryFallback) != 0) {
            sidecar.fallback_delivery = read_json(in, strings);
            sidecars.emplace(index, std::move(sidecar));
            continue;
        }
        if ((sidecar.mask & kPlayerOverride) != 0) {
            sidecar.batter = in.varuint();
            sidecar.bowler = in.varuint();
            sidecar.non_striker = in.varuint();
        }
        if ((sidecar.mask & kRunsOverride) != 0) {
            sidecar.run_batter = static_cast<int>(in.sint());
            sidecar.run_extras = static_cast<int>(in.sint());
            sidecar.run_total = static_cast<int>(in.sint());
        }
        if ((sidecar.mask & kExtrasOverride) != 0) {
            sidecar.extras = read_json(in, strings);
        }
        if ((sidecar.mask & kReplacementsPresent) != 0) {
            sidecar.replacements = read_json(in, strings);
        }
        if ((sidecar.mask & kReviewPresent) != 0) {
            sidecar.review = read_json(in, strings);
        }
        sidecars.emplace(index, std::move(sidecar));
    }
    return sidecars;
}

Json make_delivery_for_shape(DeliveryShape shape,
                             const std::string& batter,
                             const std::string& bowler,
                             const std::string& non_striker,
                             const Json& runs,
                             const Json& extras,
                             const Json* wickets,
                             const Json& replacements,
                             const Json& review) {
    Json delivery = Json::object();
    delivery["batter"] = batter;
    delivery["bowler"] = bowler;

    switch (shape) {
        case DeliveryShape::kPlain:
            delivery["non_striker"] = non_striker;
            delivery["runs"] = runs;
            break;
        case DeliveryShape::kExtras:
            delivery["extras"] = extras;
            delivery["non_striker"] = non_striker;
            delivery["runs"] = runs;
            break;
        case DeliveryShape::kWickets:
            delivery["non_striker"] = non_striker;
            delivery["runs"] = runs;
            if (wickets == nullptr) throw std::runtime_error("missing wickets for wicket shape");
            delivery["wickets"] = *wickets;
            break;
        case DeliveryShape::kReplacements:
            delivery["non_striker"] = non_striker;
            delivery["replacements"] = replacements;
            delivery["runs"] = runs;
            break;
        case DeliveryShape::kReview:
            delivery["non_striker"] = non_striker;
            delivery["review"] = review;
            delivery["runs"] = runs;
            break;
        case DeliveryShape::kReviewWickets:
            delivery["non_striker"] = non_striker;
            delivery["review"] = review;
            delivery["runs"] = runs;
            if (wickets == nullptr) throw std::runtime_error("missing wickets for review wicket shape");
            delivery["wickets"] = *wickets;
            break;
        case DeliveryShape::kExtrasReplacements:
            delivery["extras"] = extras;
            delivery["non_striker"] = non_striker;
            delivery["replacements"] = replacements;
            delivery["runs"] = runs;
            break;
        case DeliveryShape::kUnknown:
            throw std::runtime_error("unknown delivery shape needs fallback");
    }
    return delivery;
}

Json apply_extended_sidecar(const ExtendedSidecar& sidecar,
                            std::uint8_t byte,
                            const std::string& predicted_batter,
                            const std::string& predicted_bowler,
                            const std::string& predicted_non_striker,
                            const Json* wickets,
                            const std::vector<Json>& extras_codebook,
                            const IdTable& persons) {
    if ((sidecar.mask & kExactDeliveryFallback) != 0) {
        return sidecar.fallback_delivery;
    }

    const std::string batter = (sidecar.mask & kPlayerOverride) != 0
                                   ? persons.at(sidecar.batter)
                                   : predicted_batter;
    const std::string bowler = (sidecar.mask & kPlayerOverride) != 0
                                   ? persons.at(sidecar.bowler)
                                   : predicted_bowler;
    const std::string non_striker = (sidecar.mask & kPlayerOverride) != 0
                                        ? persons.at(sidecar.non_striker)
                                        : predicted_non_striker;
    const Json extras = (sidecar.mask & kExtrasOverride) != 0
                            ? sidecar.extras
                            : extras_from_code(extras_code_from_byte(byte), extras_codebook);
    const int batter_runs = (sidecar.mask & kRunsOverride) != 0
                                ? sidecar.run_batter
                                : run_from_code(run_code_from_byte(byte));
    const int extras_runs = (sidecar.mask & kRunsOverride) != 0
                                ? sidecar.run_extras
                                : (extras.is_null() ? 0 : extras_total(extras));
    const int total_runs = (sidecar.mask & kRunsOverride) != 0
                               ? sidecar.run_total
                               : batter_runs + extras_runs;

    Json runs = Json::object();
    runs["batter"] = batter_runs;
    runs["extras"] = extras_runs;
    runs["total"] = total_runs;
    if ((sidecar.mask & kRunsNonBoundary) != 0) {
        runs["non_boundary"] = true;
    }
    return make_delivery_for_shape(sidecar.shape, batter, bowler, non_striker, runs,
                                   extras, wickets, sidecar.replacements, sidecar.review);
}

}  // namespace

EncodeStats encode_file(const std::filesystem::path& input_json,
                        const std::filesystem::path& output_cjdc) {
    const auto raw = read_binary_file(input_json);
    const auto root = Json::parse(as_string(raw));
    const auto canonical = root.dump(2);

    if (canonical != as_string(raw)) {
        Writer out;
        out.raw(kMagic.data(), kMagic.size());
        out.u8(kLayoutRawFallback);
        out.varuint(raw.size());
        write_u64_fixed(out, fnv1a64(raw));
        out.bytes_blob(raw);
        write_binary_file(output_cjdc, out.bytes);

        EncodeStats stats;
        stats.original_bytes = raw.size();
        stats.encoded_bytes = out.bytes.size();
        stats.raw_json_fallback = true;
        return stats;
    }

    StringTable strings;
    collect_strings(root, strings);
    const auto persons = build_person_table(root);
    const auto teams = build_team_table(root);
    const auto extras_codebook = build_extras_codebook(root);
    for (const auto& extras : extras_codebook) {
        collect_strings(extras, strings);
    }

    std::vector<InningPlan> innings;
    std::vector<std::uint8_t> ball_stream;
    std::vector<WicketSidecar> wicket_sidecars;
    std::vector<ExtendedSidecar> extended_sidecars;
    std::size_t ball_index = 0;

    for (const auto& inning : root.at("innings")) {
        InningPlan inning_plan;
        for (auto it = inning.begin(); it != inning.end(); ++it) {
            InningKey key;
            key.key = it.key();
            key.is_overs = it.key() == "overs";
            if (!key.is_overs) {
                key.value = it.value();
            }
            inning_plan.keys.push_back(std::move(key));
        }

        for (const auto& over : inning.at("overs")) {
            const auto& deliveries = over.at("deliveries");
            if (deliveries.empty()) {
                throw std::runtime_error("over has no deliveries");
            }

            OverPlan over_plan;
            over_plan.over_number = over.at("over").get<int>();
            over_plan.delivery_count = deliveries.size();
            over_plan.start_batter = persons.id(deliveries.front().at("batter").get<std::string>());
            over_plan.start_bowler = persons.id(deliveries.front().at("bowler").get<std::string>());
            over_plan.start_non_striker =
                persons.id(deliveries.front().at("non_striker").get<std::string>());
            inning_plan.overs.push_back(over_plan);

            std::string striker = deliveries.front().at("batter").get<std::string>();
            std::string bowler = deliveries.front().at("bowler").get<std::string>();
            std::string non_striker = deliveries.front().at("non_striker").get<std::string>();

            for (const auto& delivery : deliveries) {
                const Json* wicket_json = nullptr;
                if (delivery.contains("wickets")) {
                    wicket_sidecars.push_back({ball_index, delivery.at("wickets")});
                    wicket_json = &wicket_sidecars.back().wickets;
                }

                const int e_code = extras_code(delivery, extras_codebook);
                const int r_code = run_code(delivery.at("runs").at("batter").get<int>());
                const Json extras = e_code == 0 ? Json(nullptr)
                                                : (e_code == 7 ? Json(nullptr)
                                                               : extras_codebook[static_cast<std::size_t>(e_code - 1)]);

                const auto candidate = make_delivery(
                    striker, bowler, non_striker,
                    delivery.at("runs").at("batter").get<int>(), extras, wicket_json);
                const bool extended = (r_code == 7 || e_code == 7 ||
                                       !same_json(candidate, delivery));

                const auto byte = make_ball_byte(delivery, extras_codebook, extended);
                ball_stream.push_back(byte);
                if (extended) {
                    extended_sidecars.push_back(build_extended_sidecar(
                        ball_index, delivery, candidate, persons, r_code, e_code));
                }

                advance_state(delivery, striker, non_striker);
                bowler = delivery.at("bowler").get<std::string>();
                ++ball_index;
            }
        }
        innings.push_back(std::move(inning_plan));
    }

    Writer out;
    out.raw(kMagic.data(), kMagic.size());
    out.u8(kLayoutCanonical);
    out.varuint(raw.size());
    write_u64_fixed(out, fnv1a64(raw));

    write_string_table(out, strings);
    write_id_table(out, persons, strings);
    write_id_table(out, teams, strings);

    write_json(out, root.at("meta"), strings);
    write_json(out, root.at("info"), strings);

    out.varuint(extras_codebook.size());
    for (const auto& extras : extras_codebook) {
        write_json(out, extras, strings);
    }

    out.varuint(innings.size());
    for (const auto& inning : innings) {
        out.varuint(inning.keys.size());
        for (const auto& key : inning.keys) {
            out.varuint(strings.id(key.key));
            out.u8(key.is_overs ? 1 : 0);
            if (!key.is_overs) {
                write_json(out, key.value, strings);
            }
        }
        out.varuint(inning.overs.size());
        for (const auto& over : inning.overs) {
            out.varuint(static_cast<std::uint64_t>(over.over_number));
            out.varuint(over.delivery_count);
            out.varuint(over.start_batter);
            out.varuint(over.start_bowler);
            out.varuint(over.start_non_striker);
        }
    }

    out.bytes_blob(ball_stream);

    write_wicket_sidecars(out, wicket_sidecars, persons, strings);
    write_extended_sidecars(out, extended_sidecars, strings);

    write_binary_file(output_cjdc, out.bytes);

    EncodeStats stats;
    stats.original_bytes = raw.size();
    stats.encoded_bytes = out.bytes.size();
    stats.ball_count = ball_stream.size();
    stats.ball_stream_bytes = ball_stream.size();
    stats.string_count = strings.values.size();
    stats.person_count = persons.names.size();
    stats.team_count = teams.names.size();
    stats.wicket_sidecars = wicket_sidecars.size();
    stats.extended_sidecars = extended_sidecars.size();
    for (const auto& sidecar : extended_sidecars) {
        if ((sidecar.mask & kExactDeliveryFallback) != 0) {
            ++stats.exact_delivery_fallbacks;
        }
    }
    return stats;
}

DecodeStats decode_file(const std::filesystem::path& input_cjdc,
                        const std::filesystem::path& output_json) {
    const auto encoded = read_binary_file(input_cjdc);
    Reader in(encoded);

    std::array<std::uint8_t, 5> magic{};
    in.raw(magic.data(), magic.size());
    if (magic != kMagic) {
        throw std::runtime_error("not a CJDC2 file");
    }

    const auto layout_mode = in.u8();
    const auto original_size = Reader::checked_size(in.varuint());
    const auto original_hash = read_u64_fixed(in);
    if (layout_mode == kLayoutRawFallback) {
        const auto raw = in.bytes_blob();
        if (in.pos != encoded.size()) {
            throw std::runtime_error("trailing bytes after CJDC2 raw payload");
        }
        if (raw.size() != original_size || fnv1a64(raw) != original_hash) {
            throw std::runtime_error("raw JSON fallback failed checksum");
        }
        write_binary_file(output_json, raw);

        DecodeStats stats;
        stats.encoded_bytes = encoded.size();
        stats.output_bytes = raw.size();
        return stats;
    }
    if (layout_mode != kLayoutCanonical) {
        throw std::runtime_error("unknown CJDC2 layout mode");
    }

    const auto strings = read_string_table(in);
    const auto persons = read_id_table(in, strings);
    (void)read_id_table(in, strings);

    const auto meta = read_json(in, strings);
    const auto info = read_json(in, strings);

    std::vector<Json> extras_codebook;
    const auto extras_count = Reader::checked_size(in.varuint());
    for (std::size_t i = 0; i < extras_count; ++i) {
        extras_codebook.push_back(read_json(in, strings));
    }

    std::vector<InningPlan> innings;
    const auto inning_count = Reader::checked_size(in.varuint());
    for (std::size_t i = 0; i < inning_count; ++i) {
        InningPlan inning;
        const auto key_count = Reader::checked_size(in.varuint());
        for (std::size_t k = 0; k < key_count; ++k) {
            InningKey key;
            key.key = strings.at(in.varuint());
            key.is_overs = in.u8() != 0;
            if (!key.is_overs) {
                key.value = read_json(in, strings);
            }
            inning.keys.push_back(std::move(key));
        }
        const auto over_count = Reader::checked_size(in.varuint());
        for (std::size_t o = 0; o < over_count; ++o) {
            OverPlan over;
            over.over_number = static_cast<int>(in.varuint());
            over.delivery_count = Reader::checked_size(in.varuint());
            over.start_batter = in.varuint();
            over.start_bowler = in.varuint();
            over.start_non_striker = in.varuint();
            inning.overs.push_back(over);
        }
        innings.push_back(std::move(inning));
    }

    const auto ball_stream = in.bytes_blob();

    const auto wicket_sidecars = read_wicket_sidecars(in, persons, strings);
    const auto extended_sidecars = read_extended_sidecars(in, strings);

    if (in.pos != encoded.size()) {
        throw std::runtime_error("trailing bytes after CJDC2 payload");
    }

    Json root = Json::object();
    root["meta"] = meta;
    root["info"] = info;
    Json innings_json = Json::array();

    std::size_t ball_index = 0;
    for (const auto& inning : innings) {
        Json inning_json = Json::object();

        for (const auto& key : inning.keys) {
            if (!key.is_overs) {
                inning_json[key.key] = key.value;
                continue;
            }

            Json overs_json = Json::array();
            for (const auto& over : inning.overs) {
                Json over_json = Json::object();
                over_json["over"] = over.over_number;
                Json deliveries = Json::array();

                std::string striker = persons.at(over.start_batter);
                std::string bowler = persons.at(over.start_bowler);
                std::string non_striker = persons.at(over.start_non_striker);

                for (std::size_t i = 0; i < over.delivery_count; ++i) {
                    if (ball_index >= ball_stream.size()) {
                        throw std::runtime_error("ball stream ended early");
                    }
                    const auto byte = ball_stream[ball_index];
                    Json delivery;
                    const auto extended = extended_sidecars.find(ball_index);
                    if (extended != extended_sidecars.end()) {
                        const auto wicket = wicket_sidecars.find(ball_index);
                        const Json* wickets = wicket == wicket_sidecars.end()
                                                  ? nullptr
                                                  : &wicket->second;
                        delivery = apply_extended_sidecar(
                            extended->second, byte, striker, bowler, non_striker,
                            wickets, extras_codebook, persons);
                    } else {
                        const int batter_runs = run_from_code(run_code_from_byte(byte));
                        const auto extras = extras_from_code(extras_code_from_byte(byte), extras_codebook);
                        const Json* wickets = nullptr;
                        const auto wicket = wicket_sidecars.find(ball_index);
                        if (has_wicket_bit(byte)) {
                            if (wicket == wicket_sidecars.end()) {
                                throw std::runtime_error("missing wicket sidecar");
                            }
                            wickets = &wicket->second;
                        }
                        delivery = make_delivery(striker, bowler, non_striker,
                                                 batter_runs, extras, wickets);
                    }

                    deliveries.push_back(delivery);
                    advance_state(delivery, striker, non_striker);
                    bowler = delivery.at("bowler").get<std::string>();
                    ++ball_index;
                }

                over_json["deliveries"] = deliveries;
                overs_json.push_back(over_json);
            }
            inning_json["overs"] = overs_json;
        }
        innings_json.push_back(inning_json);
    }
    root["innings"] = innings_json;

    if (ball_index != ball_stream.size()) {
        throw std::runtime_error("unused balls in ball stream");
    }

    const auto output = root.dump(2);
    const std::vector<std::uint8_t> output_bytes(output.begin(), output.end());
    if (output_bytes.size() != original_size || fnv1a64(output_bytes) != original_hash) {
        throw std::runtime_error("decoded JSON failed byte-exact checksum");
    }

    write_text_file(output_json, output);

    DecodeStats stats;
    stats.encoded_bytes = encoded.size();
    stats.output_bytes = output_bytes.size();
    stats.ball_count = ball_stream.size();
    return stats;
}

}  // namespace cjdc
