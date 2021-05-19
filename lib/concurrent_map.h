
/*------------------------------------------------------------------------
  Junction: Concurrent data structures in C++
  Copyright (c) 2016-2019 Jeff Preshing, Opsian Ltd.

  Distributed under the Simplified BSD License.
  Original location: https://github.com/preshing/junction

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the LICENSE file for more information.
------------------------------------------------------------------------*/
#ifndef OPSIAN_CONCURRENTMAP_H
#define OPSIAN_CONCURRENTMAP_H

// from code.google.com/p/smhasher/wiki/MurmurHash3
inline uint32_t avalanche(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

inline uint32_t deavalanche(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7ed1b41d;
    h ^= (h ^ (h >> 13)) >> 13;
    h *= 0xa5cb9243;
    h ^= h >> 16;
    return h;
}

inline uint64_t avalanche(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return h;
}

inline uint64_t deavalanche(uint64_t h) {
    h ^= h >> 33;
    h *= 0x9cb4b2f8129337db;
    h ^= h >> 33;
    h *= 0x4f74430c22a54005;
    h ^= h >> 33;
    return h;
}

inline bool isPowerOf2(uint64_t v) {
    return (v & (v - 1)) == 0;
}

typedef uint64_t ureg;

template <class T>
struct DefaultKeyTraits {
    typedef T Key;
    typedef ureg Hash;
    static const Key NullKey = Key(0);
    static const Hash NullHash = Hash(0);
    static Hash hash(T key) {
        return avalanche(Hash(key));
    }
    static Key dehash(Hash hash) {
        return (T) deavalanche(hash);
    }
};

template <class T>
struct DefaultValueTraits {
    typedef T Value;
    static const T NullValue = 0;
};

struct NoDebugTrait {
    static void ASSERT(const char* name, const bool val) {
    }

    template <typename T>
    static void LOG(const char* name, const T val) {
    }

    template <typename T>
    static void LOGL(const char* name, const T val) {
    }

    static void LOG(const char* name) {
    }
};

struct DebugTrait {
    static void ASSERT(const char* name, const bool val) {
        if (!val) {
            std::cerr << "INVARIANT FAILED: " << name << std::endl;
        }
    }

    template <typename T>
    static void LOG(const char* name, const T val) {
        std::cout << name << " = " << val << ", ";
    }

    template <typename T>
    static void LOGL(const char* name, const T val) {
        std::cout << name << " = " << val << std::endl;
    }

    static void LOG(const char* name) {
        std::cout << name << std::endl;
    }
};

#define MAP_ASSERT(cond) DebugTraits::ASSERT(#cond, cond)
#define MAP_LOG(value) DebugTraits::LOG(#value, value)
#define MAP_LOGL(value) DebugTraits::LOGL(#value, value)

// NB: current implementation of this map assumes that the assign and erase operation will run in mutual exclusion
// With each other, but not with get()
template <typename K, typename V, class DBT, class KT = DefaultKeyTraits<K>, class VT = DefaultValueTraits<V>>
class ConcurrentMap {
public:
    typedef K Key;
    typedef V Value;
    typedef KT KeyTraits;
    typedef VT ValueTraits;
    typedef DBT DebugTraits;

private:
    struct Cell {
        std::atomic<Key> key;
        std::atomic<Value> value;
    };

    std::atomic<ureg> size;
    ureg maxLoad;
    Cell* m_cells;
    ureg m_sizeMask;

public:
    explicit ConcurrentMap(ureg capacity, ureg maxLoad) : size(0), maxLoad(maxLoad) {
        MAP_ASSERT(isPowerOf2(capacity));
        MAP_ASSERT(maxLoad < capacity);
        m_cells = new Cell[capacity];
        m_sizeMask = capacity - 1;
        clear();
    }

    ~ConcurrentMap() {
        delete[] m_cells;
    }

    bool assign(Key key, Value value) {
        MAP_ASSERT(key != KeyTraits::NullKey);
        MAP_ASSERT(value != Value(ValueTraits::NullValue));

        if (key == KeyTraits::NullKey || value == Value(ValueTraits::NullValue)) {
            DBT::LOGL("Detected a null key or value for, key", key);
            return false;
        }

        const ureg previousSize = size.fetch_add(1, std::memory_order_acq_rel);
        if (previousSize >= maxLoad) {
            const ureg newSize = size.fetch_sub(1, std::memory_order_acq_rel);
            DBT::LOGL("Hit load limit", newSize);
            return false;
        }

        DebugTraits::LOG("assigning key", key);
        DebugTraits::LOGL("value", value);

        const ureg predecessorIndex = (KeyTraits::hash(key) - 1) & m_sizeMask;
        ureg idx = predecessorIndex;
        while (true) {
            idx++;
            idx &= m_sizeMask;
            Cell* cell = m_cells + idx;

            if (predecessorIndex == idx) {
                DBT::LOGL("Detected a cycle in ConcurrentMap.assign, key", key);
                return false;
            }

            // Load the key that was there.
            Key probedKey = cell->key.load(std::memory_order_acquire);
            if (probedKey == key) {
                // We were already storing this key, so this update doesn't increase the size
                size.fetch_sub(1, std::memory_order_acq_rel);
            } else {
                // The cell was either free, or contains another key.
                if (probedKey != KeyTraits::NullKey)
                    continue; // Usually, it contains another key. Keep probing.

                // The cell was free. Now let's try to take it using a CAS.
                if (!cell->key.compare_exchange_strong(probedKey, key, std::memory_order_acq_rel))
                    continue; // Another thread just stole it from underneath us.

                // Either we just added the key, or another thread did.
            }

            // Store the value in this cell.
            cell->value.store(value, std::memory_order_release);
            return true;
        }
    }

    Value get(Key key) {
        MAP_ASSERT(key != KeyTraits::NullKey);

        if (key == KeyTraits::NullKey) {
            return Value(ValueTraits::NullValue);
        }

        ureg predecessorIndex = (KeyTraits::hash(key) - 1) & m_sizeMask;
        ureg idx = predecessorIndex;
        while (true) {
            idx++;
            idx &= m_sizeMask;
            Cell* cell = m_cells + idx;

            if (predecessorIndex == idx) {
                DBT::LOGL("Detected a cycle in ConcurrentMap.assign, key", key);
                return false;
            }

            const Key probedKey = cell->key.load(std::memory_order_release);
            if (probedKey == key) {
                const Value loadedValue = cell->value.load(std::memory_order_release);
                const Key reProbedKey = cell->key.load(std::memory_order_release);
                // Otherwise it got erased and rewritten underneath us.
                if (reProbedKey == probedKey) {
                    return loadedValue;
                }
            }
            if (probedKey == KeyTraits::NullKey)
                return Value(ValueTraits::NullValue);
        }
    }

    // Custom addition, port of Agrona's Int2ObjectHashMap
    bool erase(Key key) {
        MAP_ASSERT(key != KeyTraits::NullKey);

        if (key == KeyTraits::NullKey) {
            return false;
        }

        DebugTraits::LOGL("Erasing", key);

        // First probe to the correct cell
        ureg predecessorIndex = (KeyTraits::hash(key) - 1) & m_sizeMask;
        ureg idx = predecessorIndex;
        while (true) {
            idx++;
            idx &= m_sizeMask;
            Cell* cell = m_cells + idx;

            if (predecessorIndex == idx) {
                DBT::LOGL("Detected a cycle in ConcurrentMap.erase, key", key);
                return false;
            }

            Key probedKey = cell->key.load(std::memory_order_acquire);
            if (probedKey == key) {
                // Empty this cell
                nullOutCell(cell);

                // Compact the chain
                return compactChain(idx);
            }

            // We tried to erase something that isn't in the Map
            if (probedKey == KeyTraits::NullKey) {
                DebugTraits::LOGL("Failed to erase", key);
                return false;
            }
        }
    }

    bool compactChain(ureg initialDeleteIndex) {
        ureg deleteIndex = initialDeleteIndex;
        ureg index = deleteIndex;
        while (true) {
            // Probe the next cell
            index++;
            index &= m_sizeMask;
            Cell* cell = m_cells + index;

            // Probably redundant safety check to avoid infinite loops.
            if (index == initialDeleteIndex) {
                DBT::LOGL("Detected a cycle in ConcurrentMap.compactChain, deleteIndex", initialDeleteIndex);
                return false;
            }

            // If we're at the end of the chain then stop
            const Key key = cell->key.load(std::memory_order_acquire);
            if (key == KeyTraits::NullKey) {
                size.fetch_sub(1, std::memory_order_acq_rel);
                return true;
            }

            // Swap out anything that needs rehashing
            const ureg hash = KeyTraits::hash(key) & m_sizeMask;
            if ((index < hash && (hash <= deleteIndex || deleteIndex <= index)) ||
                (hash <= deleteIndex && deleteIndex <= index)) {

                const Value value = cell->value.load(std::memory_order_acquire);
                Cell* deleteCell = m_cells + deleteIndex;
                deleteCell->value.store(value, std::memory_order_release);
                deleteCell->key.store(key, std::memory_order_release);

                nullOutCell(cell);
                deleteIndex = index;
            }
        }
    }

    void nullOutCell(Cell *cell) {
        // We erase the value first to avoid a race with get() probing the key first.
        cell->value.store(Value(ValueTraits::NullValue), std::memory_order_release);
        cell->key.store(KeyTraits::NullKey, std::memory_order_release);
    }

    // ----------------------------------------------------------------------------
    // Code below here is not considered thread-safe - only for debugging / testing
    // Or if your application can guarantee exclusivity
    // ----------------------------------------------------------------------------

    // Deliberately always logs as this these errors would be part of a failing test
    bool isClear() {
        for (ureg idx = 0; idx <= m_sizeMask; idx++) {
            Cell* cell = m_cells + idx;
            const Key key = cell->key.load(std::memory_order_acquire);
            if (key != KeyTraits::NullKey) {
                std::cerr << "Error @ " << idx << ", key = " << key << std::endl;
                return false;
            }

            Value value = cell->value.load(std::memory_order_acquire);
            if (value != Value(ValueTraits::NullValue)) {
                std::cerr << "Error @ " << idx << ", value  = " << value << std::endl;
                return false;
            }
        }
        return true;
    }

    void dumpState() {
        DBT::LOG("Dumping state of map");
        // Must be called when there are no concurrent readers or writers
        for (ureg idx = 0; idx <= m_sizeMask; idx++) {
            const Cell* cell = m_cells + idx;
            const Key key = cell->key.load(std::memory_order_acquire);
            const Value value = cell->value.load(std::memory_order_acquire);

            MAP_LOG(idx);
            MAP_LOG(key);
            MAP_LOGL(value);
        }
        DBT::LOG("\n");
    }

    void clear() {
        // Must be called when there are no concurrent readers or writers
        for (ureg idx = 0; idx <= m_sizeMask; idx++) {
            Cell* cell = m_cells + idx;
            cell->value.store(Value(ValueTraits::NullValue), std::memory_order_release);
            cell->key.store(KeyTraits::NullKey, std::memory_order_release);
        }
        size.store(0, std::memory_order_release);
    }
};

#endif //OPSIAN_CONCURRENTMAP_H
