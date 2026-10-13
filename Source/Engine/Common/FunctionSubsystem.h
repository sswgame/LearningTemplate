/**
 * @file Engine/Common/FunctionSubsystem.h
 * @brief 소유 객체가 없는 "한 번 하는 일" 을 서브시스템으로 적는 단 하나의 타입입니다(이름 · 의존 · 대상 + 초기화 · 종료 델리게이트).
 * @details 리플렉션 등록 · 설정 읽기 · 헤드리스 작업처럼 객체를 소유하지 않는 단계를 위해 단계마다 구조체를 짓지 않습니다. 델리게이트 둘이
 *          본문이고, 묶지 않은 델리게이트는 아무것도 하지 않습니다(초기화는 성공으로 칩니다).
 *
 * ```cpp
 * startup.addSubsystem( make_unique<FunctionSubsystem>( "Reflection", "", EngineInitTarget::All,
 *                                                       SW_DELEGATE_FUNCTION( SubsystemInitializeDelegate, &registerReflection ) ) );
 * ```
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
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
    /**
     * @class FunctionSubsystem
     * @brief 이름 · 의존 · 대상과 델리게이트 둘로 된 서브시스템입니다. 이름 · 의존 글은 문자열 리터럴(프로세스 수명)이어야 합니다.
     */
    class FunctionSubsystem final : public ISubsystem
    {
    public:
        FunctionSubsystem( const utf8* pName, const utf8* pDependencies, EngineInitTarget target, SubsystemInitializeDelegate initialize,
                           SubsystemShutdownDelegate shutdown = {}, MemoryTag memoryTag = MemoryTag::EngineMisc )
            : _initialize{ std::move( initialize ) }
            , _shutdown{ std::move( shutdown ) }
            , _pName{ pName }
            , _pDependencies{ pDependencies }
            , _target{ target }
            , _memoryTag{ memoryTag }
        {
        }

        EngineInitResult initializeSubsystem() override { return _initialize.isBound() ? _initialize() : EngineInitResult::Succeeded; }
        void             shutdownSubsystem() override
        {
            if ( _shutdown.isBound() )
                _shutdown();
        }

        const utf8*      getSubsystemName() const override { return _pName; }
        const utf8*      getSubsystemDependencies() const override { return _pDependencies; }
        EngineInitTarget getSubsystemTarget() const override { return _target; }
        MemoryTag        getSubsystemMemoryTag() const override { return _memoryTag; }

    private:
        SubsystemInitializeDelegate _initialize;
        SubsystemShutdownDelegate   _shutdown;
        const utf8*                 _pName;
        const utf8*                 _pDependencies;
        EngineInitTarget            _target;
        MemoryTag                   _memoryTag;
    };
} // namespace sw
