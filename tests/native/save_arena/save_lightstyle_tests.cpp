#include "server.h"
#include "host_cmd.h"
#include "eiface.h"
#include "zone.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>

server_t sv{};
DLL_FUNCTIONS gEntityInterface{};

namespace {
constexpr int SavedStyleBytes = 64;
constexpr int NegativeIndex = -1;
constexpr int HighestValidIndex = MAX_LIGHTSTYLES - 1;
constexpr int TerminatedStyleCount = 4;
constexpr int InvalidRecordCount = 2;
SAVELIGHTSTYLE records[] = {
    {0, "mmnmmommommnonmmonqnmmo"},
    {1, ""},
    {2, "a"},
    {3, "ab"},
    {NegativeIndex, "abc"},
    {MAX_LIGHTSTYLES, "def"},
    {HighestValidIndex, {}},
};
std::size_t nextRecord = 0;
std::array<std::unique_ptr<char[]>, std::size(records)> allocations;
std::size_t allocated = 0;
int invalidIndices = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

void ReadFields(SAVERESTOREDATA*, const char* name, void* output, TYPEDESCRIPTION* fields, int fieldCount) {
    if (std::strcmp(name, "Save Header") == 0) {
        static_cast<SAVE_HEADER*>(output)->lightStyleCount = static_cast<int>(std::size(records));
        return;
    }

    Check(std::strcmp(name, "LIGHTSTYLE") == 0 && nextRecord < std::size(records), "Unexpected saved field set");
    Check(fieldCount == 2
        && fields[0].fieldType == FIELD_INTEGER && fields[0].fieldSize == 1
        && fields[0].fieldOffset == offsetof(SAVELIGHTSTYLE, index)
        && fields[1].fieldType == FIELD_CHARACTER && fields[1].fieldSize == SavedStyleBytes
        && fields[1].fieldOffset == offsetof(SAVELIGHTSTYLE, style),
        "Saved lightstyle format changed");
    *static_cast<SAVELIGHTSTYLE*>(output) = records[nextRecord++];
}
}

int Q_strlen(const char* text) {
    return static_cast<int>(std::strlen(text));
}

void Q_memcpy(void* output, const void* input, int bytes) {
    std::memcpy(output, input, static_cast<std::size_t>(bytes));
}

void* Hunk_Alloc(int bytes) {
    auto& allocation = allocations.at(allocated++);
    allocation = std::make_unique<char[]>(static_cast<std::size_t>(bytes));
    return allocation.get();
}

void Con_Printf(const char*, ...) {
    ++invalidIndices;
}

#include "save_tables.inc"

int main() {
    auto& last = records[std::size(records) - 1];
    std::memset(last.style, 'z', sizeof(last.style));

    char priorStyle[] = "previous";
    sv.lightstyles[0] = priorStyle;
    gEntityInterface.pfnSaveReadFields = ReadFields;
    SAVERESTOREDATA save{};
    SAVE_HEADER header{};
    ParseSaveTables(&save, &header, 0);
    Check(nextRecord == std::size(records) && allocated == 0 && invalidIndices == 0
        && sv.lightstyles[0] == priorStyle, "Save table read modified global lightstyles");

    nextRecord = 0;
    ParseSaveTables(&save, &header, 1);
    Check(nextRecord == std::size(records) && invalidIndices == InvalidRecordCount
        && allocated == std::size(records) - InvalidRecordCount, "Invalid lightstyle indices were processed");
    for (int index = 0; index < TerminatedStyleCount; ++index) {
        Check(sv.lightstyles[index] && std::strcmp(sv.lightstyles[index], records[index].style) == 0,
            "Saved lightstyle pattern changed");
    }
    Check(sv.lightstyles[HighestValidIndex]
        && sv.lightstyles[HighestValidIndex] == std::string(SavedStyleBytes - 1, 'z'),
        "Maximum-width lightstyle was not terminated correctly");
}
