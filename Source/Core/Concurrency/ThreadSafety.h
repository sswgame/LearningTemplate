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
     * @brief 스레드 역할("이 목록은 스텝 스레드만 만진다")을 분석이 보는 능력으로 적는 가짜 잠금입니다. 잠그지 않고 크기도 비용도 없다.
     * @details 역할 아래 멤버는 `SW_GUARDED_BY( _role )`, 그 역할에서만 부르는 함수는 `SW_REQUIRES_LOCK( _role )` 이고, 역할이 시작되는 진입점
     *          (그 스레드에서만 불리는 콜백)이 `assumeHeld()` 를 부른다 — 역할 밖 스레드에서 만지는 코드가 컴파일 경고가 된다. 크로미움 `SequenceChecker` 의
     *          분석 판(`GUARDED_BY_CONTEXT`)과 같은 자리다.
     */
    class SW_CAPABILITY( "thread role" ) ThreadRole
    {
    public:
        /** @brief 이 스레드가 역할을 맡은 진입점임을 분석에 알립니다(실행 때 검사는 하지 않는다). */
        void assumeHeld() const SW_ASSERT_CAPABILITY_LOCK( this ) {}
    };
} // namespace sw
