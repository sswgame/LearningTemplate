/**
 * @file Delegate.h
 * @brief 타입 안전 델리게이트 · 멀티캐스트 델리게이트 · 핸들입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 전방 선언 — 실제 구현은 아래 두 템플릿의 시그니처별 특수화에 있다
    // ------------------------------------------------------------------------------
    /** @brief 대상 하나를 붙이는 콜백입니다(함수 · 멤버 함수 · 람다). */
    template <typename T>
    class Delegate;
    /** @brief 여러 Delegate 를 모아 한꺼번에 호출(broadcast)합니다. */
    template <typename T>
    class MulticastDelegate;

    // ------------------------------------------------------------------------------
    // 2) Delegate — create / operator() / isBound. 람다는 SBO 또는 힙에 둔다
    // ------------------------------------------------------------------------------
    /** @brief 호출할 대상을 하나 붙입니다. */
    template <typename R, typename... Args>
    class Delegate<R( Args... )>
    {
    public:
        /** @brief 람다 저장소의 복사 · 이동 · 파괴 연산 종류입니다. */
        enum class DelegateManagerOp
        {
            Copy,
            Move,
            Destroy
        };

        using manager_function                    = void* (*)( DelegateManagerOp, void*, const void* );
        static constexpr size_t kInlineBufferSize = 24;

        /** @brief 빈 델리게이트를 만듭니다. */
        Delegate() = default;

        /** @brief nullptr 로 빈 델리게이트를 만듭니다. */
        Delegate( std::nullptr_t ) {}

        /** @brief 바인딩과 람다 저장소를 해제합니다. */
        ~Delegate() { release(); }

        /** @brief 대상과 람다 저장소를 복제합니다. */
        Delegate( const Delegate& other ) { copyFrom( other ); }

        /** @brief 대상과 람다 저장소를 넘겨받습니다. */
        Delegate( Delegate&& other ) noexcept { moveFrom( std::move( other ) ); }

        /** @brief 복사 대입합니다. */
        // 자기 대입은 `this != &other` 로 막는다. release() 가 먼저 돌기 때문에 이 가드가 없으면 자기 저장소를 해제한 뒤
        // 읽게 된다. copy-and-swap 이 아니라서 검사기가 짚지만, 이 경우엔 가드가 맞다.
        // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
        Delegate& operator=( const Delegate& other )
        {
            if ( this != &other )
            {
                release();
                copyFrom( other );
            }
            return *this;
        }

        /** @brief 이동 대입합니다. */
        Delegate& operator=( Delegate&& other ) noexcept
        {
            if ( this != &other )
            {
                release();
                moveFrom( std::move( other ) );
            }
            return *this;
        }

        /** @brief 정적 함수 포인터로 델리게이트를 만듭니다. */
        Delegate( R ( *func )( Args... ) ) { *this = create( func ); }

        /**
         * @brief 람다로 델리게이트를 만듭니다.
         * @note 자주 불리는 이벤트(예: 매 프레임 발생하는 Tick)에는 람다 대신 멤버 함수 바인딩(create<Method>)을 쓰십시오.
         */
        template <typename Lambda, typename = std::enable_if_t<std::is_same_v<std::decay_t<Lambda>, Delegate> == false && std::is_invocable_r_v<R, Lambda, Args...>>>
        Delegate( const Lambda& lambdaFunc ) { *this = create( lambdaFunc ); }

        /**
         * @brief 두 델리게이트가 같은 대상을 가리키는지 비교합니다.
         * @note 람다로 만든 Delegate 는 복사하면 서로 다른 인스턴스로 취급되어 항상 false 입니다. 그래서 MulticastDelegate 에서
         *       람다를 제거할 때는 반드시 DelegateHandle 을 써야 합니다.
         */
        bool operator==( const Delegate& other ) const
        {
            if ( _stubFunc != other._stubFunc )
                return false;
            if ( _managerFunc != other._managerFunc )
                return false;
            return _pInstance == other._pInstance;
        }

        /** @brief 다른지 비교합니다. */
        bool operator!=( const Delegate& other ) const { return ( *this == other ) == false; }
        /** @brief 바인딩이 없으면 true 입니다. */
        bool operator==( std::nullptr_t ) const { return _stubFunc == nullptr; }
        /** @brief 바인딩이 있으면 true 입니다. */
        bool operator!=( std::nullptr_t ) const { return _stubFunc != nullptr; }

        /** @brief 호출할 수 있는 상태(바인딩됨)인지 확인합니다. */
        bool isBound() const { return _stubFunc != nullptr; }

        /**
         * @brief 호출 스텁의 코드 주소입니다. 바인딩이 없으면 nullptr 입니다.
         * @details 스텁은 델리게이트를 **만든** 번역 단위에서 인스턴스화되므로, 이 주소는 그 델리게이트를 만든 모듈(DLL · SO) 안에 있습니다.
         *          핫 리로드가 내리려는 모듈이 만든 구독을 가리는 데 씁니다(`MulticastDelegate::removeCodeWithin`).
         */
        const void* getCodeAddress() const { return reinterpret_cast<const void*>( _stubFunc ); }

        /** @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는지 봅니다. 바인딩이 없으면 false 입니다. */
        bool isCodeWithin( const void* pBegin, const void* pEnd ) const
        {
            const uintptr_t code = reinterpret_cast<uintptr_t>( _stubFunc );
            return code != 0 && reinterpret_cast<uintptr_t>( pBegin ) <= code && code < reinterpret_cast<uintptr_t>( pEnd );
        }

        /** @brief 바인딩된 대상을 호출합니다. 비어 있으면 assert 합니다. */
        template <typename... UArgs, typename = std::enable_if_t<std::is_invocable_v<R( Args... ), UArgs...>>>
        R operator()( UArgs&&... args ) const
        {
            SW_ASSERT( isBound() );
            return std::invoke( _stubFunc, _pInstance, std::forward<UArgs>( args )... );
        }

        /** @brief 컴파일 타임 함수 포인터로 바인딩합니다. */
        template <auto Function, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( Function ), Args...>>>
        static Delegate create()
        {
            Delegate newDelegate{};
            newDelegate._pInstance = nullptr;
            newDelegate._stubFunc  = static_cast<stub_function>( []( const void*, Args... args ) -> R
            { return std::invoke( Function, std::forward<Args>( args )... ); } );

            return newDelegate;
        }

        /** @brief 런타임 함수 포인터로 델리게이트를 만듭니다. */
        static Delegate create( R ( *pFunc )( Args... ) )
        {
            Delegate newDelegate{};
            newDelegate._pInstance = reinterpret_cast<const void*>( pFunc );
            newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
            {
                auto pFn = reinterpret_cast<R ( * )( Args... )>( const_cast<void*>( pPtr ) );
                return pFn( std::forward<Args>( args )... );
            } );

            return newDelegate;
        }

        /** @brief const 인스턴스의 멤버 함수를 바인딩합니다. */
        template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), const Class*, Args...>>>
        static Delegate create( const Class* pClassInstance )
        {
            Delegate newDelegate{};
            newDelegate._pInstance = pClassInstance;
            newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
            {
                const Class* pInstance = static_cast<const Class*>( pPtr );
                return std::invoke( MemberFunction, pInstance, std::forward<Args>( args )... );
            } );

            return newDelegate;
        }

        /** @brief 인스턴스의 멤버 함수를 바인딩합니다. */
        template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), Class*, Args...>>>
        static Delegate create( Class* pClassInstance )
        {
            Delegate newDelegate{};
            newDelegate._pInstance = pClassInstance;
            newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
            {
                Class* pInstance = const_cast<Class*>( static_cast<const Class*>( pPtr ) );
                return std::invoke( MemberFunction, pInstance, std::forward<Args>( args )... );
            } );

            return newDelegate;
        }

        /**
         * @brief 람다나 함수 객체로 델리게이트를 만듭니다.
         * @note 매 프레임 불리는 Tick 처럼 아주 자주 불리는 콜백에는 람다 대신 멤버 함수 바인딩(create<Method>)을 쓰십시오.
         */
        template <typename Lambda>
        static Delegate create( const Lambda& lambdaFunc )
        {
            Delegate newDelegate{};
            newDelegate._managerFunc = &lambdaManager<Lambda>;
            newDelegate._stubFunc    = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
            {
                const Lambda* pInstance = static_cast<const Lambda*>( pPtr );
                return ( const_cast<Lambda*>( pInstance )->operator() )( std::forward<Args>( args )... );
            } );

            constexpr bool bIsSBO = sizeof( Lambda ) <= kInlineBufferSize && alignof( Lambda ) <= alignof( std::max_align_t ) && std::is_nothrow_move_constructible_v<Lambda>;
            if constexpr ( bIsSBO )
            {
                sw_placement_new( &newDelegate._inlineBuffer ) Lambda( lambdaFunc );
                newDelegate._pInstance = &newDelegate._inlineBuffer;
            }
            else
            {
                Lambda* pHeapLambda                                       = sw_new Lambda( lambdaFunc );
                *reinterpret_cast<Lambda**>( &newDelegate._inlineBuffer ) = pHeapLambda;
                newDelegate._pInstance                                    = pHeapLambda;
            }

            return newDelegate;
        }

    private:
        using stub_function = R ( * )( const void*, Args... );

        /** @brief 바인딩을 해제합니다. */
        void release()
        {
            if ( _managerFunc )
                _managerFunc( DelegateManagerOp::Destroy, &_inlineBuffer, nullptr );
            _pInstance   = nullptr;
            _stubFunc    = nullptr;
            _managerFunc = nullptr;
        }

        /** @brief 다른 델리게이트를 복사해 옵니다. */
        void copyFrom( const Delegate& other )
        {
            _stubFunc    = other._stubFunc;
            _managerFunc = other._managerFunc;
            if ( _managerFunc )
                _pInstance = _managerFunc( DelegateManagerOp::Copy, &_inlineBuffer, &other._inlineBuffer );
            else
                _pInstance = other._pInstance;
        }

        /** @brief 다른 델리게이트를 옮겨 옵니다. */
        void moveFrom( Delegate&& other )
        {
            _stubFunc    = other._stubFunc;
            _managerFunc = other._managerFunc;
            if ( _managerFunc )
                _pInstance = _managerFunc( DelegateManagerOp::Move, &_inlineBuffer, &other._inlineBuffer );
            else
                _pInstance = other._pInstance;
            other._pInstance   = nullptr;
            other._stubFunc    = nullptr;
            other._managerFunc = nullptr;
        }

        /** @brief 람다 저장소를 복사 · 이동 · 파괴합니다. */
        template <typename Lambda>
        static void* lambdaManager( DelegateManagerOp managerOp, void* pDest, const void* pSrc )
        {
            constexpr bool bIsSBO = sizeof( Lambda ) <= kInlineBufferSize && alignof( Lambda ) <= alignof( std::max_align_t ) && std::is_nothrow_move_constructible_v<Lambda>;

            switch ( managerOp )
            {
                case DelegateManagerOp::Copy:
                {
                    if constexpr ( bIsSBO )
                    {
                        sw_placement_new( pDest ) Lambda( *static_cast<const Lambda*>( pSrc ) );
                        return pDest;
                    }
                    else
                    {
                        Lambda* pNewHeap                = sw_new Lambda( **static_cast<Lambda* const*>( pSrc ) );
                        *static_cast<Lambda**>( pDest ) = pNewHeap;
                        return pNewHeap;
                    }
                }
                case DelegateManagerOp::Move:
                {
                    if constexpr ( bIsSBO )
                    {
                        sw_placement_new( pDest ) Lambda( std::move( *static_cast<Lambda*>( const_cast<void*>( pSrc ) ) ) );
                        return pDest;
                    }
                    else
                    {
                        Lambda* pHeap                   = *static_cast<Lambda* const*>( pSrc );
                        *static_cast<Lambda**>( pDest ) = pHeap;
                        return pHeap;
                    }
                }
                case DelegateManagerOp::Destroy:
                {
                    if constexpr ( bIsSBO )
                        static_cast<Lambda*>( pDest )->~Lambda();
                    else
                        sw_delete( *static_cast<Lambda**>( pDest ) );
                    return nullptr;
                }
                default:
                    break;
            }
            return nullptr;
        }

        const void*      _pInstance{ nullptr };
        stub_function    _stubFunc{ nullptr };
        manager_function _managerFunc{ nullptr };
        alignas( std::max_align_t ) std::array<uint8, kInlineBufferSize> _inlineBuffer{};
    };
} // namespace sw

namespace sw
{

    // ------------------------------------------------------------------------------
    // 3) DelegateHandle — add() 가 발급하고 remove(handle) 로 해제한다.
    //    발급기(`allocate`)의 실체는 Engine.dll(Core OBJECT)에 하나뿐이다. 로드되는 모듈들도 같은 카운터를 보므로,
    //    모듈이 건 구독의 핸들이 엔진 쪽 핸들과 겹치지 않는다.
    // ------------------------------------------------------------------------------
    /** @brief 멀티캐스트 항목을 가리키는 발급 ID 입니다. 0 은 무효입니다. */
    struct DelegateHandle
    {
        uint64 _id{ 0 };

        /** @brief 다음 멀티캐스트 델리게이트 핸들을 발급합니다. */
        SW_API static DelegateHandle allocate();

        /** @brief 발급된 ID 가 있으면 true 입니다. */
        bool isValid() const { return _id != 0; }
        /** @brief 같은지 비교합니다. */
        bool operator==( const DelegateHandle& rhs ) const { return _id == rhs._id; }
        /** @brief 다른지 비교합니다. */
        bool operator!=( const DelegateHandle& rhs ) const { return _id != rhs._id; }
    };

    /** @brief 타입을 지운 멀티캐스트 인터페이스입니다. 항목은 핸들로만 제거합니다. */
    class SW_API IMulticastDelegateBase
    {
    public:
        IMulticastDelegateBase() = default;
        /** @brief 가상 소멸자입니다(리스트 정리는 파생 클래스가 합니다). */
        virtual ~IMulticastDelegateBase()                                      = default;
        IMulticastDelegateBase( const IMulticastDelegateBase& )                = default;
        IMulticastDelegateBase& operator=( const IMulticastDelegateBase& )     = default;
        IMulticastDelegateBase( IMulticastDelegateBase&& ) noexcept            = default;
        IMulticastDelegateBase& operator=( IMulticastDelegateBase&& ) noexcept = default;

        /** @brief 핸들과 같은 항목을 제거합니다. */
        virtual void remove( const DelegateHandle& handle ) = 0;
    };

    /** @brief 시그니처별로 특수화하는 멀티캐스트 델리게이트입니다. */
    template <typename Signature>
    class MulticastDelegate;

    // ------------------------------------------------------------------------------
    // 4) MulticastDelegate — add / remove / broadcast. broadcast 중의 remove 는 끝난 뒤로 미룬다
    // ------------------------------------------------------------------------------
    /**
     * @class MulticastDelegate
     * @brief 여러 델리게이트를 모아 두고 한꺼번에 호출하는 컨테이너입니다.
     */
    template <typename R, typename... Args>
    class MulticastDelegate<R( Args... )> : public IMulticastDelegateBase
    {
        using delegate_type = Delegate<R( Args... )>;

        /** @brief 핸들과 델리게이트 한 쌍입니다. */
        struct DelegateEntry
        {
            DelegateHandle _handle;
            delegate_type  _delegate;
        };

    public:
        /** @brief 빈 구독 리스트로 둡니다. */
        MulticastDelegate() noexcept
            : _listDelegate{}
            , _listPendingRemove{}
            , _broadcastDepth{ 0 }
        {
        }

        // ------------------------------------------------------------------------------
        // 복사 · 이동 — broadcast 상태는 값의 일부가 아니다
        //
        // 이 넷을 직접 정의하는 이유는 두 가지다.
        //
        // 1) **이동이 복사로 대체되고 있었다.** 복사 생성자를 `= default` 로 *선언*하는 순간 암시적 이동 생성자와 이동
        //    대입이 만들어지지 않는다(C++ 규칙). 그래서 `MulticastDelegate` 를 옮길 때마다 구독자 벡터가 통째로 깊은
        //    복사됐다. `is_nothrow_move_constructible` 이 false 였고, `std::move` 한 뒤에도 원본이 그대로 남아 있었다.
        // 2) **`_broadcastDepth` 와 지연 제거 큐까지 함께 복사됐다.** 이 둘은 값이 아니라 *그 인스턴스의 호출 스택
        //    상태*다. broadcast 중에 복사하면 사본의 깊이가 0 이 아닌 채로 만들어지고, 그 사본은 지연된 제거를 **영영
        //    반영하지 않는다**(자기 broadcast 는 깊이가 1→2→1 로만 오가므로 0 이 되지 않는다).
        //
        // 그래서 생성은 깊이 0 · 빈 큐로 시작하고, 대입은 받는 쪽의 깊이를 건드리지 않는다(그 깊이는 지금 이 객체를
        // broadcast 중인 호출 스택의 것이다). 큐는 비운다. 교체되어 사라질 리스트의 핸들이기 때문이다.
        // ------------------------------------------------------------------------------
        /** @brief 구독 리스트만 복제합니다. broadcast 상태는 가져오지 않습니다. */
        MulticastDelegate( const MulticastDelegate& other )
            : IMulticastDelegateBase( other )
            , _listDelegate{ other._listDelegate }
            , _listPendingRemove{}
            , _broadcastDepth{ 0 }
        {
        }

        /** @brief 구독 리스트를 넘겨받습니다. 원본은 빈 상태로 남습니다. */
        MulticastDelegate( MulticastDelegate&& other ) noexcept
            : IMulticastDelegateBase( std::move( other ) )
            , _listDelegate{ std::move( other._listDelegate ) }
            , _listPendingRemove{ std::move( other._listPendingRemove ) }
            , _broadcastDepth{ 0 }
        {
            other._listDelegate.clear();
            other._listPendingRemove.clear();
            other._broadcastDepth = 0;
        }

        /** @brief 구독 리스트만 대입합니다. 이 객체의 broadcast 깊이는 그대로 둡니다. */
        MulticastDelegate& operator=( const MulticastDelegate& other )
        {
            if ( this != &other )
            {
                IMulticastDelegateBase::operator=( other );
                _listDelegate = other._listDelegate;
                _listPendingRemove.clear();
            }
            return *this;
        }

        /** @brief 구독 리스트를 넘겨받습니다. 이 객체의 broadcast 깊이는 그대로 둡니다. */
        MulticastDelegate& operator=( MulticastDelegate&& other ) noexcept
        {
            if ( this != &other )
            {
                IMulticastDelegateBase::operator=( std::move( other ) );
                _listDelegate = std::move( other._listDelegate );
                _listPendingRemove.clear();
                other._listDelegate.clear();
                other._listPendingRemove.clear();
                other._broadcastDepth = 0;
            }
            return *this;
        }

        /** @brief 두 멀티캐스트 델리게이트가 같은 대상 목록을 가졌는지 비교합니다. */
        bool operator==( const MulticastDelegate& other ) const
        {
            if ( _listDelegate.size() != other._listDelegate.size() )
                return false;

            const uint32 delegateCount = static_cast<uint32>( _listDelegate.size() );
            for ( uint32 delegateIndex = 0; delegateIndex < delegateCount; ++delegateIndex )
            {
                if ( _listDelegate[delegateIndex]._delegate != other._listDelegate[delegateIndex]._delegate )
                    return false;
            }
            return true;
        }

        /** @brief 다른지 비교합니다. */
        bool operator!=( const MulticastDelegate& other ) const { return ( *this == other ) == false; }

        /** @brief 등록된 델리게이트가 하나라도 있는지 확인합니다. */
        bool isBound() const { return _listDelegate.empty() == false; }

        /**
         * @brief 등록된 모든 델리게이트를 호출합니다.
         * @details broadcast 중에 remove() 가 불리면 끝난 뒤 한꺼번에 처리합니다. 호출할 때마다 벡터 전체를 복사하지 않으므로
         *          자주 발생하는 이벤트에도 알맞습니다.
         */
        void broadcast( Args... args )
        {
            // 콜백이 다시 broadcast 를 부를 수 있으므로 깊이로 센다. 가장 바깥 호출만 지연된 제거를 처리한다.
            ++_broadcastDepth;
            const size_t numDelegates = _listDelegate.size();
            for ( size_t entryIndex = 0; entryIndex < numDelegates; ++entryIndex )
            {
                // 콜백이 add() 를 부르면 벡터가 재할당되므로 원소 참조를 들고 호출하지 않는다.
                delegate_type callee = _listDelegate[entryIndex]._delegate;
                if ( callee.isBound() )
                    callee( args... );
            }
            --_broadcastDepth;

            if ( _broadcastDepth > 0 )
                return;

            // broadcast 중에 요청된 remove 를 한꺼번에 처리한다
            for ( const DelegateHandle& removeHandle : _listPendingRemove )
            {
                removeNow( removeHandle );
            }
            _listPendingRemove.clear();
        }

        /** @brief 새 델리게이트를 등록하고 핸들을 반환합니다. */
        DelegateHandle add( const delegate_type& newDelegate )
        {
            DelegateHandle handle = DelegateHandle::allocate();
            _listDelegate.push_back( DelegateEntry{ handle, newDelegate } );
            return handle;
        }

        /** @brief 발급받은 핸들로 델리게이트 등록을 해제합니다. */
        void remove( const DelegateHandle& handle ) override
        {
            if ( handle.isValid() == false )
                return;

            if ( _broadcastDepth > 0 )
            {
                // broadcast 중에 삭제되면, 남은 순회에서 이미 파괴된 객체를 호출(UAF)하지 않도록 즉시 무효화한다
                for ( DelegateEntry& entry : _listDelegate )
                {
                    if ( entry._handle == handle )
                    {
                        entry._delegate = delegate_type{};
                        break;
                    }
                }
                _listPendingRemove.push_back( handle );
                return;
            }
            removeNow( handle );
        }

        /** @brief 등록된 델리게이트 중 target 과 같은 항목을 찾아 삭제합니다. */
        void remove( const delegate_type& target )
        {
            const auto iter = std::find_if( _listDelegate.begin(), _listDelegate.end(), [&]( const DelegateEntry& entry )
            { return entry._delegate == target; } );

            if ( iter == _listDelegate.end() )
                return;

            // 핸들 경로와 같은 지연 제거를 거쳐야 broadcast 중에 이터레이터가 깨지지 않는다.
            remove( iter->_handle );
        }

        /** @brief 등록된 델리게이트를 모두 해제합니다. */
        void removeAll()
        {
            if ( _broadcastDepth > 0 )
            {
                for ( DelegateEntry& entry : _listDelegate )
                {
                    entry._delegate = delegate_type{};
                    _listPendingRemove.push_back( entry._handle );
                }
                return;
            }
            _listDelegate.clear();
        }

        /** @brief 등록된 델리게이트를 모두 해제합니다(removeAll 의 별칭). */
        void clear() { removeAll(); }

        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 델리게이트를 모두 떼고, 뗀 수를 반환합니다.
         * @details 그 범위의 모듈(DLL · SO)이 만든 구독을 모듈을 내리기 **전에** 떼는 데 씁니다. broadcast 중이면 `remove` 와 같이 그 자리에서
         *          무효화하고 끝난 뒤 지웁니다.
         */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd )
        {
            vector<DelegateHandle> listHandle;
            for ( const DelegateEntry& entry : _listDelegate )
            {
                if ( entry._delegate.isCodeWithin( pBegin, pEnd ) )
                    listHandle.push_back( entry._handle );
            }
            for ( const DelegateHandle& handle : listHandle )
            {
                remove( handle );
            }
            return static_cast<uint32>( listHandle.size() );
        }

    private:
        /** @brief 핸들에 해당하는 항목을 즉시 제거합니다. broadcast 밖에서만 부르십시오. */
        void removeNow( const DelegateHandle& handle )
        {
            const auto iter = std::find_if( _listDelegate.begin(), _listDelegate.end(), [&]( const DelegateEntry& entry )
            { return entry._handle == handle; } );
            if ( iter != _listDelegate.end() )
                _listDelegate.erase( iter );
        }

        vector<DelegateEntry>  _listDelegate;
        vector<DelegateHandle> _listPendingRemove; ///< broadcast 중에 미뤄 둔 remove 목록
        uint32                 _broadcastDepth;    ///< 중첩된 broadcast 깊이. 0 이 될 때만 지연된 제거를 반영한다
    };
} // namespace sw

/** @brief Delegate 별칭을 선언합니다. */
#define SW_DECLARE_DELEGATE( ReturnType, DelegateName, ... ) using DelegateName = sw::Delegate<ReturnType( __VA_ARGS__ )>
/** @brief MulticastDelegate 별칭을 선언합니다. */
#define SW_DECLARE_MULTI_CAST_DELEGATE( ReturnType, DelegateName, ... ) using DelegateName = sw::MulticastDelegate<ReturnType( __VA_ARGS__ )>
/** @brief 자유 함수를 델리게이트에 붙입니다. */
#define SW_DELEGATE_FUNCTION( DelegateName, Func ) DelegateName::create<Func>()
/** @brief 멤버 함수를 델리게이트에 붙입니다. */
#define SW_DELEGATE_METHOD( DelegateName, Method, Instance ) DelegateName::create<Method>( Instance )
/** @brief 람다 · 함수 객체를 델리게이트에 붙입니다. */
#define SW_DELEGATE_LAMBDA( DelegateName, ... ) DelegateName::create( __VA_ARGS__ )
