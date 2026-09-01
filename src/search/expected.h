// Minimal Expected<T, E> shim for the search facade.
//
// The S0 design is written against the std::expected shape, but std::expected
// is not available under -std=c++20 on the supported toolchains (it is a C++23
// library feature with uneven backport coverage). This header ships the agreed
// tiny in-repo shim instead of adding a vendored dependency. Swap to
// std::expected when the floor toolchain provides it.

#ifndef ISLAND_SEARCH_EXPECTED_H_
#define ISLAND_SEARCH_EXPECTED_H_

#include <type_traits>
#include <utility>
#include <variant>

namespace island {
namespace search {

// Holds either a value of type T or an error of type E. Never both, never
// neither. Movable and copyable when T and E are.
template <typename T, typename E>
class Expected {
  public:
    template <typename U = T, typename = std::enable_if_t<std::is_constructible_v<T, U&&>>>
    Expected(U&& value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    Expected(const Expected&) = default;
    Expected(Expected&&) noexcept(std::is_nothrow_move_constructible_v<T>&&
                                      std::is_nothrow_move_constructible_v<E>) = default;
    Expected& operator=(const Expected&) = default;
    Expected& operator=(Expected&&) noexcept(std::is_nothrow_move_assignable_v<T>&&
                                                 std::is_nothrow_move_assignable_v<E>) = default;

    static Expected Error(E error) {
        Expected result(Uninitialized{});
        result.storage_.template emplace<1>(std::move(error));
        return result;
    }

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] E& error() & { return std::get<1>(storage_); }
    [[nodiscard]] const E& error() const& { return std::get<1>(storage_); }

  private:
    struct Uninitialized {};
    explicit Expected(Uninitialized) : storage_(std::in_place_index<1>, E{}) {}

    std::variant<T, E> storage_;
};

// void-value specialization: success carries no payload.
template <typename E>
class Expected<void, E> {
  public:
    static Expected Ok() { return Expected(true); }

    static Expected Error(E error) {
        Expected result(false);
        result.error_ = std::move(error);
        return result;
    }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] E& error() & { return error_; }
    [[nodiscard]] const E& error() const& { return error_; }

  private:
    explicit Expected(bool ok) : ok_(ok) {}

    bool ok_;
    E error_{};
};

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_EXPECTED_H_
