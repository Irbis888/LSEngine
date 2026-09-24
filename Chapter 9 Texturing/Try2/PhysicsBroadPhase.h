#pragma once

#include "PhysicsCommons.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

// Spatial hash for the current physics step. Indices follow the ECS view order.
class PhysicsBroadPhase
{
public:
    static constexpr float CellSize = 2.0f;
    static constexpr size_t MaxCellsPerCollider = 64;

    void Build(const std::vector<AABB>& bounds)
    {
        mGrid.clear();
        mLarge.clear();
        mEntries.clear();
        mEntries.reserve(bounds.size());
        mAabbTests = 0;
        for (const auto& box : bounds)
        {
            const size_t index = mEntries.size();
            mEntries.push_back(MakeEntry(box));
            Insert(index);
        }
    }

    const AABB& Bounds(size_t index) const { return mEntries[index].bounds; }
    size_t CellCount() const { return mGrid.size(); }
    size_t LargeCount() const { return mLarge.size(); }
    size_t AabbTests() const { return mAabbTests; }

    void Update(size_t index, const AABB& bounds)
    {
        Entry replacement = MakeEntry(bounds);
        const Entry& previous = mEntries[index];
        if (previous.large == replacement.large &&
            (previous.large || (previous.first == replacement.first && previous.last == replacement.last)))
        {
            mEntries[index] = replacement;
            return;
        }

        if (previous.large)
            EraseIndex(mLarge, index);
        else
            ForCells(previous, [&](const Cell& cell)
            {
                auto it = mGrid.find(cell);
                EraseIndex(it->second, index);
                if (it->second.empty())
                    mGrid.erase(it);
            });

        mEntries[index] = replacement;
        Insert(index);
    }

    // Sorted, unique overlapping candidates at/after firstIndex. Re-query after
    // moving the source to discover contacts created by positional correction.
    void Query(size_t source, size_t firstIndex, std::vector<size_t>& result)
    {
        result.clear();
        const Entry& entry = mEntries[source];
        if (entry.large)
        {
            for (size_t i = firstIndex; i < mEntries.size(); ++i)
                if (i != source)
                    result.push_back(i);
        }
        else
        {
            auto append = [&](const std::vector<size_t>& indices)
            {
                for (size_t i : indices)
                    if (i >= firstIndex && i != source)
                        result.push_back(i);
            };
            ForCells(entry, [&](const Cell& cell)
            {
                auto it = mGrid.find(cell);
                if (it != mGrid.end())
                    append(it->second);
            });
            // Large floors/walls never occupy the grid; test their actual bounds.
            append(mLarge);
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
        }

        mAabbTests += result.size();
        result.erase(std::remove_if(result.begin(), result.end(), [&](size_t i)
        {
            return !IntersectsAABB(entry.bounds, mEntries[i].bounds);
        }), result.end());
    }

private:
    struct Cell
    {
        int64_t x = 0, y = 0, z = 0;
        bool operator==(const Cell& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct CellHash
    {
        size_t operator()(const Cell& cell) const
        {
            size_t hash = std::hash<int64_t>{}(cell.x);
            for (int64_t coordinate : { cell.y, cell.z })
                hash ^= std::hash<int64_t>{}(coordinate) + size_t{0x9e3779b9u} + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct Entry
    {
        AABB bounds;
        Cell first, last;
        bool large = false;
    };

    static Entry MakeEntry(const AABB& bounds)
    {
        Entry entry;
        entry.bounds = bounds;
        const auto minimum = bounds.Min();
        const auto maximum = bounds.Max();
        int64_t low[3], high[3];
        size_t cellCount = 1;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double lo = std::floor(double(minimum[axis]) / CellSize);
            const double hi = std::floor(double(maximum[axis]) / CellSize);
            // Bound conversion and multiplication before enumerating any cells.
            // Exceptional coordinates conservatively use the separate list too.
            if (!std::isfinite(lo) || !std::isfinite(hi) || hi < lo ||
                lo < (std::numeric_limits<int32_t>::min)() ||
                hi > (std::numeric_limits<int32_t>::max)())
            {
                entry.large = true;
                return entry;
            }
            low[axis] = static_cast<int64_t>(lo);
            high[axis] = static_cast<int64_t>(hi);
            const auto width = static_cast<uint64_t>(high[axis] - low[axis] + 1);
            if (width > MaxCellsPerCollider / cellCount)
            {
                entry.large = true;
                return entry;
            }
            cellCount *= static_cast<size_t>(width);
        }
        entry.first = { low[0], low[1], low[2] };
        entry.last = { high[0], high[1], high[2] };
        return entry;
    }

    template<class Function>
    static void ForCells(const Entry& entry, Function function)
    {
        for (int64_t x = entry.first.x; x <= entry.last.x; ++x)
            for (int64_t y = entry.first.y; y <= entry.last.y; ++y)
                for (int64_t z = entry.first.z; z <= entry.last.z; ++z)
                    function(Cell{ x, y, z });
    }

    static void EraseIndex(std::vector<size_t>& indices, size_t index)
    {
        indices.erase(std::remove(indices.begin(), indices.end(), index), indices.end());
    }

    void Insert(size_t index)
    {
        const Entry& entry = mEntries[index];
        if (entry.large)
            mLarge.push_back(index);
        else
            ForCells(entry, [&](const Cell& cell) { mGrid[cell].push_back(index); });
    }

    std::vector<Entry> mEntries;
    std::unordered_map<Cell, std::vector<size_t>, CellHash> mGrid;
    std::vector<size_t> mLarge;
    size_t mAabbTests = 0;
};

