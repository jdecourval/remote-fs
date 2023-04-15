#ifndef REMOTE_FS_CALLBACKSIMPL_H
#define REMOTE_FS_CALLBACKSIMPL_H

#include <memory>

#include "Callbacks.h"

namespace remotefs::details {
class CallbackNop {};

template <class Callable>
class CallbackEmpty final : public remotefs::CallbackErased {
   public:
    explicit CallbackEmpty(Callable&& c)
        : callable{std::forward<Callable>(c)} {}

    void inline operator()(int res) final {
        callable(res);
    }

    void inline operator()(int res, std::unique_ptr<CallbackErased> self) final {
        static_assert(decltype(self)::deleter_type::is_proper_deleter);
        callable(res);
    }

   private:
    Callable callable;
};

template <typename Callable, typename Storage>
class CallbackWithStorage final : public CallbackWithStorageAbstract<Storage> {
    static_assert(!std::is_same_v<Storage, void>);

   public:
    template <typename... Ts>
    explicit CallbackWithStorage(Callable&& callable, Ts&&... args)
        : storage{std::forward<Ts>(args)...},
          callable{std::forward<Callable>(callable)} {}

    Storage& get_storage() final {
        return storage;
    }

    const Storage& get_storage() const final {
        return storage;
    }

    [[nodiscard]] short get_index() const final {
        return get_pool().get_index(this);
    }

    void inline operator()(int res) final {
        if constexpr (std::is_invocable_v<Callable, int>) {
            callable(res);
        } else if constexpr (std::is_invocable_v<
                                 Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>) {
            assert(false);
        } else {
            static_assert(std::is_invocable_v<Callable, int>);
        }
    }

    void inline operator()(int res, std::unique_ptr<CallbackErased> self) final {
        static_assert(decltype(self)::deleter_type::is_proper_deleter);
        if constexpr (std::is_invocable_v<Callable, int>) {
            callable(res);
        } else if constexpr (std::is_invocable_v<
                                 Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>) {
            // TODO: This is not exception safe
            auto raw = self.release();
            callable(
                res,
                std::unique_ptr<CallbackWithStorageAbstract<Storage>>{
                    polymorphic_downcast<CallbackWithStorageAbstract<Storage>*>(raw)}
            );
        } else {
            static_assert(std::is_invocable_v<Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>);
        }
    }

   public:
    alignas(buffers_alignment) alignas(Storage) Storage storage;
    Callable callable;
};

class CallbackWithAttachedStorageInterface {};

template <typename Callable, typename Storage>
class CallbackWithAttachedStorage final : public CallbackWithStorageAbstract<Storage>,
                                          public CallbackWithAttachedStorageInterface {
    static_assert(!std::is_same_v<Storage, void>);

   public:
    explicit CallbackWithAttachedStorage(
        Callable&& callable, std::unique_ptr<CallbackWithStorageAbstract<Storage>> attached
    )
        : callable{std::forward<Callable>(callable)},
          attached{std::move(attached)} {
        static_assert(decltype(attached)::deleter_type::is_proper_deleter);
    }

    Storage& get_storage() final {
        return attached->get_storage();
    }

    const Storage& get_storage() const final {
        return attached->get_storage();
    }

    [[nodiscard]] short get_index() const final {
        return get_pool().get_index(static_cast<const void*>(attached.get()));
    }

   private:
    void inline operator()(int res) final {
        if constexpr (std::is_invocable_v<Callable, int>) {
            callable(res);
        } else if constexpr (std::is_invocable_v<
                                 Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>) {
            std::terminate();
        } else {
            static_assert(std::is_invocable_v<Callable, int>);
        }
    }

    void inline operator()(int res, [[maybe_unused]] std::unique_ptr<CallbackErased> self) final {
        static_assert(decltype(self)::deleter_type::is_proper_deleter);
        if constexpr (std::is_invocable_v<Callable, int>) {
            attached.reset();
            callable(res);
        } else if constexpr (std::is_invocable_v<
                                 Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>) {
            assert(attached);
            callable(res, std::move(attached));
        } else {
            static_assert(std::is_invocable_v<Callable, int, std::unique_ptr<CallbackWithStorageAbstract<Storage>>>);
        }
    }

   public:
    Callable callable;
    std::unique_ptr<CallbackWithStorageAbstract<Storage>> attached;
};

template <typename Storage>
class CallbackWithAttachedStorageNop final : public CallbackWithStorageAbstract<Storage>,
                                             public CallbackWithAttachedStorageInterface,
                                             public CallbackNop {
    static_assert(!std::is_same_v<Storage, void>);

   public:
    explicit CallbackWithAttachedStorageNop(std::unique_ptr<CallbackWithStorageAbstract<Storage>> attached)
        : attached{std::move(attached)} {
        static_assert(decltype(attached)::deleter_type::is_proper_deleter);
        assert(this->attached);
        assert(this->attached.get() != this);
    }

    Storage& get_storage() final {
        return attached->get_storage();
    }

    const Storage& get_storage() const final {
        return attached->get_storage();
    }

    [[nodiscard]] short get_index() const final {
        return get_pool().get_index(static_cast<const void*>(attached.get()));
    }

    void operator()(int) final {
        throw std::logic_error("Not meant to be executed");
    }

    void operator()(int, std::unique_ptr<CallbackErased>) final {
        throw std::logic_error("Not meant to be executed");
    }

   public:
    std::unique_ptr<CallbackWithStorageAbstract<Storage>> attached;
};
}  // namespace remotefs::details

#endif  // REMOTE_FS_CALLBACKSIMPL_H
