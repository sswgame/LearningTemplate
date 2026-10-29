#include "pch.h"

#include "Engine/EngineFrameHookList.h"

namespace sw
{
    SW_LOG_CALLER( "Engine" );

    EngineFrameHookList::EngineFrameHookList()
        : _listHook{}
    {
    }

    EngineFrameHookList::~EngineFrameHookList() = default;

    Subscription EngineFrameHookList::add( const EngineFrameHooks& hooks )
    {
        const bool bAnyBound = hooks._onFrameBegin.isBound() || hooks._onAfterSceneTick.isBound() || hooks._onFrameEnd.isBound() || hooks._onWindowClosed.isBound();
        if ( bAnyBound == false )
        {
            SW_LOG_ERROR( "Frame hooks are all empty - nothing is registered" );
            return {};
        }
        // 묶음 하나를 항목 하나로 올린다 — 시점마다 맞는 칸을 부르고, 빈 칸 · 종료 코드가 없는 시점은 `kEngineFrameRunning` 이다.
        return _listHook.add( FrameHookDelegate::create( [hooks]( FramePoint point, const EngineFrameContext& context ) -> int32
        {
            switch ( point )
            {
                case FramePoint::FrameBegin:
                    if ( hooks._onFrameBegin.isBound() )
                        hooks._onFrameBegin( context );
                    return kEngineFrameRunning;
                case FramePoint::AfterSceneTick:
                    if ( hooks._onAfterSceneTick.isBound() )
                        hooks._onAfterSceneTick( context );
                    return kEngineFrameRunning;
                case FramePoint::FrameEnd:
                    return hooks._onFrameEnd.isBound() ? hooks._onFrameEnd( context ) : kEngineFrameRunning;
                case FramePoint::WindowClosed:
                    return hooks._onWindowClosed.isBound() ? hooks._onWindowClosed( context ) : kEngineFrameRunning;
            }
            return kEngineFrameRunning;
        } ) );
    }

    void EngineFrameHookList::clear()
    {
        _listHook = FrameHookList{}; // 남은 구독을 무효화하고 저장소를 놓는다
    }

    void EngineFrameHookList::notifyFrameBegin( const EngineFrameContext& context )
    {
        _listHook.broadcast( FramePoint::FrameBegin, context );
    }

    void EngineFrameHookList::notifyAfterSceneTick( const EngineFrameContext& context )
    {
        _listHook.broadcast( FramePoint::AfterSceneTick, context );
    }

    int32 EngineFrameHookList::notifyFrameEnd( const EngineFrameContext& context )
    {
        // 모든 관찰자를 부른다(멈추지 않는다). 여럿이 코드를 내면 처음 것의 코드다.
        int32 exitCode = kEngineFrameRunning;
        (void)_listHook.broadcastUntil( [&exitCode]( int32 observerExitCode )
        {
            if ( observerExitCode != kEngineFrameRunning && exitCode == kEngineFrameRunning )
                exitCode = observerExitCode;
            return false;
        }, FramePoint::FrameEnd, context );
        return exitCode;
    }

    int32 EngineFrameHookList::notifyWindowClosed( const EngineFrameContext& context )
    {
        // 처음으로 코드를 낸 관찰자에서 멈춘다.
        int32 exitCode = kEngineFrameRunning;
        (void)_listHook.broadcastUntil( [&exitCode]( int32 observerExitCode )
        {
            if ( observerExitCode == kEngineFrameRunning )
                return false;
            exitCode = observerExitCode;
            return true;
        }, FramePoint::WindowClosed, context );
        return exitCode;
    }
} // namespace sw
