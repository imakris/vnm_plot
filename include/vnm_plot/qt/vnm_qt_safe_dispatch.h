#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QtGlobal>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

/**
 * @file vnm_qt_safe_dispatch.h
 * @brief Provides a set of type-safe, const-correct, and thread-safe helpers
 *        for invoking methods and emitting signals on QObjects, ensuring calls
 *        are dispatched to the object's thread.
 *
 * @section usage_examples Usage Examples
 *
 * @subsection ex_invoke Invoking Methods
 * Replace verbose, string-based calls with type-safe, single-line alternatives.
 * @code
 * // Fire-and-forget a method call:
 * vnm::post_invoke(m_plot, &VNM_plot::adjust_t_to_target, new_min, new_max);
 *
 * // Blocking call with return value and error handling:
 * try {
 *     int new_id = vnm::blocking_invoke(m_plot, &VNM_plot::create_data_source, data);
 *     ls_debug() << "Data source attached with ID: " << new_id;
 * }
 * catch (const std::exception& e) {
 *     qWarning() << "Failed to attach data source: " << e.what();
 * }
 * @endcode
 *
 * @subsection ex_emit Emitting Signals
 * Safely emit signals from any thread, ensuring they originate from the emitter's thread.
 * @code
 * // Fire-and-forget a signal emission:
 * vnm::post_emit(this, &MyClass::data_ready, 42, "hello");
 *
 * // Atomically emit a batch of signals from the emitter's thread:
 * vnm::post_emit_batch(this, [](MyClass* emitter) {
 *     emitter->first_signal(1);
 *     emitter->second_signal("foo");
 *     emitter->third_signal(true);
 * });
 * @endcode
 *
 * @section main_features Main Features & Requirements
 * - Requires the target object's thread to have a running event loop for queued/blocking calls.
 * - No `Q_ARG` or `QMetaType` registration is needed; arguments are marshalled via lambda capture.
 * - Arguments are copied or moved into a tuple for cross-thread calls.
 *
 * @section exceptions Exception Handling Contract
 * - `safe_invoke`, `safe_emit`: Rethrow on same thread. Queued path swallows target-thread exceptions
 *   (logs a critical error and asserts in debug builds). May throw on caller's thread during argument capture.
 * - `post_invoke`, `post_emit`, `post_emit_batch`: Never throw to the caller. Exceptions are handled internally.
 * - `blocking_invoke`: Propagates exceptions from the receiver thread to the caller.
 * - `try_blocking_invoke`: Catches all exceptions and returns an error state (`false` or empty `std::optional`).
 *
 * @section warnings Important Warnings
 * @warning **Argument Lifetimes**: Avoid passing non-owning views (e.g., `QStringView`, `std::string_view`)
 *   to queued calls (`safe_*`, `post_*`) unless the underlying data is guaranteed to outlive the call's
 *   execution on the target thread. Prefer passing owning copies.
 * @warning **Reentrancy and Deadlocks**:
 * - `safe_invoke`/`safe_emit` may execute synchronously. Be mindful of reentrancy.
 * - `blocking_invoke` will deadlock if the receiver's thread is busy or lacks a running event loop.
 * @warning **Reference Returns**: `blocking_invoke` and `try_blocking_invoke` do not support methods
 *          that return references. Return by value or smart pointer instead.
 * @warning **C++ Access Control**: Qt `signals:` are `protected`. To use `safe_emit` or `post_emit`
 *          from outside the class hierarchy, provide a public `Q_INVOKABLE` wrapper method in the emitter class.
 */

namespace vnm {
namespace detail {

template<class T> constexpr T* to_raw(T* p) noexcept { return p; }
template<class T> constexpr T* to_raw(QPointer<T> const& p) noexcept { return p.data(); }
template<class T> constexpr T* to_raw(QPointer<T>* p) noexcept { return p ? p->data() : nullptr; }
template<class T> constexpr T* to_raw(QPointer<T> const* p) noexcept { return p ? p->data() : nullptr; }

template<class M> struct member_class;

#ifndef VNM_NODISCARD
#  ifdef VNM_STRICT_API   // define in CI or debug-only if you like
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

// member_class_t<Method> yields the class `C` with the method's cv/ref qualifiers.
template<class Method> using member_class_t = typename member_class<Method>::type;

// this_ptr_t<Method> yields the corresponding `C*` (with cv qualifiers)
// expected by the member function pointer.
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


//================================================================================================//
//                                    METHOD INVOCATION
//================================================================================================//

/**
 * @brief Invokes a method directly if on the same thread, otherwise queues it.
 * @return True if the call was invoked directly or successfully queued.
 * @return False if the receiver object was null at the time of the call.
 */
template<class ObjLike, class Method, class... Args>
VNM_NODISCARD bool safe_invoke(ObjLike&& obj_like, Method method, Args&&... args)
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

    if (QThread::currentThread() == raw_cv->thread()) {
        std::invoke(method, static_cast<this_ptr>(raw_cv), std::forward<Args>(args)...);
        return true;
    }

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
#ifdef QT_DEBUG
                qCritical("safe_invoke: Unhandled exception in target thread is a critical bug.");
                Q_ASSERT_X(false, "vnm::safe_invoke", "Exception escaped target thread");
#else
                qWarning("safe_invoke: Exception swallowed in queued call.");
#endif
            }
        },
        Qt::QueuedConnection
    );
}


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


/**
 * @brief Invokes a method and blocks until it completes, returning the result.
 * @return The result of the invoked method.
 * @throws std::runtime_error on failure to invoke, or rethrows any exception from the target method.
 */
template<class ObjLike, class Method, class... Args>
auto blocking_invoke(ObjLike&& obj_like, Method method, Args&&... args)
    -> std::invoke_result_t<Method, detail::this_ptr_t<Method>, Args...>
{
    auto* raw_cv = detail::to_raw(std::forward<ObjLike>(obj_like));
    if (!raw_cv) {
        throw std::runtime_error("blocking_invoke: Receiver object is null.");
    }

    using raw_type_cv = std::remove_pointer_t<decltype(raw_cv)>;
    using base_type   = std::remove_cv_t<raw_type_cv>;
    using this_ptr    = detail::this_ptr_t<Method>;
    using R         = std::invoke_result_t<Method, this_ptr, Args...>;

    static_assert(std::is_member_function_pointer_v<Method>,
        "Method must be a pointer-to-member function");
    static_assert(std::is_base_of_v<QObject, base_type>,
        "Receiver must inherit QObject");
    static_assert(std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<this_ptr>>, base_type>,
        "Method not in object's class hierarchy");
    static_assert(std::is_invocable_v<Method, this_ptr, Args...>,
        "Method cannot be invoked with provided arguments");
    static_assert(!std::is_reference_v<R>,
        "blocking_invoke does not support reference returns; return by value (or wrap) instead.");

    constexpr bool receiver_is_const = std::is_const_v<raw_type_cv>;
    constexpr bool method_is_const = std::is_const_v<
        std::remove_reference_t<
            detail::member_class_t<Method>
        >
    >;
    static_assert(!receiver_is_const || method_is_const,
        "Cannot invoke a non-const method on a const receiver");

    if (QThread::currentThread() == raw_cv->thread()) {
        if constexpr (std::is_void_v<R>) {
            std::invoke(method, static_cast<this_ptr>(raw_cv), std::forward<Args>(args)...);
            return;
        }
        else {
            return std::invoke(method, static_cast<this_ptr>(raw_cv), std::forward<Args>(args)...);
        }
    }

    QPointer<base_type> guard{const_cast<base_type*>(raw_cv)};
    std::exception_ptr eptr;

    if constexpr (std::is_void_v<R>) {
        bool queued = QMetaObject::invokeMethod(
            const_cast<base_type*>(raw_cv),
            [guard, method, tup = std::make_tuple(
                vnm::detail::decay_copy(std::forward<Args>(args))...), &eptr]() mutable
            {
                try {
                    auto* obj = guard.data();
                    if (!obj) {
                        throw std::runtime_error(
                            "blocking_invoke: QObject was destroyed during call."
                        );
                    }
                    std::apply(
                        [&](auto&&... u) {
                            std::invoke(
                                method,
                                static_cast<this_ptr>(obj),
                                std::forward<decltype(u)>(u)...
                            );
                        }, std::move(tup)
                    );
                }
                catch (...) {
                    eptr = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection);

        if (!queued) {
            throw std::runtime_error(
                "blocking_invoke: QMetaObject::invokeMethod failed to queue the call."
            );
        }
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        return;
    }
    else {
        std::optional<R> result;
        bool queued = QMetaObject::invokeMethod(
            const_cast<base_type*>(raw_cv),
            [guard, method, tup = std::make_tuple(
                vnm::detail::decay_copy(std::forward<Args>(args))...), &eptr, &result]() mutable
            {
                try {
                    auto* obj = guard.data();
                    if (!obj) {
                        throw std::runtime_error(
                            "blocking_invoke: QObject was destroyed during call."
                        );
                    }
                    result.emplace(std::apply(
                        [&](auto&&... u) -> R {
                            return std::invoke(
                                method,
                                static_cast<this_ptr>(obj),
                                std::forward<decltype(u)>(u)...
                            );
                        },
                        std::move(tup))
                    );
                }
                catch (...) {
                    eptr = std::current_exception();
                }
            },
            Qt::BlockingQueuedConnection
        );

        if (!queued) {
            throw std::runtime_error(
                "blocking_invoke: QMetaObject::invokeMethod failed to queue the call."
            );
        }
        if (eptr) {
            std::rethrow_exception(eptr);
        }
        return *result;
    }
}


/**
 * @brief Nothrow version of blocking_invoke.
 * @return For `void` methods, `true` on success, `false` on failure.
 * @return For non-void `R` methods, `std::optional<R>` with a value on success, empty on failure.
 */
template<class ObjLike, class Method, class... Args>
VNM_NODISCARD auto try_blocking_invoke(ObjLike&& obj_like, Method method, Args&&... args) noexcept
{
    using R = std::invoke_result_t<Method, detail::this_ptr_t<Method>, Args...>;
    static_assert(!std::is_reference_v<R>,
        "try_blocking_invoke does not support reference returns; use blocking_invoke instead.");

    try {
        if constexpr (std::is_void_v<R>) {
            blocking_invoke(std::forward<ObjLike>(obj_like), method, std::forward<Args>(args)...);
            return true;
        }
        else {
            return std::optional<R>{
                blocking_invoke(
                    std::forward<ObjLike>(obj_like),
                    method,
                    std::forward<Args>(args)...
                )
            };
        }
    }
    catch (...) {
        if constexpr (std::is_void_v<R>) {
            return false;
        }
        else {
            return std::optional<R>{};
        }
    }
}


//================================================================================================//
//                                     SIGNAL EMISSION
//================================================================================================//

/**
 * @brief Emits a signal directly if on the same thread, otherwise queues the emission.
 * @return True if the signal was emitted directly or the emission was successfully queued.
 * @return False if the emitter object was null at the time of the call.
 */
template<class EmitterLike, class Signal, class... Args>
VNM_NODISCARD bool safe_emit(EmitterLike&& emitter_like, Signal signal, Args&&... args)
{
    auto* raw_cv = detail::to_raw(std::forward<EmitterLike>(emitter_like));
    if (!raw_cv) {
        return false;
    }

    using raw_type_cv = std::remove_pointer_t<decltype(raw_cv)>;
    using base_type   = std::remove_cv_t<raw_type_cv>;
    using this_ptr    = detail::this_ptr_t<Signal>;

    static_assert(std::is_member_function_pointer_v<Signal>,
        "Signal must be a pointer-to-member function");
    static_assert(std::is_base_of_v<QObject, base_type>,
        "Emitter must inherit QObject");
    static_assert(std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<this_ptr>>, base_type>,
        "Signal is not in the emitter's class hierarchy");
    static_assert(std::is_invocable_v<Signal, this_ptr, Args...>,
        "Signal cannot be invoked with the provided arguments");
    static_assert(std::is_void_v<std::invoke_result_t<Signal, this_ptr, Args...>>,
        "Method passed must return void");
    static_assert(((std::is_copy_constructible_v<std::decay_t<Args>>
                    || std::is_rvalue_reference_v<Args&&>) && ...),
        "Each lvalue argument must be copy-constructible; pass rvalues (std::move) for move-only types.");

    constexpr bool emitter_is_const = std::is_const_v<raw_type_cv>;
    constexpr bool signal_is_const  = std::is_const_v<
        std::remove_reference_t<
            detail::member_class_t<Signal>
        >
    >;
    static_assert(!emitter_is_const || signal_is_const,
        "Cannot use a non-const signal/wrapper on a const emitter");

    if (QThread::currentThread() == raw_cv->thread()) {
        std::invoke(
            signal,
            static_cast<this_ptr>(raw_cv),
            std::forward<Args>(args)...
        );
        return true;
    }

    QPointer<base_type> guard{const_cast<base_type*>(raw_cv)};
    return QMetaObject::invokeMethod(
        const_cast<base_type*>(raw_cv),
        [guard, signal, tup = std::make_tuple(
            vnm::detail::decay_copy(std::forward<Args>(args))...)]() mutable
        {
            auto* obj = guard.data();
            if (!obj) {
                return;
            }
            try {
                std::apply(
                    [&](auto&&... u) {
                        std::invoke(
                            signal,
                            static_cast<this_ptr>(obj),
                            std::forward<decltype(u)>(u)...
                        );
                    },
                    std::move(tup)
                );
            }
            catch (...) {
#ifdef QT_DEBUG
                qCritical("safe_emit: Unhandled exception during queued emission. "
                    "Signals/wrappers must not throw.");
                Q_ASSERT_X(false, "vnm::safe_emit", "Exception escaped target thread");
#else
                qWarning("safe_emit: Exception swallowed during queued emission. "
                    "Signals/wrappers should not throw.");
#endif
            }
        },
        Qt::QueuedConnection
    );
}

/**
 * @brief Posts a signal for asynchronous emission (always queued). "Fire-and-forget".
 * @return True if the emission was successfully queued.
 * @return False if the emitter was null or if argument capture failed.
 */
template<class EmitterLike, class Signal, class... Args>
VNM_NODISCARD bool post_emit(EmitterLike&& emitter_like, Signal signal, Args&&... args) noexcept
{
    auto* raw_cv = detail::to_raw(std::forward<EmitterLike>(emitter_like));
    if (!raw_cv) {
        return false;
    }

    using raw_type_cv = std::remove_pointer_t<decltype(raw_cv)>;
    using base_type   = std::remove_cv_t<raw_type_cv>;
    using this_ptr    = detail::this_ptr_t<Signal>;

    static_assert(std::is_member_function_pointer_v<Signal>,
        "Signal must be a pointer-to-member function");
    static_assert(std::is_base_of_v<QObject, base_type>,
        "Emitter must inherit QObject");
    static_assert(std::is_base_of_v<std::remove_cv_t<std::remove_pointer_t<this_ptr>>, base_type>,
        "Signal is not in the emitter's class hierarchy");
    static_assert(std::is_invocable_v<Signal, this_ptr, Args...>,
        "Signal cannot be invoked with the provided arguments");
    static_assert(std::is_void_v<std::invoke_result_t<Signal, this_ptr, Args...>>,
        "Method passed must return void");

    constexpr bool emitter_is_const = std::is_const_v<raw_type_cv>;
    constexpr bool signal_is_const  = std::is_const_v<
        std::remove_reference_t<
            detail::member_class_t<Signal>
        >
    >;
    static_assert(!emitter_is_const || signal_is_const,
        "Cannot use a non-const signal/wrapper on a const emitter");
    static_assert(((std::is_copy_constructible_v<std::decay_t<Args>>
                    || std::is_rvalue_reference_v<Args&&>) && ...),
        "Each lvalue argument must be copy-constructible; pass rvalues (std::move) for move-only types.");

    try {
        QPointer<base_type> guard{const_cast<base_type*>(raw_cv)};
        return QMetaObject::invokeMethod(
            const_cast<base_type*>(raw_cv),
            [guard, signal, tup = std::make_tuple(
                vnm::detail::decay_copy(std::forward<Args>(args))...)]() mutable
            {
                auto* obj = guard.data();
                if (!obj) {
                    return;
                }
                try {
                    std::apply(
                        [&](auto&&... u) {
                            std::invoke(
                                signal,
                                static_cast<this_ptr>(obj),
                                std::forward<decltype(u)>(u)...
                            );
                        },
                        std::move(tup)
                    );
                }
                catch (...) {
                    qWarning("post_emit: Exception swallowed during queued emission. "
                        "Signals/wrappers should not throw.");
                }
            },
            Qt::QueuedConnection
        );
    }
    catch (...) {
        return false;
    }
}


/**
 * @brief Posts a functor to be executed on the emitter's thread for batch emissions.
 * @return True if the batch functor was successfully queued.
 * @return False if the emitter was null or the functor could not be captured.
 */
template<class EmitterLike, class Functor>
VNM_NODISCARD bool post_emit_batch(EmitterLike&& emitter_like, Functor&& f) noexcept
{
    auto* raw_cv = detail::to_raw(std::forward<EmitterLike>(emitter_like));
    if (!raw_cv) {
        return false;
    }

    using raw_type_cv = std::remove_pointer_t<decltype(raw_cv)>;
    using base_type   = std::remove_cv_t<raw_type_cv>;
    using functor_t   = std::decay_t<Functor>;

    static_assert(std::is_base_of_v<QObject, base_type>, "Emitter must inherit QObject");

    if constexpr (std::is_const_v<raw_type_cv>) {
        static_assert(std::is_invocable_v<functor_t, const base_type*>,
            "For a const emitter, the functor must accept (const base_type*).");
        static_assert(std::is_invocable_r_v<void, functor_t, const base_type*>,
            "The batch functor must return void.");
    }
    else {
        static_assert(std::is_invocable_v<functor_t, base_type*>,
            "Functor must be invocable with (base_type*).");
        static_assert(std::is_invocable_r_v<void, functor_t, base_type*>,
            "The batch functor must return void.");
    }

    static_assert(std::is_copy_constructible_v<functor_t> || std::is_rvalue_reference_v<Functor&&>,
        "Lvalue functors must be copy-constructible; pass rvalues (std::move) for move-only functors.");

    try {
        QPointer<base_type> guard{const_cast<base_type*>(raw_cv)};
        return QMetaObject::invokeMethod(
            const_cast<base_type*>(raw_cv),
            [guard, fn = std::forward<Functor>(f)]() mutable {
                auto* obj = guard.data();
                if (!obj) {
                    return;
                }
                try {
                    if constexpr (std::is_const_v<raw_type_cv>) {
                        fn(static_cast<const base_type*>(obj));
                    }
                    else {
                        fn(obj);
                    }
                }
                catch (...) {
                    qWarning("post_emit_batch: Exception swallowed during queued emission. "
                        "The batch functor should not throw.");
                }
            },
            Qt::QueuedConnection
        );
    }
    catch (...) {
        return false;
    }
}

// ============================================================================
// CONVENIENCE OVERLOADS FOR LVALUES
// (Only enabled when the lvalue type itself is NOT a pointer.
//  This avoids taking the address of a pointer (T* -> T**), which breaks
//  the pointer-traits used above.)
// ============================================================================

template<class Obj, class Method, class... Args,
         std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Obj>>, int> = 0>
VNM_NODISCARD bool post_invoke(Obj& o, Method m, Args&&... args)
{
    return vnm::post_invoke(std::addressof(o), m, std::forward<Args>(args)...);
}

template<class Obj, class Method, class... Args,
         std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Obj>>, int> = 0>
decltype(auto) blocking_invoke(Obj& o, Method m, Args&&... args)
{
    using Ret = decltype(vnm::blocking_invoke(std::addressof(o), m, std::forward<Args>(args)...));
    if constexpr (std::is_void_v<Ret>) {
        vnm::blocking_invoke(std::addressof(o), m, std::forward<Args>(args)...);
    }
    else {
        return vnm::blocking_invoke(std::addressof(o), m, std::forward<Args>(args)...);
    }
}

template<class Obj, class Method, class... Args,
         std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Obj>>, int> = 0>
VNM_NODISCARD auto try_blocking_invoke(Obj& o, Method m, Args&&... args) noexcept
    -> decltype(vnm::try_blocking_invoke(std::addressof(o), m, std::forward<Args>(args)...))
{
    return vnm::try_blocking_invoke(std::addressof(o), m, std::forward<Args>(args)...);
}

// --- Pointer lvalue overloads (handle raw T* variables safely) ---------

template<class Ptr, class Method, class... Args,
         std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD bool post_invoke(Ptr& p, Method m, Args&&... args)
{
    if (!p) {
        return false;
    }
    return vnm::post_invoke(*p, m, std::forward<Args>(args)...);
}

template<class Ptr, class Method, class... Args,
         std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
decltype(auto) blocking_invoke(Ptr& p, Method m, Args&&... args)
{
    using Ret = decltype(vnm::blocking_invoke(*p, m, std::forward<Args>(args)...));
    if (!p) {
        if constexpr (std::is_void_v<Ret>) {
            return;
        }
        else {
            return Ret{};
        }
    }
    if constexpr (std::is_void_v<Ret>) {
        vnm::blocking_invoke(*p, m, std::forward<Args>(args)...);
    }
    else {
        return vnm::blocking_invoke(*p, m, std::forward<Args>(args)...);
    }
}

template<class Ptr, class Method, class... Args,
    std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD auto try_blocking_invoke(Ptr& p, Method m, Args&&... args) noexcept
    -> decltype(vnm::try_blocking_invoke(*p, m, std::forward<Args>(args)...))
{
    using return_t = decltype(vnm::try_blocking_invoke(*p, m, std::forward<Args>(args)...));
    if (!p) {
        if constexpr (std::is_same_v<return_t, bool>) {
            return false;
        }
        else {
            return return_t{};
        }
    }
    return vnm::try_blocking_invoke(*p, m, std::forward<Args>(args)...);
}

// --- Emission ---

template<class Emitter, class Signal, class... Args,
    std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Emitter>>, int> = 0>
VNM_NODISCARD bool safe_emit(Emitter& e, Signal s, Args&&... args)
{
    return vnm::safe_emit(std::addressof(e), s, std::forward<Args>(args)...);
}

template<class Emitter, class Signal, class... Args,
    std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Emitter>>, int> = 0>
VNM_NODISCARD bool post_emit(Emitter& e, Signal s, Args&&... args) noexcept
{
    return vnm::post_emit(std::addressof(e), s, std::forward<Args>(args)...);
}

template<class Emitter, class Functor,
         std::enable_if_t<!std::is_pointer_v<std::remove_reference_t<Emitter>>, int> = 0>
VNM_NODISCARD bool post_emit_batch(Emitter& e, Functor&& f) noexcept
{
    return vnm::post_emit_batch(std::addressof(e), std::forward<Functor>(f));
}

// --- Emission (raw pointer variables) ----------------------------------

template<class Ptr, class Signal, class... Args,
    std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD bool safe_emit(Ptr& p, Signal s, Args&&... args)
{
    if (!p) {
        return false;
    }
    return vnm::safe_emit(*p, s, std::forward<Args>(args)...);
}

template<class Ptr, class Signal, class... Args,
    std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD bool post_emit(Ptr& p, Signal s, Args&&... args) noexcept
{
    if (!p) {
        return false;
    }
    return vnm::post_emit(*p, s, std::forward<Args>(args)...);
}

template<class Ptr, class Functor,
    std::enable_if_t<std::is_pointer_v<std::remove_reference_t<Ptr>>, int> = 0>
VNM_NODISCARD bool post_emit_batch(Ptr& p, Functor&& f) noexcept {
    if (!p) {
        return false;
    }
    return vnm::post_emit_batch(*p, std::forward<Functor>(f));
}

} // namespace vnm
