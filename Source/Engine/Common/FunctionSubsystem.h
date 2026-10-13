/**
 * @file Engine/Common/FunctionSubsystem.h
 * @brief 자기 클래스가 없는 기동 단계를 서브시스템으로 적는 단 하나의 타입입니다(선언 + 초기화 · 종료 델리게이트 + 함께 소멸할 객체).
 * @details 리플렉션 등록 · 모듈 훅처럼 호스트가 엮는 단계, 그리고 Core 서비스(태스크 · 비동기 IO · 코덱 레지스트리 — Core 는 `ISubsystem` 을 모른다)를
 *          세우는 단계를 위해 단계마다 구조체를 짓지 않습니다. 델리게이트 둘이 본문이고, 묶지 않은 델리게이트는 아무것도 하지 않습니다(초기화는
 *          성공으로 칩니다). 객체를 넘기면(`takeOwnership`) 이 서브시스템의 소멸 자리 — 모든 종료 뒤, 초기화의 역순 — 에서 함께 소멸합니다.
 *
 * ```cpp
 * startup.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Reflection", "", EngineInitTarget::All, MemoryTag::Reflection },
 *                                                       SW_DELEGATE_FUNCTION( SubsystemInitializeDelegate, &registerReflection ) ) );
 * ```
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryTag.h"

#include "Engine/Common/ISubsystem.h"

namespace sw
{
    /** @brief `FunctionSubsystem` 의 초기화 본문입니다. */
    SW_DECLARE_DELEGATE( EngineInitResult, SubsystemInitializeDelegate, void );
    /** @brief `FunctionSubsystem` 의 종료 본문입니다. */
    SW_DECLARE_DELEGATE( void, SubsystemShutdownDelegate, void );
} // namespace sw

namespace sw
{
    /** @brief `FunctionSubsystem` 의 선언(이름 · 의존 · 대상 · 메모리 태그)입니다. 글은 문자열 리터럴(프로세스 수명)이어야 합니다. */
    struct FunctionSubsystemDesc
    {
        const utf8*      _pName;         ///< 이름(의존 칸이 부르는 식별자)
        const utf8*      _pDependencies; ///< 먼저 서야 하는 이름들(쉼표 · 공백 · 중괄호로 구분)
        EngineInitTarget _target;        ///< 도는 호스트
        MemoryTag        _memoryTag;     ///< 초기화가 할당하는 메모리의 용도
    };
} // namespace sw

namespace sw
{
    /**
     * @class FunctionSubsystem
     * @brief 선언(`FunctionSubsystemDesc`)과 델리게이트 둘로 된 서브시스템입니다. 넘겨받은 객체가 있으면 소멸할 때 함께 소멸시킵니다.
     */
    class FunctionSubsystem final : public ISubsystem
    {
    public:
        explicit FunctionSubsystem( const FunctionSubsystemDesc& desc, SubsystemInitializeDelegate initialize = {}, SubsystemShutdownDelegate shutdown = {} )
            : _initialize{ std::move( initialize ) }
            , _shutdown{ std::move( shutdown ) }
            , _desc{ desc }
            , _listOwned{}
        {
        }
        /** @brief 넘겨받은 객체를 넘겨받은 역순으로 소멸시킵니다. */
        ~FunctionSubsystem() override
        {
            for ( size_t index = _listOwned.size(); index > 0; --index )
            {
                _listOwned[index - 1]._pDestroy( _listOwned[index - 1]._pObject );
            }
        }

        FunctionSubsystem( const FunctionSubsystem& )            = delete;
        FunctionSubsystem& operator=( const FunctionSubsystem& ) = delete;

        EngineInitResult initializeSubsystem() override { return _initialize.isBound() ? _initialize() : EngineInitResult::Succeeded; }
        void             shutdownSubsystem() override
        {
            if ( _shutdown.isBound() )
                _shutdown();
        }

        /**
         * @brief @p owned 를 넘겨받아 이 서브시스템과 함께 소멸시킵니다. 넘긴 객체를 돌려줍니다.
         * @details 서비스 저장소가 만든 서비스의 소유를 기동 순서로 옮길 때 씁니다 — 해제가 그 단계의 소멸 자리(초기화의 역순)에서 일어납니다.
         *          여럿을 넘기면 넘긴 역순으로 소멸합니다(먼저 넘긴 것이 나중까지 산다).
         */
        template <class TOwned>
        TOwned& takeOwnership( unique_ptr<TOwned> owned )
        {
            return takeOwnershipInternal( std::move( owned ), &destroyOwned<TOwned> );
        }
        /** @brief `takeOwnership` 과 같고, 소멸 직전에 @p pRelease 를 부릅니다(`AssetManager::shutdown` 처럼 소멸자가 하지 않는 해제). */
        template <auto pRelease, class TOwned>
        TOwned& takeOwnership( unique_ptr<TOwned> owned )
        {
            return takeOwnershipInternal( std::move( owned ), &releaseAndDestroyOwned<pRelease, TOwned> );
        }

        const utf8*      getSubsystemName() const override { return _desc._pName; }
        const utf8*      getSubsystemDependencies() const override { return _desc._pDependencies; }
        EngineInitTarget getSubsystemTarget() const override { return _desc._target; }
        MemoryTag        getSubsystemMemoryTag() const override { return _desc._memoryTag; }

    private:
        /** @brief 넘겨받은 객체 하나와 그 타입으로 소멸시키는 함수입니다. */
        struct OwnedObject
        {
            void* _pObject;
            void ( *_pDestroy )( void* pObject );
        };

        template <class TOwned>
        TOwned& takeOwnershipInternal( unique_ptr<TOwned> owned, void ( *pDestroy )( void* pObject ) )
        {
            SW_ASSERT( owned != nullptr );
            TOwned& taken = *owned;
            _listOwned.push_back( { owned.release(), pDestroy } );
            return taken;
        }
        template <class TOwned>
        static void destroyOwned( void* pObject )
        {
            default_delete<TOwned>{}( static_cast<TOwned*>( pObject ) );
        }
        template <auto pRelease, class TOwned>
        static void releaseAndDestroyOwned( void* pObject )
        {
            ( static_cast<TOwned*>( pObject )->*pRelease )();
            default_delete<TOwned>{}( static_cast<TOwned*>( pObject ) );
        }

    private:
        SubsystemInitializeDelegate _initialize;
        SubsystemShutdownDelegate   _shutdown;
        FunctionSubsystemDesc       _desc;
        vector<OwnedObject>         _listOwned; ///< `takeOwnership` 으로 넘겨받은 객체(넘겨받은 순서)
    };
} // namespace sw
