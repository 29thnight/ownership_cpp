#ifndef OWN_OWNERSHIP_HPP
#define OWN_OWNERSHIP_HPP

// Copyright (c) 2026 29thnight. Licensed under the MIT License; see LICENSE.
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

#ifndef OWN_DEBUG_THREAD_CHECK
#ifdef NDEBUG
#define OWN_DEBUG_THREAD_CHECK 0
#else
#define OWN_DEBUG_THREAD_CHECK 1
#endif
#endif

#ifndef OWN_ENABLE_UNSAFE_GET_WARNING
#define OWN_ENABLE_UNSAFE_GET_WARNING 1
#endif

namespace own
{
    template<class T> class unique_owner;
    template<class T> class allocated_unique_owner;
    template<class T> class local_owner;
    template<class T> class shared_owner;
    template<class T> class weak_owner;
    template<class T> class local_view;
    template<class T> class enable_owner_from_this;
    class retirement_task;

    namespace detail
    {
        [[noreturn]] inline void fail_fast() noexcept
        {
            std::abort();
        }

        inline void* default_allocate(void*, std::size_t bytes, std::size_t alignment)
        {
            if (alignment > alignof(std::max_align_t))
            {
                return ::operator new(bytes, std::align_val_t(alignment));
            }
            return ::operator new(bytes);
        }

        inline void default_deallocate(void*, void* pointer, std::size_t,
                                       std::size_t alignment) noexcept
        {
            if (alignment > alignof(std::max_align_t))
            {
                ::operator delete(pointer, std::align_val_t(alignment));
                return;
            }
            ::operator delete(pointer);
        }

        template<class From, class To>
        inline constexpr bool unique_conversion_allowed = []() noexcept
        {
            if constexpr (!std::is_convertible_v<From*, To*>)
            {
                return false;
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<From>, std::remove_cv_t<To>>)
            {
                return true; // Qualification conversion keeps the same delete type.
            }
            else
            {
                // Unlike an erased owner, the default deletes through its view
                // type. A polymorphic upcast must preserve valid destruction.
                return std::has_virtual_destructor_v<To> && std::is_nothrow_destructible_v<To>;
            }
        }();

        using unique_deallocate_function = void (*)(void*, void*, std::size_t, std::size_t) noexcept;
        using unique_dispose_function = void (*)(void*, void*, unique_deallocate_function) noexcept;

        template<class T>
        void dispose_unique(void* storage, void* context, unique_deallocate_function deallocate) noexcept
        {
            // The concrete factory type and original allocation survive every
            // adjusted base/const conversion of the owner handle.
            static_cast<T*>(storage)->~T();
            deallocate(context, storage, sizeof(T), alignof(T));
        }

        struct owner_registration_marker {};
        struct control_block;
        struct owner_access;
        void release_strong(control_block*) noexcept;
    }

    // A non-owning byte allocator. Its context must outlive all allocations,
    // including weak handles and deferred retirement tasks. Callbacks must be
    // usable on every thread that can allocate/free these objects.
    struct allocator_ref
    {
        void* context = nullptr;
        void* (*allocate)(void*, std::size_t, std::size_t) = detail::default_allocate;
        void (*deallocate)(void*, void*, std::size_t, std::size_t) noexcept =
            detail::default_deallocate;
    };

    // The callback must be noexcept, and either run the task or move it into
    // a queue. A dropped task runs immediately. The context is non-owning.
    struct retirement_hook
    {
        void* context = nullptr;
        void (*retire)(void*, retirement_task) noexcept = nullptr;
    };

    class retirement_task
    {
    public:
        retirement_task() noexcept = default;
        retirement_task(const retirement_task&) = delete;
        retirement_task& operator=(const retirement_task&) = delete;
        retirement_task(retirement_task&& other) noexcept
            : block_(std::exchange(other.block_, nullptr))
        {
        }
        retirement_task& operator=(retirement_task&& other) noexcept
        {
            if (this != &other)
            {
                run();
                block_ = std::exchange(other.block_, nullptr);
            }
            return *this;
        }
        ~retirement_task() { run(); }
        explicit operator bool() const noexcept { return block_ != nullptr; }
        void run() noexcept;

    private:
        explicit retirement_task(detail::control_block* block) noexcept : block_(block) {}
        detail::control_block* block_ = nullptr;
        friend void detail::release_strong(detail::control_block*) noexcept;
    };

    namespace detail
    {
        inline constexpr std::size_t count_limit = static_cast<std::size_t>(-1);
        // Shared counts abort at half the range. The other half is headroom for
        // increments already in flight when one thread reaches the threshold.
        inline constexpr std::size_t saturation_limit = count_limit / 2;

        // One locked add instead of a load + compare-exchange retry loop, which
        // costs extra round trips of the contended line. Every caller already
        // holds a reference, so a previous value of zero is a use-after-release
        // bug, and reaching the threshold aborts. Wrapping through zero would need
        // more than saturation_limit concurrent unobserved increments.
        inline void increment(std::atomic<std::size_t>& count) noexcept
        {
            const auto previous = count.fetch_add(1, std::memory_order_relaxed);
            if (previous == 0 || previous >= saturation_limit) [[unlikely]] { fail_fast(); }
        }

        struct control_block
        {
            std::atomic<std::size_t> strong{1};
            // One implicit weak reference covers the entire live or retired
            // object lifetime, plus one per external weak_owner.
            std::atomic<std::size_t> weak{1};
            allocator_ref allocator;
            retirement_hook retirement;
            void (*dispose)(control_block*) noexcept;
            void (*destroy)(control_block*) noexcept;

            control_block(allocator_ref resource, retirement_hook hook,
                          void (*dispose_object)(control_block*) noexcept,
                          void (*destroy_control)(control_block*) noexcept) noexcept
                : allocator(resource), retirement(hook), dispose(dispose_object),
                  destroy(destroy_control)
            {
            }
        };

        inline void release_weak(control_block* block) noexcept
        {
            if (block->weak.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                block->destroy(block);
            }
        }

        inline void release_strong(control_block* block) noexcept
        {
            if (block->strong.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // Zero is permanent. Keeping the implicit weak alive permits
                // deferred destruction without successful weak locking.
                auto hook = block->retirement;
                retirement_task task(block);
                if (hook.retire) { hook.retire(hook.context, std::move(task)); }
            }
        }

        inline bool try_add_strong(control_block* block) noexcept
        {
            auto value = block->strong.load(std::memory_order_relaxed);
            while (value != 0)
            {
                if (value >= saturation_limit) { fail_fast(); }
                if (block->strong.compare_exchange_weak(value, value + 1,
                        std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return true;
                }
            }
            return false;
        }

        inline void* allocate_bytes(allocator_ref allocator, std::size_t size,
                                    std::size_t alignment)
        {
            if (!allocator.allocate || !allocator.deallocate) { fail_fast(); }
            void* result = allocator.allocate(allocator.context, size, alignment);
            if (!result) { throw std::bad_alloc(); }
            return result;
        }

        template<class T>
        struct in_place_control final : control_block
        {
            alignas(T) unsigned char storage[sizeof(T)];

            template<class... Args>
            in_place_control(allocator_ref allocator, retirement_hook hook, Args&&... args)
                : control_block(allocator, hook, dispose_object, destroy_control)
            {
                ::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
            }

            T* pointer() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }

            static void dispose_object(control_block* base) noexcept
            {
                static_cast<in_place_control*>(base)->pointer()->~T();
            }
            static void destroy_control(control_block* base) noexcept
            {
                auto* block = static_cast<in_place_control*>(base);
                auto allocator = block->allocator;
                block->~in_place_control();
                allocator.deallocate(allocator.context, block, sizeof(in_place_control),
                                     alignof(in_place_control));
            }
        };

        inline std::size_t current_thread_id() noexcept
        {
            // Unique monotonic IDs also catch destruction on a newly created
            // thread after the origin thread has exited (TLS addresses reuse).
            // IDs start at 1: zero marks a group created without checks.
            static std::atomic<std::size_t> next{1};
            thread_local const std::size_t id = []() noexcept
            {
                auto value = next.load(std::memory_order_relaxed);
                for (;;)
                {
                    if (value == count_limit) { fail_fast(); }
                    if (next.compare_exchange_weak(value, value + 1,
                            std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        return value;
                    }
                }
            }();
            return id;
        }

        struct local_group
        {
            std::size_t references = 1;
            control_block* block;
            allocator_ref allocator;
            // Present in every configuration: translation units that disagree on
            // NDEBUG/OWN_DEBUG_THREAD_CHECK must still agree on size and offsets,
            // because groups are allocated, read and freed across them. Zero means
            // the group was created without checks; such a group is never checked.
            const std::size_t thread_id = OWN_DEBUG_THREAD_CHECK ? current_thread_id() : 0;
            local_group(control_block* control, allocator_ref resource) noexcept
                : block(control), allocator(resource)
            {
            }
            void check_thread() const noexcept
            {
#if OWN_DEBUG_THREAD_CHECK
                if (thread_id != 0 && thread_id != current_thread_id()) { fail_fast(); }
#endif
            }
            void add_reference() noexcept
            {
                check_thread();
                if (references == count_limit) { fail_fast(); }
                ++references;
            }
            void release() noexcept
            {
                check_thread();
                if (--references == 0)
                {
                    auto* control = block;
                    auto resource = allocator;
                    this->~local_group();
                    resource.deallocate(resource.context, this, sizeof(local_group),
                                        alignof(local_group));
                    release_strong(control);
                }
            }
        };

        inline local_group* new_group(control_block* block, allocator_ref allocator)
        {
            void* storage = allocate_bytes(allocator, sizeof(local_group), alignof(local_group));
            return ::new (storage) local_group(block, allocator);
        }
    }

    inline void retirement_task::run() noexcept
    {
        if (auto* block = std::exchange(block_, nullptr))
        {
            block->dispose(block);
            detail::release_weak(block);
        }
    }

    // Non-owning access only. Copying a view does not retain or inspect any
    // ownership record. The caller keeps an owner alive for every use.
    template<class T>
    class local_view
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        constexpr local_view() noexcept = default;
        constexpr local_view(std::nullptr_t) noexcept {}
        template<class U> requires std::is_convertible_v<U*, T*>
        constexpr local_view(const local_view<U>& other) noexcept : pointer_(other.pointer_) {}
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        constexpr T* unsafe_get() const noexcept { return pointer_; }
        constexpr explicit operator bool() const noexcept { return pointer_ != nullptr; }
        constexpr T& operator*() const noexcept { return *pointer_; }
        constexpr T* operator->() const noexcept { return pointer_; }
        constexpr void reset() noexcept { pointer_ = nullptr; }
        constexpr void swap(local_view& other) noexcept { std::swap(pointer_, other.pointer_); }

    private:
        T* pointer_ = nullptr;
        explicit constexpr local_view(T* pointer) noexcept : pointer_(pointer) {}
        template<class> friend class local_view;
        template<class> friend class unique_owner;
        template<class> friend class allocated_unique_owner;
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class enable_owner_from_this;
    };

    // Default exclusive ownership is just one pointer. Typed new/delete supply
    // destruction and allocation lookup without runtime allocator metadata.
    template<class T>
    class unique_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        unique_owner() noexcept = default;
        unique_owner(std::nullptr_t) noexcept {}
        unique_owner(const unique_owner&) = delete;
        unique_owner& operator=(const unique_owner&) = delete;
        unique_owner(unique_owner&& other) noexcept
            : pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires detail::unique_conversion_allowed<U, T>
        unique_owner(unique_owner<U>&& other) noexcept
            : pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        ~unique_owner() { destroy(pointer_); }
        unique_owner& operator=(unique_owner&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires detail::unique_conversion_allowed<U, T>
        unique_owner& operator=(unique_owner<U>&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { destroy(std::exchange(pointer_, nullptr)); }
        void swap(unique_owner& other) noexcept { std::swap(pointer_, other.pointer_); }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }

    private:
        T* pointer_ = nullptr;
        static void destroy(T* pointer) noexcept
        {
            static_assert(sizeof(T) > 0, "unique_owner destruction requires a complete payload type");
            static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
            if (pointer) { delete pointer; }
        }
        explicit unique_owner(T* pointer) noexcept : pointer_(pointer) {}
        template<class> friend class unique_owner;
        friend struct detail::owner_access;
    };

    // Explicit allocator-aware ownership. Erased allocator/destruction state
    // lives in this opt-in handle, never in the pointer-only default owner.
    template<class T>
    class allocated_unique_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        allocated_unique_owner() noexcept = default;
        allocated_unique_owner(std::nullptr_t) noexcept {}
        allocated_unique_owner(const allocated_unique_owner&) = delete;
        allocated_unique_owner& operator=(const allocated_unique_owner&) = delete;
        allocated_unique_owner(allocated_unique_owner&& other) noexcept { take_from(other); }
        template<class U> requires std::is_convertible_v<U*, T*>
        allocated_unique_owner(allocated_unique_owner<U>&& other) noexcept { take_from(other); }
        ~allocated_unique_owner() { reset(); }
        allocated_unique_owner& operator=(allocated_unique_owner&& other) noexcept
        {
            allocated_unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        allocated_unique_owner& operator=(allocated_unique_owner<U>&& other) noexcept
        {
            allocated_unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept
        {
            if (!pointer_) { return; }
            auto* storage = storage_;
            auto* context = context_;
            auto deallocate = deallocate_;
            auto dispose = dispose_;
            pointer_ = nullptr;
            // Empty before callbacks; do not touch this again, because user
            // cleanup may install a replacement owner through a saved handle.
            dispose(storage, context, deallocate);
        }
        void swap(allocated_unique_owner& other) noexcept
        {
            if (pointer_ && other.pointer_)
            {
                std::swap(pointer_, other.pointer_);
                std::swap(storage_, other.storage_);
                std::swap(context_, other.context_);
                std::swap(deallocate_, other.deallocate_);
                std::swap(dispose_, other.dispose_);
            }
            else if (pointer_)
            {
                other.take_from(*this);
            }
            else if (other.pointer_)
            {
                take_from(other);
            }
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }

    private:
        // pointer_ is the sole ownership indicator. The four cleanup fields
        // are meaningful/readable only when pointer_ is non-null.
        T* pointer_ = nullptr;
        void* storage_ = nullptr;
        void* context_ = nullptr;
        detail::unique_deallocate_function deallocate_ = nullptr;
        detail::unique_dispose_function dispose_ = nullptr;

        allocated_unique_owner(T* pointer, void* storage, allocator_ref allocator,
                     detail::unique_dispose_function dispose) noexcept
            : pointer_(pointer), storage_(storage), context_(allocator.context),
              deallocate_(allocator.deallocate), dispose_(dispose)
        {
        }
        template<class U>
        void take_from(allocated_unique_owner<U>& other) noexcept
        {
            // Called only for a new or logically empty destination. Inactive
            // cleanup fields may hold invalid pointer values after the old
            // allocation/context dies: never copy/read them on an empty path.
            pointer_ = other.pointer_;
            if (!pointer_) { return; }
            storage_ = other.storage_;
            context_ = other.context_;
            deallocate_ = other.deallocate_;
            dispose_ = other.dispose_;
            other.pointer_ = nullptr;
        }
        template<class> friend class allocated_unique_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    class shared_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        shared_owner() noexcept = default;
        shared_owner(std::nullptr_t) noexcept {}
        shared_owner(const shared_owner& other) noexcept
            : block_(other.block_), pointer_(other.pointer_)
        {
            if (block_) { detail::increment(block_->strong); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner(const shared_owner<U>& other) noexcept
            : block_(other.block_), pointer_(other.pointer_)
        {
            if (block_) { detail::increment(block_->strong); }
        }
        shared_owner(shared_owner&& other) noexcept
            : block_(std::exchange(other.block_, nullptr)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner(shared_owner<U>&& other) noexcept
            : block_(std::exchange(other.block_, nullptr)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        ~shared_owner() { if (block_) { detail::release_strong(block_); } }
        shared_owner& operator=(const shared_owner& other) noexcept
        {
            shared_owner(other).swap(*this);
            return *this;
        }
        shared_owner& operator=(shared_owner&& other) noexcept
        {
            shared_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner& operator=(const shared_owner<U>& other) noexcept
        {
            shared_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner& operator=(shared_owner<U>&& other) noexcept
        {
            shared_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { shared_owner().swap(*this); }
        void swap(shared_owner& other) noexcept
        {
            std::swap(block_, other.block_);
            std::swap(pointer_, other.pointer_);
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }
        std::size_t use_count() const noexcept
        {
            return block_ ? block_->strong.load(std::memory_order_relaxed) : 0;
        }
        [[nodiscard]] local_owner<T> localize(allocator_ref allocator = {}) const &;
        [[nodiscard]] local_owner<T> localize(allocator_ref allocator = {}) &&;

    private:
        detail::control_block* block_ = nullptr;
        T* pointer_ = nullptr;
        shared_owner(detail::control_block* block, T* pointer) noexcept
            : block_(block), pointer_(pointer)
        {
        }
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class weak_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    class local_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        local_owner() noexcept = default;
        local_owner(std::nullptr_t) noexcept {}
        local_owner(const local_owner& other) noexcept
            : group_(other.group_), pointer_(other.pointer_)
        {
            if (group_) { group_->add_reference(); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner(const local_owner<U>& other) noexcept
            : group_(other.group_), pointer_(other.pointer_)
        {
            if (group_) { group_->add_reference(); }
        }
        local_owner(local_owner&& other) noexcept
        {
            other.check_thread();
            group_ = std::exchange(other.group_, nullptr);
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner(local_owner<U>&& other) noexcept
        {
            other.check_thread();
            group_ = std::exchange(other.group_, nullptr);
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        ~local_owner() { if (group_) { group_->release(); } }
        local_owner& operator=(const local_owner& other) noexcept
        {
            local_owner(other).swap(*this);
            return *this;
        }
        local_owner& operator=(local_owner&& other) noexcept
        {
            local_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner& operator=(const local_owner<U>& other) noexcept
        {
            local_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner& operator=(local_owner<U>&& other) noexcept
        {
            local_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { local_owner().swap(*this); }
        void swap(local_owner& other) noexcept
        {
            check_thread();
            other.check_thread();
            std::swap(group_, other.group_);
            std::swap(pointer_, other.pointer_);
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { check_thread(); return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept
        {
            check_thread();
            return local_view<T>(pointer_);
        }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { check_thread(); return pointer_ != nullptr; }
        T& operator*() const noexcept { check_thread(); return *pointer_; }
        T* operator->() const noexcept { check_thread(); return pointer_; }
        std::size_t use_count() const noexcept
        {
            check_thread();
            return group_ ? group_->block->strong.load(std::memory_order_relaxed) : 0;
        }
        std::size_t local_use_count() const noexcept
        {
            check_thread();
            return group_ ? group_->references : 0;
        }
        [[nodiscard]] shared_owner<T> share() const noexcept
        {
            check_thread();
            if (!group_) { return {}; }
            detail::increment(group_->block->strong);
            return shared_owner<T>(group_->block, pointer_);
        }

    private:
        detail::local_group* group_ = nullptr;
        T* pointer_ = nullptr;
        void check_thread() const noexcept { if (group_) { group_->check_thread(); } }
        local_owner(detail::local_group* group, T* pointer) noexcept
            : group_(group), pointer_(pointer)
        {
        }
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class weak_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    local_owner<T> shared_owner<T>::localize(allocator_ref allocator) const &
    {
        if (!block_) { return {}; }
        // Allocate first. Failure leaves this owner and its count unchanged.
        auto* group = detail::new_group(block_, allocator);
        detail::increment(block_->strong);
        return local_owner<T>(group, pointer_);
    }

    template<class T>
    local_owner<T> shared_owner<T>::localize(allocator_ref allocator) &&
    {
        if (!block_) { return {}; }
        auto* group = detail::new_group(block_, allocator);
        auto* pointer = std::exchange(pointer_, nullptr);
        block_ = nullptr; // Transfer this global strong reference into the group.
        return local_owner<T>(group, pointer);
    }

    template<class T>
    class weak_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        weak_owner() noexcept = default;
        weak_owner(const weak_owner& other) noexcept
            : block_(other.block_), pointer_(other.pointer_)
        {
            if (block_) { detail::increment(block_->weak); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const weak_owner<U>& other) noexcept
            : block_(other.block_)
        {
            if (block_)
            {
                detail::increment(block_->weak);
                // Derived-to-virtual-base adjustment can read the object.
                // Never adjust an expired pointer, even though its storage
                // remains allocated for the weak control block.
                if (detail::try_add_strong(block_))
                {
                    pointer_ = other.pointer_;
                    detail::release_strong(block_);
                }
            }
        }
        weak_owner(weak_owner&& other) noexcept
            : block_(std::exchange(other.block_, nullptr)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(weak_owner<U>&& other) noexcept
            : weak_owner(static_cast<const weak_owner<U>&>(other))
        {
            other.reset();
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const shared_owner<U>& other) noexcept
            : block_(other.block_), pointer_(other.pointer_)
        {
            if (block_) { detail::increment(block_->weak); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const local_owner<U>& other) noexcept
        {
            other.check_thread();
            if (other.group_)
            {
                block_ = other.group_->block;
                pointer_ = other.pointer_;
                detail::increment(block_->weak);
            }
        }
        ~weak_owner() { if (block_) { detail::release_weak(block_); } }
        weak_owner& operator=(const weak_owner& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        weak_owner& operator=(weak_owner&& other) noexcept
        {
            weak_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const weak_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(weak_owner<U>&& other) noexcept
        {
            weak_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const shared_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const local_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        void reset() noexcept { weak_owner().swap(*this); }
        void swap(weak_owner& other) noexcept
        {
            std::swap(block_, other.block_);
            std::swap(pointer_, other.pointer_);
        }
        std::size_t use_count() const noexcept
        {
            return block_ ? block_->strong.load(std::memory_order_relaxed) : 0;
        }
        bool expired() const noexcept { return use_count() == 0; }
        [[nodiscard]] shared_owner<T> lock() const noexcept
        {
            if (block_ && detail::try_add_strong(block_))
            {
                return shared_owner<T>(block_, pointer_);
            }
            return {};
        }

    private:
        detail::control_block* block_ = nullptr;
        T* pointer_ = nullptr;
        template<class> friend class weak_owner;
        template<class> friend class enable_owner_from_this;
    };

    // Registration is factory-only. This weak reference never owns the object.
    // Copy/move construction starts unregistered; assignment keeps the target's
    // registration, just as assigning payload must not replace its owner block.
    template<class T>
    class enable_owner_from_this : public detail::owner_registration_marker
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        [[nodiscard]] shared_owner<T> shared_from_this() noexcept { return weak_.lock(); }
        [[nodiscard]] shared_owner<const T> shared_from_this() const noexcept { return weak_.lock(); }
        [[nodiscard]] weak_owner<T> weak_from_this() noexcept { return weak_; }
        [[nodiscard]] weak_owner<const T> weak_from_this() const noexcept { return weak_; }
        [[nodiscard]] local_view<T> borrow_from_this() & noexcept
        {
            return weak_.expired() ? local_view<T>() : local_view<T>(weak_.pointer_);
        }
        [[nodiscard]] local_view<const T> borrow_from_this() const & noexcept
        {
            return weak_.expired() ? local_view<const T>() : local_view<const T>(weak_.pointer_);
        }
        local_view<T> borrow_from_this() && = delete;
        local_view<const T> borrow_from_this() const && = delete;
        [[nodiscard]] local_view<T> local_from_this() & noexcept { return borrow_from_this(); }
        [[nodiscard]] local_view<const T> local_from_this() const & noexcept { return borrow_from_this(); }
        local_view<T> local_from_this() && = delete;
        local_view<const T> local_from_this() const && = delete;

    protected:
        constexpr enable_owner_from_this() noexcept = default;
        enable_owner_from_this(const enable_owner_from_this&) noexcept {}
        enable_owner_from_this(enable_owner_from_this&&) noexcept {}
        enable_owner_from_this& operator=(const enable_owner_from_this&) noexcept { return *this; }
        enable_owner_from_this& operator=(enable_owner_from_this&&) noexcept { return *this; }
        ~enable_owner_from_this() = default;

    private:
        mutable weak_owner<T> weak_;
        friend const enable_owner_from_this* owner_from_this_base(
            detail::control_block*, const enable_owner_from_this* base) noexcept
        {
            return base;
        }
        template<class U>
        void accept_owner(detail::control_block* block, U* object) const noexcept
        {
            static_assert(std::is_convertible_v<std::remove_cv_t<U>*, T*>,
                          "enable_owner_from_this must name a public unambiguous object base");
            if (weak_.block_) { detail::fail_fast(); }
            // Const factories bind the same registration; their const member
            // accessors expose only const ownership/views, as expected.
            weak_.pointer_ = const_cast<std::remove_cv_t<U>*>(object);
            weak_.block_ = block;
            detail::increment(block->weak);
        }
        friend struct detail::owner_access;
    };

    namespace detail
    {
        struct owner_access
        {
            template<class T>
            static unique_owner<T> adopt_unique(T* pointer) noexcept
            {
                return unique_owner<T>(pointer);
            }
            template<class T>
            static allocated_unique_owner<T> adopt_allocated_unique(T* pointer, void* storage,
                                                                   allocator_ref allocator) noexcept
            {
                return allocated_unique_owner<T>(pointer, storage, allocator, dispose_unique<T>);
            }
            template<class T>
            static shared_owner<T> adopt(control_block* block, T* pointer) noexcept
            {
                return shared_owner<T>(block, pointer);
            }
            template<class T>
            static void bind_from_this(control_block* block, T* object) noexcept
            {
                if constexpr (std::is_base_of_v<owner_registration_marker, std::remove_cv_t<T>>)
                {
                    static_assert(requires { owner_from_this_base(block, object); },
                                  "enable_owner_from_this must be a single public unambiguous base");
                    if constexpr (requires { owner_from_this_base(block, object); })
                    {
                        owner_from_this_base(block, object)->accept_owner(block, object);
                    }
                }
            }
            template<class T>
            static void set_retirement(local_owner<T>& owner, retirement_hook hook) noexcept
            {
                owner.group_->block->retirement = hook;
            }
        };
    }

    template<class T, class... Args>
    [[nodiscard]] allocated_unique_owner<T> allocate_unique(allocator_ref allocator, Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
        void* storage = detail::allocate_bytes(allocator, sizeof(T), alignof(T));
        T* pointer;
        try
        {
            pointer = ::new (storage) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            allocator.deallocate(allocator.context, storage, sizeof(T), alignof(T));
            throw;
        }
        // Unique ownership does not create/register any shared or weak control.
        return detail::owner_access::adopt_allocated_unique(pointer, storage, allocator);
    }

    template<class T, class... Args>
    [[nodiscard]] unique_owner<T> make_unique(Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
        // Ordinary typed new/delete honor the same class-specific allocation
        // functions, including language-provided constructor-failure cleanup.
        return detail::owner_access::adopt_unique(new T(std::forward<Args>(args)...));
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> allocate_shared_with(allocator_ref allocator,
                                                       retirement_hook hook, Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
        using block_type = detail::in_place_control<T>;
        void* storage = detail::allocate_bytes(allocator, sizeof(block_type), alignof(block_type));
        block_type* block;
        try
        {
            block = ::new (storage) block_type(allocator, hook, std::forward<Args>(args)...);
        }
        catch (...)
        {
            allocator.deallocate(allocator.context, storage, sizeof(block_type), alignof(block_type));
            throw;
        }
        detail::owner_access::bind_from_this(block, block->pointer());
        return detail::owner_access::adopt(block, block->pointer());
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> allocate_shared(allocator_ref allocator, Args&&... args)
    {
        return own::allocate_shared_with<T>(allocator, {}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> make_shared_with(retirement_hook hook, Args&&... args)
    {
        return own::allocate_shared_with<T>({}, hook, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> make_shared(Args&&... args)
    {
        return own::allocate_shared<T>({}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> allocate_local_with(allocator_ref allocator,
                                                     retirement_hook hook, Args&&... args)
    {
        // Install the hook only after both allocations succeed. A failed
        // factory cleans up immediately rather than queueing an unseen object.
        auto result = own::allocate_shared_with<T>(allocator, {}, std::forward<Args>(args)...)
            .localize(allocator);
        detail::owner_access::set_retirement(result, hook);
        return result;
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> allocate_local(allocator_ref allocator, Args&&... args)
    {
        return own::allocate_local_with<T>(allocator, {}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> make_local_with(retirement_hook hook, Args&&... args)
    {
        return own::allocate_local_with<T>({}, hook, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> make_local(Args&&... args)
    {
        return own::allocate_local<T>({}, std::forward<Args>(args)...);
    }

    template<class T> void swap(unique_owner<T>& a, unique_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(allocated_unique_owner<T>& a, allocated_unique_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_view<T>& a, local_view<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(shared_owner<T>& a, shared_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_owner<T>& a, local_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(weak_owner<T>& a, weak_owner<T>& b) noexcept { a.swap(b); }
}

#endif
