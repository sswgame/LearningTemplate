#include "pch.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"

namespace sw
{
    namespace
    {
        struct RenderGraphDumpInternal
        {
            /**
             * @brief `render.graph`: 주 렌더러의 컴파일된 렌더 그래프(레벨 · 패스 · 읽고 쓰는 자원 · 컬링된 패스, `RenderGraph::describeCompiledOrder`)를 답합니다.
             * @details "왜 이 패스가 저것보다 먼저 도나", "왜 안 도나" 에 답한다. 그래프는 렌더 스레드의 것이라 그 스레드에서 읽는다(`RenderThread::executeAndWait`).
             *          예전에는 `-gv_dumpRenderGraph` 가 런타임 패스 실행기 안에서 파이프라인을 묶을 때마다 로그로 남겼다 — 기동 때 보려면 `-gv_devConsoleExec=render.graph`.
             */
            static bool runDump( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const FrameRendererSubsystem* const pFrameRenderer = engine::findSubsystem<FrameRendererSubsystem>();
                const RenderThreadSubsystem* const  pRenderThread  = engine::findSubsystem<RenderThreadSubsystem>();
                RenderThread* const                 pThread        = pRenderThread != nullptr ? pRenderThread->findRenderThread() : nullptr;
                if ( pFrameRenderer == nullptr || pThread == nullptr )
                {
                    outReply = "no renderer in this run";
                    return true;
                }
                string     text;
                const bool bRan = pThread->executeAndWait( SW_DELEGATE_LAMBDA( RenderCommandDelegate, [pFrameRenderer, &text]( IRHIDevice& )
                { text = pFrameRenderer->getFrameRenderer().getGraph().describeCompiledOrder(); } ) );
                outReply        = bRan ? std::move( text ) : string{ "the render thread could not run the command (see the log)" };
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( RenderGraphDump, "render.graph", "render.graph", "Print the compiled render graph of the main renderer (levels, passes, read and written resources, culled passes)",
                    &RenderGraphDumpInternal::runDump );
} // namespace sw
