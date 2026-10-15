#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>

using AIControlAdapterRecordingSink = void (*)(void*, const char*, const char*, unsigned int,
    unsigned int, const char*, const char*, const char*);

// Only the simulation thread accesses this journal. Producing a record performs
// bounded copies into preallocated memory; readers serialize bounded batches.
struct AIControlAdapterRecordingEvent
{
    std::uint64_t sequence = 0;
    unsigned int tick = 0, frame = 0, taskId = 0, objectId = 0, productionId = 0;
    int playerIndex = -1;
    float x = 0, y = 0;
    bool hasPosition = false, truncated = false;
    char kind[24] = {}, label[48] = {}, reason[96] = {};
    char owner[48] = {}, requestId[64] = {}, state[24] = {};
};

class AIControlAdapterRecordingJournal
{
    static constexpr std::size_t budget = 2u * 1024u * 1024u;
    template<std::size_t N> struct Ring
    {
        std::array<AIControlAdapterRecordingEvent, N> items{};
        std::size_t start = 0, count = 0;
        void push(const AIControlAdapterRecordingEvent& e)
        {
            if (count < N) items[(start + count++) % N] = e;
            else { items[start] = e; start = (start + 1) % N; }
        }
        const AIControlAdapterRecordingEvent& at(std::size_t i) const { return items[(start + i) % N]; }
        void clear() { start = count = 0; }
    };
    Ring<(budget * 3u / 4u) / sizeof(AIControlAdapterRecordingEvent)> normal;
    Ring<(budget / 4u) / sizeof(AIControlAdapterRecordingEvent)> priority;
    Ring<12> recent;

    template<std::size_t N> static bool copy(char (&target)[N], const char* source)
    {
        std::size_t i = 0;
        if (source != nullptr) while (i + 1 < N && source[i] != '\0') { target[i] = source[i]; ++i; }
        const bool truncated = source != nullptr && source[i] != '\0';
        if (truncated && i > 0 && static_cast<unsigned char>(target[i - 1]) >= 0x80)
        {
            while (i > 0 && (static_cast<unsigned char>(target[i - 1]) & 0xc0) == 0x80) --i;
            if (i > 0 && static_cast<unsigned char>(target[i - 1]) >= 0xc0) --i;
        }
        target[i] = '\0';
        return truncated;
    }

public:
    unsigned int generation = 1;
    std::uint64_t nextSequence = 1, overwritten = 0, truncated = 0;
    std::size_t highWater = 0;

    void beginMatch()
    {
        ++generation; normal.clear(); priority.clear(); recent.clear();
        nextSequence = 1; overwritten = truncated = 0; highWater = 0;
    }
    void clearRecent() { recent.clear(); }
    void append(const char* kind, const char* label, const char* reason,
        unsigned int tick, unsigned int frame, int playerIndex, unsigned int taskId = 0,
        unsigned int objectId = 0, const char* owner = "", const char* requestId = "",
        const char* state = "", const float* position = nullptr, bool recentEligible = false, unsigned int productionId = 0)
    {
        AIControlAdapterRecordingEvent e;
        e.sequence = nextSequence++; e.tick = tick; e.frame = frame; e.playerIndex = playerIndex;
        e.taskId = taskId; e.objectId = objectId; e.productionId = productionId;
        e.truncated = copy(e.kind, kind); e.truncated |= copy(e.label, label);
        e.truncated |= copy(e.reason, reason); e.truncated |= copy(e.owner, owner);
        e.truncated |= copy(e.requestId, requestId); e.truncated |= copy(e.state, state);
        if (position != nullptr) { e.hasPosition = true; e.x = position[0]; e.y = position[1]; }
        if (e.truncated) ++truncated;
        const bool important = std::strcmp(e.label, "task_complete") == 0 ||
            std::strcmp(e.label, "task_failed") == 0 || std::strcmp(e.label, "match_end") == 0;
        if (important) { if (priority.count == priority.items.size()) ++overwritten; priority.push(e); }
        else { if (normal.count == normal.items.size()) ++overwritten; normal.push(e); }
        if (recentEligible) recent.push(e); highWater = std::max(highWater, normal.count + priority.count);
    }
    template<typename Visitor> void after(std::uint64_t sequence, std::size_t limit, Visitor visit) const
    {
        std::size_t n = 0, p = 0, emitted = 0;
        while (n < normal.count && normal.at(n).sequence <= sequence) ++n;
        while (p < priority.count && priority.at(p).sequence <= sequence) ++p;
        while (emitted < limit && (n < normal.count || p < priority.count))
        {
            if (p >= priority.count || (n < normal.count && normal.at(n).sequence < priority.at(p).sequence)) visit(normal.at(n++));
            else visit(priority.at(p++));
            ++emitted;
        }
    }
    template<typename Visitor> void recentEvents(Visitor visit) const
    {
        for (std::size_t i = 0; i < recent.count; ++i) visit(recent.at(i));
    }
    bool recentAttack(unsigned int now, unsigned int freshness, float& x, float& y) const
    {
        for (std::size_t i = recent.count; i > 0; --i)
        {
            const auto& e = recent.at(i - 1);
            if (std::strcmp(e.kind, "attack") != 0 || !e.hasPosition || (e.tick != 0u && now - e.tick > freshness)) continue;
            x = e.x; y = e.y; return true;
        }
        return false;
    }
    std::size_t retained() const { return normal.count + priority.count; }
    std::size_t capacity() const { return normal.items.size() + priority.items.size(); }
};

static_assert(sizeof(AIControlAdapterRecordingEvent) < 512, "Recording event exceeds its bounded size");
