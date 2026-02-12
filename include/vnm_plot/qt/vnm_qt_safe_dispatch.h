#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QtGlobal>

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @file vnm_qt_safe_dispatch.h
 * @brief Type-safe helper for posting method calls to a QObject's thread.
 *
 * @code
 * // Fire-and-forget a method call:
 * vnm::post_invoke(m_plot, &VNM_plot::adjust_t_to_target, new_min, new_max);
 * @endcode
 *
 * @warning **Argument Lifetimes**: Avoid passing non-owning views (e.g., `std::string_view`)
 *   to queued calls unless the underlying data is guaranteed to outlive the call's
 *   execution on the target thread. Prefer passing owning copies.
 */

namespace vnm {
namespace detail {

template<class T> constexpr T* to_raw(T* p) noexcept { return p; }
template<class T> constexpr T* to_raw(QPointer<T> const& p) noexcept { return p.data(); }
template<class T> constexpr T* to_raw(QPointer<T>* p) noexcept { return p ? p->data() : nullptr; }
template<class T> constexpr T* to_raw(QPointer<T> const* p) noexcept { return p ? p->data() : nullptr; }

template<class M> struct member_class;

#ifndef VNM_NODISCARD
#  ifdef VNM_STRICT_API
#    define VNM_NODISCARD [[nodiscard]]
#  else
#    define VNM_NODISCARD
#  endif
#endif

// Expands to cover all cv- and ref-qualifiers, with and without noexcept.
#define VNM_MEMBER_QUAL(REFQUAL, NOEX) \
    template<class C, class R, class... A> \
    struct member_class<R (C::*)(A...) REFQUAL NOEX> { using type = C REFQUAL; };
#define VNM_FOR_ALL_REFQUAL(NOEX) \
    VNM_MEMBER_QUAL(                , NOEX) VNM_MEMBER_QUAL(const           , NOEX) \
    VNM_MEMBER_QUAL(volatile        , NOEX) VNM_MEMBER_QUAL(const volatile  , NOEX) \
    VNM_MEMBER_QUAL(&               , NOEX) VNM_MEMBER_QUAL(const &         , NOEX) \
    VNM_MEMBER_QUAL(volatile &      , NOEX) VNM_MEMBER_QUAL(const volatile &, NOEX) \
    VNM_MEMBER_QUAL(&&              , NOEX) VNM_MEMBER_QUAL(const &&        , NOEX) \
    VNM_MEMBER_QUAL(volatile &&     , NOEX) VNM_MEMBER_QUAL(const volatile&&, NOEX)
#define VNM_EMPTY /* nothing */
VNM_FOR_ALL_REFQUAL(VNM_EMPTY)   // no noexcept
VNM_FOR_ALL_REFQUAL(noexcept)    // with noexcept
#undef VNM_EMPTY
#undef VNM_FOR_ALL_REFQUAL
#undef VNM_MEMBER_QUAL

template<class Method> using member_class_t = typename member_class<Method>::type;

template<class Method> using this_ptr_t = std::add_pointer_t<
    std::remove_reference_t<
        member_class_t<Method>
    >
>;

template<class T>
constexpr std::decay_t<T> decay_copy(T&& v) noexcept(std::is_nothrow_constructible_v<std::decay_t<T>, T&&>)
{
    return std::forward<T>(v);
}

} // namespace detail


/**
 * @brief Posts a method for asynchronous invocation (always queued). "Fire-and-forget".
 * @return True if the call was successfully queued.
 * @return False if the receiver was null or if argument capture failed.
 */
template<class ObjLike, class Method, class... Args>
VNM_NODISCARD bool post_invoke(ObjLike&& obj_like, Method method, Args&&... args) noexcept
{
    auto* raw_cv = detail::to_raw(std::forward<ObjLike>(obj_like));
    if (!raw_cv) {
        return false;
    }

    using raw_type_cv = std::remove_pointer_t<decltype(raw_cv)>;
    using base_type   = std::remove_cv_t<raw_type_cv>;
    using this_ptr    = detail::this_ptr_t<Method>;

    static_assert(std::is_member_function_pointer_v<Method>,
        "Method must be a pointer-to-member function");
    static_assert(std::is_base_of_v<QObject, base_type>,
        "Receiver must inherit QObject");
    static_assert(std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<this_ptr>>, base_type>,
        "Method not in object's class hierarchy");
    static_assert(std::is_invocable_v<Method, this_ptr, Args...>,
        "Method cannot be invoked with provided arguments");
    static_assert(((std::is_copy_constructible_v<std::decay_t<Args>>
                    || std::is_rvalue_reference_v<Args&&>) && ...),
        "Each lvalue argument must be copy-constructible; pass rvalues (std::move) for move-only types.");

    constexpr bool receiver_is_const = std::is_const_v<raw_type_cv>;
    constexpr bool method_is_const   = std::is_const_v<
        std::remove_reference_t<
            detail::member_class_t<Method>
        >
    >;
    static_assert(!receiver_is_const || method_is_const,
        "Cannot invoke a non-const method on a const receiver");

    try {
        QPointer<base_type> guard{const_cast<base_type*>(raw_cv)};
        return QMetaObject::invokeMethod(
            const_cast<base_type*>(raw_cv),
            [guard, method, tup = std::make_tuple(
                vnm::detail::decay_copy(std::forward<Args>(args))...)]() mutable
            {
                auto* obj = guard.data();
                if (!obj) {
                    return;
                }
                try {
                    std::apply(
                        [&](auto&&... u){
                            std::invoke(
                                method,
                                static_cast<this_ptr>(obj),
                                std::forward<decltype(u)>(u)...
                            );
                        },
                        std::move(tup)
                    );
                }
                catch (...) {
                    qWarning("post_invoke: Exception swallowed in queued call.");
                }
            },
            Qt::QueuedConnection
        );
    }
    catch (...) {
        return false;
    }
}

// Convenience overload for lvalue objects (non-pointer).
template<class Obj, class Method, class... Args,
         std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Obj>>, int> = 0>
VNM_NODISCARD bool post_invoke(Obj& o, Method m, Args&&... args)
{
    return vnm::post_invoke(std::addressof(o), m, std::forward<Args>(args)...);
}

// Convenience overload for lvalue pointers.
template<class Ptr, class Method, class... Args,
         std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD bool post_invoke(Ptr& p, Method m, Args&&... args)
{
    if (!p) {
        return false;
    }
    return vnm::post_invoke(*p, m, std::forward<Args>(args)...);
}

} // namespace vnm
