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

namespace own
{
    template<class T> class local_owner;
    template<class T> class shared_owner;
    template<class T> class weak_owner;
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

        // CAS rather than unchecked fetch_add: even a racing overflow cannot
        // wrap through zero and resurrect a retired control block.
        inline void increment(std::atomic<std::size_t>& count) noexcept
        {
            auto value = count.load(std::memory_order_relaxed);
            for (;;)
            {
                if (value == 0 || value == count_limit) { fail_fast(); }
                if (count.compare_exchange_weak(value, value + 1,
                        std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    return;
                }
            }
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
                if (value == count_limit) { fail_fast(); }
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

#if OWN_DEBUG_THREAD_CHECK
        inline std::size_t current_thread_id() noexcept
        {
            // Unique monotonic IDs also catch destruction on a newly created
            // thread after the origin thread has exited (TLS addresses reuse).
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
#endif

        struct local_group
        {
            std::size_t references = 1;
            control_block* block;
            allocator_ref allocator;
#if OWN_DEBUG_THREAD_CHECK
            const std::size_t thread_id = current_thread_id();
#endif
            local_group(control_block* control, allocator_ref resource) noexcept
                : block(control), allocator(resource)
            {
            }
            void check_thread() const noexcept
            {
#if OWN_DEBUG_THREAD_CHECK
                if (thread_id != current_thread_id()) { fail_fast(); }
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
        T* get() const noexcept { return pointer_; }
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
        T* get() const noexcept { check_thread(); return pointer_; }
        explicit operator bool() const noexcept { return get() != nullptr; }
        T& operator*() const noexcept { return *get(); }
        T* operator->() const noexcept { return get(); }
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
    };

    namespace detail
    {
        struct owner_access
        {
            template<class T>
            static shared_owner<T> adopt(control_block* block, T* pointer) noexcept
            {
                return shared_owner<T>(block, pointer);
            }
            template<class T>
            static void set_retirement(local_owner<T>& owner, retirement_hook hook) noexcept
            {
                owner.group_->block->retirement = hook;
            }
        };
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

    template<class T> void swap(shared_owner<T>& a, shared_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_owner<T>& a, local_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(weak_owner<T>& a, weak_owner<T>& b) noexcept { a.swap(b); }
}

#endif
