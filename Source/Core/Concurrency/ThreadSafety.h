/**
 * @file ThreadSafety.h
 * @brief 잠금 계약을 컴파일 때 검사하게 하는 주석 매크로입니다(clang `-Wthread-safety`). clang 이 아니면 빈 값입니다.
 * @details 주석에 적던 "이 멤버는 `_mutex` 아래" · "`_mutex` 를 쥔 채 부른다" 를 선언에 붙이면, 잠그지 않고 읽거나 쓰는 줄이 경고가 된다.
 *          잠금 타입(`sw::mutex` · `sw::shared_mutex` · `SpinLock`)은 `SW_CAPABILITY` 이고, 그 아래 멤버는 `SW_GUARDED_BY`,
 *          쥔 채 부를 함수는 `SW_REQUIRES_LOCK`, 쥐지 않은 채 부를 함수(안에서 잠근다)는 `SW_EXCLUDES_LOCK` 다. 분석은 주석이 달린 잠금 가드
 *          (`sw::ScopedLock` · `sw::ScopedSharedLock`)만 알아본다 — `std::scoped_lock` 은 표준 라이브러리에 주석이 없어 분석이 보지 못한다.
 *          언리얼은 같은 매크로를 두지 않고 주석으로 적으며, 크로미움 · LLVM · Abseil 이 같은 clang 속성을 매크로로 감싼다(`GUARDED_BY` · `EXCLUSIVE_LOCKS_REQUIRED`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/atomic.h"

#include <thread>

#if defined( SW_COMPILER_CLANG )
    #define SW_THREAD_ANNOTATION_INTERNAL( attribute ) __attribute__( ( attribute ) )
#else
    #define SW_THREAD_ANNOTATION_INTERNAL( attribute )
#endif

/** @brief 이 타입이 잠금(능력)임을 적습니다. @p name 은 경고에 나오는 종류 이름입니다. */
#define SW_CAPABILITY( name ) SW_THREAD_ANNOTATION_INTERNAL( capability( name ) )
/** @brief 생성자에서 잠그고 소멸자에서 푸는 가드 타입임을 적습니다. */
#define SW_SCOPED_CAPABILITY SW_THREAD_ANNOTATION_INTERNAL( scoped_lockable )

/** @brief 이 멤버는 잠금 @p lock 을 쥐어야 읽고 쓸 수 있습니다. */
#define SW_GUARDED_BY( lock ) SW_THREAD_ANNOTATION_INTERNAL( guarded_by( lock ) )
/**
 * @brief 이 잠금은 @p ... 를 쥔 뒤에만 잡습니다(잠금 순서). 순서 검사는 clang 빌드에 켠 `-Wthread-safety-beta` 가 한다 —
 *        거꾸로 잡으면 컴파일 경고다.
 */
#define SW_ACQUIRED_AFTER( ... ) SW_THREAD_ANNOTATION_INTERNAL( acquired_after( __VA_ARGS__ ) )

/** @brief 이 함수는 잠금을 독점으로 쥔 채 부릅니다. */
#define SW_REQUIRES_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( requires_capability( __VA_ARGS__ ) )
/** @brief 이 함수는 잠금을 공유(읽기)로라도 쥔 채 부릅니다. */
#define SW_REQUIRES_SHARED_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( requires_shared_capability( __VA_ARGS__ ) )
/** @brief 이 함수는 잠금을 쥐지 않은 채 부릅니다(안에서 잠근다 — 쥔 채 부르면 재진입으로 멈춘다). */
#define SW_EXCLUDES_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( locks_excluded( __VA_ARGS__ ) )

/** @brief 이 함수가 잠금을 독점으로 얻습니다(인자가 없으면 `this`). */
#define SW_ACQUIRE_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( acquire_capability( __VA_ARGS__ ) )
/** @brief 이 함수가 잠금을 공유로 얻습니다. */
#define SW_ACQUIRE_SHARED_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( acquire_shared_capability( __VA_ARGS__ ) )
/** @brief 이 함수가 독점으로 쥔 잠금을 풉니다. 가드의 소멸자는 공유로 얻은 것도 이것으로 푼다. */
#define SW_RELEASE_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( release_capability( __VA_ARGS__ ) )
/** @brief 이 함수가 공유로 쥔 잠금을 풉니다. */
#define SW_RELEASE_SHARED_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( release_shared_capability( __VA_ARGS__ ) )
/** @brief 이 함수가 @p result 를 돌려주면 잠금을 독점으로 얻은 것입니다. */
#define SW_TRY_ACQUIRE_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( try_acquire_capability( __VA_ARGS__ ) )
/** @brief 이 함수가 @p result 를 돌려주면 잠금을 공유로 얻은 것입니다. */
#define SW_TRY_ACQUIRE_SHARED_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( try_acquire_shared_capability( __VA_ARGS__ ) )

/** @brief 이 함수가 돌아오면 부른 쪽이 그 능력을 쥔 것으로 봅니다(검사가 아니라 선언 — 스레드 역할의 진입점에 쓴다). */
#define SW_ASSERT_CAPABILITY_LOCK( ... ) SW_THREAD_ANNOTATION_INTERNAL( assert_capability( __VA_ARGS__ ) )

/** @brief 이 함수 본문은 분석하지 않습니다. 잠금 타입 자신의 구현(주석 없는 원시 잠금을 부른다)에만 씁니다 — 다른 곳에 쓰면 이유를 한 줄 적는다. */
#define SW_NO_THREAD_SAFETY_ANALYSIS SW_THREAD_ANNOTATION_INTERNAL( no_thread_safety_analysis )

namespace sw
{
    /**
     * @class ThreadRole
     * @brief 스레드 역할("이 목록은 스텝 스레드만 만진다")을 분석이 보는 능력으로 적는 가짜 잠금입니다. 잠그지 않는다.
     * @details 역할 아래 멤버는 `SW_GUARDED_BY( _role )`, 그 역할에서만 부르는 함수는 `SW_REQUIRES_LOCK( _role )` 이고, 역할이 시작되는 진입점이
     *          `assumeHeld()` 를 부른다 — 역할 밖 스레드에서 만지는 코드가 컴파일 경고가 된다. 크로미움 `SequenceChecker`(`GUARDED_BY_CONTEXT` + DCHECK)와 같은 자리다.
     *          **Debug 는 실제 스레드를 확인한다.** 소유 스레드는 시작 지점이 `bindToCurrentThread` 로 정하고(스레드를 다시 세우면 다시 정한다),
     *          정하지 않았으면 처음 `assumeHeld` 를 부른 스레드가 소유자가 된다. 그 뒤 다른 스레드의 `assumeHeld` 는 단언이다. 소유 스레드가 끝나면
     *          `unbind` 로 놓아 다음 소유자(종료를 마무리하는 스레드)가 받게 한다. Debug 가 아니면 멤버도 비용도 없다.
     *          소유 스레드가 합류를 기다리는 동안 대신 도는 작업(병렬 기록 태스크)은 `assumeHeldByJoinedTask` 로 적는다 — 스레드 id 가 달라 확인하지 않는다.
     */
    class SW_CAPABILITY( "thread role" ) ThreadRole
    {
    public:
        /** @brief 이 스레드를 역할의 소유자로 정합니다(역할이 시작되는 지점 — 스레드 시작 · 다시 세움). */
        void bindToCurrentThread() { bindToThread( std::this_thread::get_id() ); }

        /** @brief @p owner 스레드를 역할의 소유자로 정합니다(다른 스레드가 소유 스레드를 띄우며 정할 때). */
        void bindToThread( std::thread::id owner )
        {
#if defined( SW_DEBUG )
            _owner.store( owner, std::memory_order_release );
#else
            (void)owner;
#endif
        }

        /** @brief 소유자를 놓습니다(소유 스레드가 끝날 때). 다음 `assumeHeld` 를 부른 스레드가 소유자가 됩니다. */
        void unbind()
        {
#if defined( SW_DEBUG )
            _owner.store( std::thread::id{}, std::memory_order_release );
#endif
        }

        /** @brief 이 스레드가 역할을 맡은 진입점임을 분석에 알립니다. Debug 는 소유 스레드인지 확인합니다(소유자가 없으면 이 스레드가 맡는다). */
        void assumeHeld() const SW_ASSERT_CAPABILITY_LOCK( this )
        {
#if defined( SW_DEBUG )
            const std::thread::id current = std::this_thread::get_id();
            std::thread::id       expected{};
            if ( _owner.compare_exchange_strong( expected, current, std::memory_order_acq_rel ) )
                return;
            SW_LOW_LEVEL_ASSERT( expected == current, "ThreadRole: entered from a thread that does not own the role - bind the owner where the role starts" );
#endif
        }

        /** @brief 소유 스레드가 합류를 기다리는 동안 대신 도는 작업의 진입점임을 분석에 알립니다(스레드가 달라 Debug 확인을 하지 않는다). 쓰는 자리에 그 작업이 무엇인지 적는다. */
        void assumeHeldByJoinedTask() const SW_ASSERT_CAPABILITY_LOCK( this ) {}

    private:
        friend class ScopedThreadRoleBorrow;
#if defined( SW_DEBUG )
        mutable atomic<std::thread::id> _owner{};
#endif
    };

    /**
     * @class ScopedThreadRoleBorrow
     * @brief 소유 스레드가 세워져 있는 동안(예: 렌더 스레드를 `waitIdle` 로 세운 뒤의 resize) 이 스레드가 역할을 잠깐 빌립니다. 범위가 끝나면 원래 소유자로 돌려줍니다.
     * @details 분석에는 그 범위에서 역할을 쥔 것으로 보이고, Debug 는 소유자를 이 스레드로 바꿨다가 되돌린다 — 빌린 동안 소유 스레드가 들어오면 그쪽이 단언한다.
     */
    class SW_SCOPED_CAPABILITY ScopedThreadRoleBorrow
    {
    public:
        explicit ScopedThreadRoleBorrow( ThreadRole& role ) SW_ACQUIRE_LOCK( role ) SW_NO_THREAD_SAFETY_ANALYSIS
            : _role{ role }
        {
#if defined( SW_DEBUG )
            _previous = _role._owner.exchange( std::this_thread::get_id(), std::memory_order_acq_rel );
#endif
        }

        ~ScopedThreadRoleBorrow() SW_RELEASE_LOCK() SW_NO_THREAD_SAFETY_ANALYSIS
        {
#if defined( SW_DEBUG )
            _role._owner.store( _previous, std::memory_order_release );
#endif
        }

        ScopedThreadRoleBorrow( const ScopedThreadRoleBorrow& )            = delete;
        ScopedThreadRoleBorrow& operator=( const ScopedThreadRoleBorrow& ) = delete;

    private:
        ThreadRole& _role;
#if defined( SW_DEBUG )
        std::thread::id _previous{};
#endif
    };
} // namespace sw
