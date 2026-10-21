/**
 * @file LifetimeScope.h
 * @brief 소유 범위(`LifetimeScope`)와 그 범위가 닫히면 함께 사라지는 지연 생성 값(`ScopeOwned<T>`)입니다.
 * @details 프로세스 정적 저장소는 소유자만 선언하고, 비우는 함수를 따로 두지 않습니다. 값은 처음 쓸 때 만들어져 범위에 등록되고,
 *          범위가 닫히면 만든 역순으로 소멸합니다. 전역 범위(`LifetimeScope::getGlobal`)는 엔진 종료(`StaticStorageSubsystem::shutdownSubsystem`)가 닫으므로,
 *          엔진이 기동한 뒤에 자란 저장소가 종료 누수 보고 전에 돌아갑니다. 엔진을 세우지 않는 프로세스(Core 테스트)에서는 값이 프로세스 끝에 소멸합니다.
 *
 *          종료 때만 부르는 반납 함수(`releaseXxx`)를 저장소마다 두지 않는 것이 이 파일의 목적입니다. 모양별로 푸는 곳은 셋입니다.
 *          - 소유자 없는 정적 저장소(캐시 · 기록부 · 트랜스폼 페이지): `ScopeOwned<T>` 로 엔진 수명 범위가 소유한다.
 *          - 모듈이 등록하는 레지스트리: `RegistrationList::remove` 가 뺄 때마다 용량을 남은 수에 맞춘다(모듈이 내려가면 용량도 돌아간다).
 *          - 스레드 지역 스크래치 · 엔진보다 오래 사는 객체의 힙(창 제목): 그리는 쪽이 소유하거나(`UIPaintScratch`) 고정 버퍼로 바꾼다(`IWindow::_arrTitle`).
 *          모듈마다의 범위는 두지 않는다 — 모듈 DLL 의 `ScopeOwned` 는 DLL 언로드 때 스스로 소멸해 같은 효과다(범위에서 빠지고 값을 소멸시킨다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct LifetimeScopeEntry
     * @brief 범위에 등록된 값 하나의 연결 고리입니다. `ScopeOwned<T>` 가 상속하고, 범위는 이 고리만 압니다(침입형 목록이라 등록에 할당이 없습니다).
     */
    struct LifetimeScopeEntry
    {
        LifetimeScopeEntry* _pNext;                                     ///< 범위 목록의 다음 고리(나중에 등록한 것이 앞)
        void ( *_pDestroyValue )( LifetimeScopeEntry& entry ) noexcept; ///< 값을 소멸시키는 함수(`ScopeOwned<T>::destroyValue`)

        /** @brief 아직 어느 범위에도 등록되지 않은 고리입니다. */
        constexpr explicit LifetimeScopeEntry( void ( *pDestroyValue )( LifetimeScopeEntry& entry ) noexcept )
            : _pNext{ nullptr }
            , _pDestroyValue{ pDestroyValue }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class LifetimeScope
     * @brief 값의 소유 범위입니다. 등록된 값을 `close` 가 등록의 역순으로 소멸시킵니다.
     * @details 상수 초기화되고 소멸자가 하는 일이 없어, 전역 범위는 프로세스 끝까지(다른 정적 객체의 소멸자 안에서도) 쓸 수 있습니다.
     *          스레드: 등록 · 해제 · 닫기는 스핀락으로 지킵니다. 닫기와 값 생성이 동시에 일어나는 것은 지원하지 않습니다(닫기는 종료 끝에 한 번).
     */
    class SW_API LifetimeScope
    {
    public:
        constexpr LifetimeScope()
            : _pHead{ nullptr }
            , _lock{}
            , _bClosed{ false }
        {
        }

        /** @brief 복사를 금지합니다. */
        LifetimeScope( const LifetimeScope& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        LifetimeScope& operator=( const LifetimeScope& ) = delete;

        /** @brief 엔진 수명 범위입니다. 엔진 종료(`StaticStorageSubsystem::shutdownSubsystem`)가 닫습니다. */
        static LifetimeScope& getGlobal();

        /**
         * @brief 범위를 닫습니다. 등록된 값을 등록의 역순으로 소멸시키고, 이 뒤의 등록은 거절합니다.
         * @note Debug 에서 두 번 닫으면 단언합니다(두 번째는 아무것도 하지 않습니다).
         */
        void close();

        /**
         * @brief @p entry 를 범위에 등록합니다. 닫힌 범위면 거절합니다.
         * @return 등록했으면 true 입니다.
         */
        [[nodiscard]] bool attach( LifetimeScopeEntry& entry );
        /** @brief @p entry 를 목록에서 뺍니다. 없으면 아무것도 하지 않습니다(값이 범위보다 먼저 소멸할 때 — 모듈 언로드). */
        void detach( LifetimeScopeEntry& entry );

        /** @brief 닫혔는지입니다. */
        bool isClosed() const { return _bClosed.load( std::memory_order_acquire ); }

    private:
        LifetimeScopeEntry* _pHead;   ///< 등록된 고리(나중에 등록한 것이 앞이라 앞에서부터 소멸시키면 역순이다)
        SpinLock            _lock;    ///< `_pHead` · `_bClosed` 를 지킨다
        atomic<bool>        _bClosed; ///< `close` 뒤 true
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScopeOwned
     * @brief 처음 쓸 때 만들어지고, 소유 범위가 닫히면 소멸하는 값 하나입니다. 비우는 함수를 따로 두지 않습니다.
     * @details 상수 초기화되므로 익명 네임스페이스의 정적 변수로 두어도 정적 초기화 순서를 타지 않습니다. 만들 때의 메모리 태그는 처음 부른 쪽의 것이고,
     *          소멸하면 그 태그에서 빠집니다. 값이 모듈(DLL)에 있으면 모듈 언로드가 범위보다 먼저 이 객체를 소멸시키고, 그때 값도 함께 소멸합니다.
     *          스레드: `getOrCreate` · `find` 는 어느 스레드에서나 부릅니다. 값의 생성자는 생성 락을 잡은 채 돌므로 짧아야 하고, 같은 값을 다시 부르면 안 됩니다.
     * @tparam T 기본 생성할 수 있는 값입니다. 생성자가 private 이면 `friend class ScopeOwned<T>;` 를 둡니다.
     */
    template <class T>
    class ScopeOwned : private LifetimeScopeEntry
    {
    public:
        /** @brief @p pScope 가 소유하는 빈 자리입니다. nullptr 이면 전역 범위(`LifetimeScope::getGlobal`)입니다. */
        constexpr explicit ScopeOwned( LifetimeScope* pScope = nullptr )
            : LifetimeScopeEntry{ &ScopeOwned::destroyValue }
            , _pScope{ pScope }
            , _pValue{ nullptr }
            , _createLock{}
        {
        }

        /** @brief 범위가 아직 닫히지 않았으면 범위에서 빠지고 값을 소멸시킵니다(프로세스 끝 · 모듈 언로드). */
        ~ScopeOwned()
        {
            T* const pValue = _pValue.exchange( nullptr, std::memory_order_acq_rel );
            if ( pValue == nullptr )
                return;
            resolveScope().detach( *this );
            sw_delete( pValue );
        }

        /** @brief 복사를 금지합니다. */
        ScopeOwned( const ScopeOwned& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ScopeOwned& operator=( const ScopeOwned& ) = delete;

        /**
         * @brief 값을 돌려주고, 없으면 만들어 범위에 등록합니다.
         * @return 범위가 이미 닫혔으면 nullptr 입니다(Debug 에서 단언 — 소유 범위가 끝난 뒤의 접근이다).
         */
        T* getOrCreate()
        {
            T* pValue = _pValue.load( std::memory_order_acquire );
            if ( pValue != nullptr )
                return pValue;

            std::lock_guard<SpinLock> lock( _createLock );
            pValue = _pValue.load( std::memory_order_acquire );
            if ( pValue != nullptr )
                return pValue;
            const bool bAttached = resolveScope().attach( *this );
            SW_ASSERT( bAttached );
            if ( bAttached == false )
                return nullptr;
            pValue = sw_new T();
            _pValue.store( pValue, std::memory_order_release );
            return pValue;
        }

        /** @brief 만들어진 값입니다. 아직 없거나 범위가 닫혔으면 nullptr 입니다(만들지 않습니다). */
        T* find() const { return _pValue.load( std::memory_order_acquire ); }

    private:
        /** @brief 범위가 닫힐 때 불립니다. 값을 소멸시키고 자리를 비웁니다. */
        static void destroyValue( LifetimeScopeEntry& entry ) noexcept
        {
            ScopeOwned& owned = static_cast<ScopeOwned&>( entry );
            sw_delete( owned._pValue.exchange( nullptr, std::memory_order_acq_rel ) );
        }

        LifetimeScope& resolveScope() const { return _pScope != nullptr ? *_pScope : LifetimeScope::getGlobal(); }

    private:
        LifetimeScope* _pScope;     ///< 소유 범위. nullptr 이면 전역 범위
        atomic<T*>     _pValue;     ///< 만든 값. 범위가 닫히면 nullptr 로 돌아간다
        SpinLock       _createLock; ///< 생성을 한 번으로 줄인다
    };
} // namespace sw
