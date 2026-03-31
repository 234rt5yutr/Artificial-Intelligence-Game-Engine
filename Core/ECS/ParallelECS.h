#pragma once

#include "Core/ECS/Scene.h"
#include "Core/JobSystem/JobSystem.h"
#include "Core/Profile.h"
#include <entt/entt.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <type_traits>

namespace Core {
namespace ECS {

    //=========================================================================
    // Cache Line Optimization Constants
    //=========================================================================

    // Standard cache line size (64 bytes on most modern CPUs)
    constexpr size_t CACHE_LINE_SIZE = 64;

    // Optimal batch sizes for parallel processing
    constexpr uint32_t DEFAULT_BATCH_SIZE = 64;
    constexpr uint32_t SMALL_COMPONENT_BATCH_SIZE = 128;  // For components < 32 bytes
    constexpr uint32_t LARGE_COMPONENT_BATCH_SIZE = 32;   // For components > 128 bytes

    //=========================================================================
    // Cache-Aligned Allocator
    //=========================================================================

    template<typename T, size_t Alignment = CACHE_LINE_SIZE>
    struct CacheAlignedAllocator {
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        template<typename U>
        struct rebind {
            using other = CacheAlignedAllocator<U, Alignment>;
        };

        CacheAlignedAllocator() = default;

        template<typename U>
        CacheAlignedAllocator(const CacheAlignedAllocator<U, Alignment>&) noexcept {}

        T* allocate(size_type n) {
            void* ptr = nullptr;
#ifdef _WIN32
            ptr = _aligned_malloc(n * sizeof(T), Alignment);
#else
            if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0) {
                ptr = nullptr;
            }
#endif
            if (!ptr) {
                throw std::bad_alloc();
            }
            return static_cast<T*>(ptr);
        }

        void deallocate(T* ptr, size_type) noexcept {
#ifdef _WIN32
            _aligned_free(ptr);
#else
            free(ptr);
#endif
        }

        template<typename U>
        bool operator==(const CacheAlignedAllocator<U, Alignment>&) const noexcept {
            return true;
        }

        template<typename U>
        bool operator!=(const CacheAlignedAllocator<U, Alignment>&) const noexcept {
            return false;
        }
    };

    // Cache-aligned vector type
    template<typename T>
    using CacheAlignedVector = std::vector<T, CacheAlignedAllocator<T>>;

    //=========================================================================
    // Component Size Categories
    //=========================================================================

    enum class ComponentSizeCategory {
        Tiny,    // <= 16 bytes
        Small,   // <= 64 bytes (fits in cache line)
        Medium,  // <= 128 bytes
        Large    // > 128 bytes
    };

    template<typename T>
    constexpr ComponentSizeCategory GetComponentSizeCategory() {
        if constexpr (sizeof(T) <= 16) {
            return ComponentSizeCategory::Tiny;
        } else if constexpr (sizeof(T) <= CACHE_LINE_SIZE) {
            return ComponentSizeCategory::Small;
        } else if constexpr (sizeof(T) <= 128) {
            return ComponentSizeCategory::Medium;
        } else {
            return ComponentSizeCategory::Large;
        }
    }

    template<typename T>
    constexpr uint32_t GetOptimalBatchSize() {
        constexpr auto category = GetComponentSizeCategory<T>();
        if constexpr (category == ComponentSizeCategory::Tiny) {
            return SMALL_COMPONENT_BATCH_SIZE * 2;
        } else if constexpr (category == ComponentSizeCategory::Small) {
            return SMALL_COMPONENT_BATCH_SIZE;
        } else if constexpr (category == ComponentSizeCategory::Medium) {
            return DEFAULT_BATCH_SIZE;
        } else {
            return LARGE_COMPONENT_BATCH_SIZE;
        }
    }

    //=========================================================================
    // Parallel For-Each Utilities
    //=========================================================================

    // Parallel iteration over entities with specific components
    template<typename... Components, typename Func>
    void ParallelForEach(entt::registry& registry, Func&& func, uint32_t batchSize = DEFAULT_BATCH_SIZE) {
        PROFILE_FUNCTION();

        auto view = registry.view<Components...>();
        
        // Collect entities into a contiguous array for parallel access
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        
        for (auto entity : view) {
            entities.push_back(entity);
        }

        if (entities.empty()) {
            return;
        }

        uint32_t entityCount = static_cast<uint32_t>(entities.size());

        // For small counts, run synchronously
        if (entityCount < batchSize * 2) {
            for (auto entity : entities) {
                func(entity, view.template get<Components>(entity)...);
            }
            return;
        }

        // Parallel dispatch
        JobSystem::Context ctx;
        JobSystem::Dispatch(ctx, entityCount, batchSize, [&](uint32_t index) {
            entt::entity entity = entities[index];
            func(entity, view.template get<Components>(entity)...);
        });
        JobSystem::Wait(ctx);
    }

    // Parallel iteration with index (useful for write to separate output arrays)
    template<typename... Components, typename Func>
    void ParallelForEachIndexed(entt::registry& registry, Func&& func, uint32_t batchSize = DEFAULT_BATCH_SIZE) {
        PROFILE_FUNCTION();

        auto view = registry.view<Components...>();
        
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        
        for (auto entity : view) {
            entities.push_back(entity);
        }

        if (entities.empty()) {
            return;
        }

        uint32_t entityCount = static_cast<uint32_t>(entities.size());

        if (entityCount < batchSize * 2) {
            for (uint32_t i = 0; i < entityCount; ++i) {
                entt::entity entity = entities[i];
                func(i, entity, view.template get<Components>(entity)...);
            }
            return;
        }

        JobSystem::Context ctx;
        JobSystem::Dispatch(ctx, entityCount, batchSize, [&](uint32_t index) {
            entt::entity entity = entities[index];
            func(index, entity, view.template get<Components>(entity)...);
        });
        JobSystem::Wait(ctx);
    }

    // Parallel iteration with read-only access (const components)
    template<typename... Components, typename Func>
    void ParallelForEachReadOnly(const entt::registry& registry, Func&& func, uint32_t batchSize = DEFAULT_BATCH_SIZE) {
        PROFILE_FUNCTION();

        auto view = registry.view<Components...>();
        
        std::vector<entt::entity> entities;
        entities.reserve(view.size_hint());
        
        for (auto entity : view) {
            entities.push_back(entity);
        }

        if (entities.empty()) {
            return;
        }

        uint32_t entityCount = static_cast<uint32_t>(entities.size());

        if (entityCount < batchSize * 2) {
            for (auto entity : entities) {
                func(entity, view.template get<Components>(entity)...);
            }
            return;
        }

        JobSystem::Context ctx;
        JobSystem::Dispatch(ctx, entityCount, batchSize, [&](uint32_t index) {
            entt::entity entity = entities[index];
            func(entity, view.template get<Components>(entity)...);
        });
        JobSystem::Wait(ctx);
    }

    //=========================================================================
    // Batched Processing Utilities
    //=========================================================================

    // Process entities in cache-friendly batches
    template<typename... Components>
    class BatchedView {
    public:
        BatchedView(entt::registry& registry, uint32_t batchSize = DEFAULT_BATCH_SIZE)
            : m_Registry(registry)
            , m_BatchSize(batchSize)
        {
            auto view = registry.view<Components...>();
            m_Entities.reserve(view.size_hint());
            for (auto entity : view) {
                m_Entities.push_back(entity);
            }
        }

        uint32_t GetBatchCount() const {
            return (static_cast<uint32_t>(m_Entities.size()) + m_BatchSize - 1) / m_BatchSize;
        }

        uint32_t GetEntityCount() const {
            return static_cast<uint32_t>(m_Entities.size());
        }

        // Process a specific batch (for manual job distribution)
        template<typename Func>
        void ProcessBatch(uint32_t batchIndex, Func&& func) {
            uint32_t start = batchIndex * m_BatchSize;
            uint32_t end = std::min(start + m_BatchSize, static_cast<uint32_t>(m_Entities.size()));

            auto view = m_Registry.view<Components...>();
            for (uint32_t i = start; i < end; ++i) {
                entt::entity entity = m_Entities[i];
                func(entity, view.template get<Components>(entity)...);
            }
        }

        // Process all batches in parallel
        template<typename Func>
        void ProcessAllParallel(Func&& func) {
            if (m_Entities.empty()) {
                return;
            }

            JobSystem::Context ctx;
            uint32_t batchCount = GetBatchCount();

            JobSystem::Dispatch(ctx, batchCount, 1, [this, &func](uint32_t batchIndex) {
                ProcessBatch(batchIndex, func);
            });

            JobSystem::Wait(ctx);
        }

    private:
        entt::registry& m_Registry;
        std::vector<entt::entity> m_Entities;
        uint32_t m_BatchSize;
    };

    //=========================================================================
    // Prefetching Utilities
    //=========================================================================

    // Prefetch next entity's components while processing current
    template<typename T>
    inline void PrefetchComponent(const T* ptr) {
#if defined(_MSC_VER)
        _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(ptr, 0, 3);  // Read, high temporal locality
#endif
    }

    template<typename T>
    inline void PrefetchComponentWrite(T* ptr) {
#if defined(_MSC_VER)
        _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(ptr, 1, 3);  // Write, high temporal locality
#endif
    }

    //=========================================================================
    // Thread-Local Scratch Buffers
    //=========================================================================

    // Thread-local storage for temporary per-thread data during parallel processing
    template<typename T, size_t MaxThreads = 64>
    class ThreadLocalScratch {
    public:
        ThreadLocalScratch() {
            for (size_t i = 0; i < MaxThreads; ++i) {
                m_Data[i] = T{};
            }
        }

        T& Get() {
            thread_local size_t threadIndex = GetThreadIndex();
            return m_Data[threadIndex];
        }

        // Reset all thread-local data
        void Reset() {
            for (size_t i = 0; i < MaxThreads; ++i) {
                m_Data[i] = T{};
            }
        }

        // Aggregate results from all threads
        template<typename AggregateFunc>
        T Aggregate(AggregateFunc&& func) {
            T result = T{};
            for (size_t i = 0; i < MaxThreads; ++i) {
                result = func(result, m_Data[i]);
            }
            return result;
        }

    private:
        static size_t GetThreadIndex() {
            static std::atomic<size_t> nextIndex{0};
            thread_local size_t index = nextIndex.fetch_add(1) % MaxThreads;
            return index;
        }

        alignas(CACHE_LINE_SIZE) std::array<T, MaxThreads> m_Data;
    };

    //=========================================================================
    // System Base Class with Parallel Support
    //=========================================================================

    class ParallelSystemBase {
    public:
        ParallelSystemBase() = default;
        virtual ~ParallelSystemBase() = default;

        // Enable/disable parallel execution
        void SetParallelEnabled(bool enabled) { m_ParallelEnabled = enabled; }
        bool IsParallelEnabled() const { return m_ParallelEnabled; }

        // Set batch size for parallel processing
        void SetBatchSize(uint32_t batchSize) { m_BatchSize = batchSize; }
        uint32_t GetBatchSize() const { return m_BatchSize; }

        // Get the minimum entity count to trigger parallel processing
        void SetParallelThreshold(uint32_t threshold) { m_ParallelThreshold = threshold; }
        uint32_t GetParallelThreshold() const { return m_ParallelThreshold; }

    protected:
        // Check if parallel processing should be used for given count
        bool ShouldRunParallel(uint32_t entityCount) const {
            return m_ParallelEnabled && entityCount >= m_ParallelThreshold;
        }

        // Get the job system context for this system
        JobSystem::Context& GetJobContext() { return m_JobContext; }

        // Wait for all jobs to complete
        void WaitForJobs() {
            JobSystem::Wait(m_JobContext);
        }

    private:
        bool m_ParallelEnabled = true;
        uint32_t m_BatchSize = DEFAULT_BATCH_SIZE;
        uint32_t m_ParallelThreshold = DEFAULT_BATCH_SIZE * 2;
        JobSystem::Context m_JobContext;
    };

    //=========================================================================
    // Component Data Layout Analysis
    //=========================================================================

    // Utility to analyze and report component memory layout
    template<typename T>
    struct ComponentLayoutInfo {
        static constexpr size_t Size = sizeof(T);
        static constexpr size_t Alignment = alignof(T);
        static constexpr size_t CacheLineSpan = (Size + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
        static constexpr bool FitsInCacheLine = Size <= CACHE_LINE_SIZE;
        static constexpr ComponentSizeCategory Category = GetComponentSizeCategory<T>();
        static constexpr uint32_t OptimalBatchSize = GetOptimalBatchSize<T>();
    };

    //=========================================================================
    // Parallel Transform Update Helper
    //=========================================================================

    // Special helper for parallel transform updates (handles hierarchy constraints)
    class ParallelTransformHelper {
    public:
        // Sort entities by hierarchy depth (parents before children)
        static void SortByDepth(entt::registry& registry, std::vector<entt::entity>& entities);

        // Get parallel-safe batches (entities in same batch have no parent-child relationships)
        static std::vector<std::vector<entt::entity>> GetParallelBatches(
            entt::registry& registry,
            const std::vector<entt::entity>& sortedEntities);

        // Update transforms in parallel where safe
        static void UpdateTransformsParallel(Scene& scene);
    };

} // namespace ECS
} // namespace Core
