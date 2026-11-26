/**
 * @file DevTools/Client/Render/FrameDebuggerCommands.cpp
 * @brief 프레임 디버거 개발 명령 `framedebug` 입니다 — 게임 중(에디터 없이)에 유니티 Frame Debugger 의 이벤트 고르기를 한다.
 * @details `framedebug on`(전체 프레임을 기록 — 건너뛰지 않음) · `framedebug <패스> <드로우>`(그 드로우까지 그린다) · `framedebug next|prev`(기록 안에서 한 칸) · `framedebug off`.
 *          상태는 `RenderDebugRegistry` 가 들고 에디터 렌더 디버거 창 · HUD `framedebug` 범주가 같은 값을 본다. 화면과 렌더 타깃(`gpu.show` — 언리얼 `vis`)이 그 지점의 그림이다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

namespace sw
{
    namespace
    {
        struct FrameDebuggerCommandsInternal
        {
            /** @brief 지금 멈춤 지점을 한 줄로 적습니다(기록에 있으면 그 드로우의 PSO · 인덱스 · 묶음 수까지). */
            static string describeStop( const RenderDebugRegistry& registry )
            {
                const FrameDebuggerSettings settings = registry.getFrameDebugger();
                if ( settings._bEnabled == SW_FALSE )
                    return "frame debugger off";
                if ( settings._stopPass.empty() )
                    return "frame debugger on - recording the whole frame (framedebug next to stop at the first draw)";
                vector<FrameDebugDrawEvent> listEvent;
                registry.copyDrawEvents( listEvent );
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "stopped at %# draw %#", settings._stopPass.c_str(), settings._stopDraw );
                if ( const optional<uint32> index = RenderDebugRegistry::findStopEvent( listEvent, settings ) )
                {
                    const FrameDebugDrawEvent& event = listEvent[*index];
                    reply.appendFormat( " (event %# of %#, PSO %#, %# indices, %# batches)", *index + 1, static_cast<uint32>( listEvent.size() ), event._pso, event._indexCount,
                                        event._groupCount );
                }
                return string( reply.view() );
            }

            static bool runFrameDebug( const vector<string>& listArgument, string& outReply )
            {
                RenderDebugRegistry* const pRegistry = engine::findSubsystem<RenderDebugRegistry>();
                if ( pRegistry == nullptr )
                {
                    outReply = "no renderer debug registry in this process";
                    return true;
                }
                FrameDebuggerSettings settings = pRegistry->getFrameDebugger();
                if ( listArgument.size() == 1 && listArgument[0] == "off" )
                {
                    settings._bEnabled = SW_FALSE;
                }
                else if ( listArgument.size() == 1 && listArgument[0] == "on" )
                {
                    settings           = FrameDebuggerSettings{};
                    settings._bEnabled = SW_TRUE;
                }
                else if ( listArgument.size() == 1 && ( listArgument[0] == "next" || listArgument[0] == "prev" ) )
                {
                    vector<FrameDebugDrawEvent> listEvent;
                    pRegistry->copyDrawEvents( listEvent );
                    settings = RenderDebugRegistry::stepStop( listEvent, settings, listArgument[0] == "next" ? 1 : -1 );
                }
                else if ( listArgument.size() == 2 )
                {
                    int32 drawIndex = 0;
                    if ( StringUtil::parseInt( listArgument[1], drawIndex ) == false || drawIndex < 0 )
                        return false; // 사용법 줄을 낸다
                    settings._stopPass = listArgument[0];
                    settings._stopDraw = static_cast<uint32>( drawIndex );
                    settings._bEnabled = SW_TRUE;
                }
                else
                {
                    return false;
                }
                pRegistry->setFrameDebugger( settings );
                outReply = describeStop( *pRegistry );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( FrameDebug, "framedebug", "framedebug on | off | next | prev | <pass> <draw>",
                    "Frame debugger: draw the main view only up to one scene draw (later draws and passes are skipped) and record every draw with its PSO bindings",
                    &FrameDebuggerCommandsInternal::runFrameDebug );
} // namespace sw
