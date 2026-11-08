/**
 * @file DevTools/Client/Render/GPUDiagnosticsCommands.cpp
 * @brief GPU 진단 개발 명령입니다 — `gpu.inspect x y [첨부] | off`(픽셀 검사 · 셰이더 출력 선택), `gpu.thread n`(컴퓨트 스레드 선택), `gpu.timings [on|off]`(패스별 GPU 시간 줄).
 * @details 언리얼 `r.ShaderPrint` 커서 · `stat gpu`, RenderDoc 픽셀 기록과 같은 자리다. 상태는 엔진 서브시스템(`GPUPixelInspector` · `FrameProfiler`)이 들고, 이 파일은 명령 줄만 푼다.
 *          셰이더 출력(`SW_GPU_PRINT`)은 `-gv_gpuMessages=1` 로 띄운 실행에서만 줄이 나온다(메시지 변형 PSO). 첨부 값 줄(`GPUInspect.Pixel`)은 늘 나온다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"

namespace sw
{
    namespace
    {
        struct GPUDiagnosticsCommandsInternal
        {
            /** @brief 음이 아닌 정수 인자를 읽습니다. */
            [[nodiscard]] static bool parseCoordinate( const string& text, uint32& outValue )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( text, value ) == false || value < 0 )
                    return false;
                outValue = static_cast<uint32>( value );
                return true;
            }

            static bool runInspect( const vector<string>& listArgument, string& outReply )
            {
                GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
                if ( pInspector == nullptr )
                {
                    outReply = "no GPU pixel inspector in this process";
                    return false;
                }
                if ( listArgument.size() == 1 && listArgument[0] == "off" )
                {
                    pInspector->clearSelection();
                    outReply = "GPU pixel selection cleared";
                    return true;
                }
                uint32     x      = 0;
                uint32     y      = 0;
                const bool bValid = ( listArgument.size() == 2 || listArgument.size() == 3 ) && parseCoordinate( listArgument[0], x ) && parseCoordinate( listArgument[1], y );
                if ( bValid == false )
                    return false; // 사용법 줄을 낸다
                const string_view attachment = listArgument.size() == 3 ? string_view( listArgument[2] ) : string_view{};
                pInspector->requestPixelInspection( x, y, attachment );
                outReply = formatToString( "inspecting pixel %#,%# - attachment values (GPUInspect.Pixel) after this frame, shader prints (GPUMessage.Print) a few frames later with -gv_gpuMessages=1", x, y );
                return true;
            }

            static bool runThread( const vector<string>& listArgument, string& outReply )
            {
                GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
                uint32                   thread     = 0;
                if ( pInspector == nullptr || listArgument.size() != 1 || parseCoordinate( listArgument[0], thread ) == false )
                    return false;
                pInspector->selectThread( thread );
                outReply = formatToString( "SW_GPU_PRINT now writes for compute dispatch thread %#", thread );
                return true;
            }

            static bool runShow( const vector<string>& listArgument, string& outReply )
            {
                GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
                if ( pInspector == nullptr || listArgument.size() != 1 )
                    return false;
                const bool bOff = listArgument[0] == "off";
                pInspector->setVisualizedAttachment( bOff ? string_view{} : string_view( listArgument[0] ) );
                outReply = bOff ? string( "showing the normal output" ) : formatToString( "showing attachment '%#' on screen (gpu.show off to return)", listArgument[0].c_str() );
                return true;
            }

            static bool runTimings( const vector<string>& listArgument, string& outReply )
            {
                FrameProfiler* const pProfiler = engine::findSubsystem<FrameProfiler>();
                if ( pProfiler == nullptr || listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    const bool bOn = listArgument[0] == "on";
                    if ( bOn == false && listArgument[0] != "off" )
                        return false;
                    pProfiler->setEnabled( bOn ); // GPU 타임스탬프는 계측이 켜졌을 때만 쓴다(GPUPassTimeline::isRecording)
                    outReply = bOn ? "GPU timings on - run gpu.timings again after a few frames" : "GPU timings off";
                    return true;
                }
                if ( pProfiler->isEnabled() == false )
                {
                    outReply = "the profiler is off - run gpu.timings on first";
                    return false;
                }
                const vector<GPUTimingRow> listRow = GPUTimingRow::collect( *pProfiler );
                for ( const GPUTimingRow& row : listRow )
                {
                    SW_LOG_INFO_ID( "GPUTiming.Pass", "GPU %# ms %#", row._milliseconds, row._name.c_str() );
                }
                outReply = formatToString( "%# GPU scopes logged (GPUTiming.Pass, slowest first)", static_cast<uint32>( listRow.size() ) );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( GPUInspectPixel, "gpu.inspect", "gpu.inspect <x> <y> [attachment] | gpu.inspect off",
                    "Log the attachment values at a pixel and select it for SW_GPU_PRINT (shader prints need -gv_gpuMessages=1)", &GPUDiagnosticsCommandsInternal::runInspect );
    SW_DEV_COMMAND( GPUSelectThread, "gpu.thread", "gpu.thread <dispatch thread>", "Select the compute dispatch thread for SW_GPU_PRINT", &GPUDiagnosticsCommandsInternal::runThread );
    SW_DEV_COMMAND( GPUShowAttachment, "gpu.show", "gpu.show <attachment> | gpu.show off", "Show a pipeline attachment (GBuffer, depth, ...) on screen instead of the normal output",
                    &GPUDiagnosticsCommandsInternal::runShow );
    SW_DEV_COMMAND( GPUTimings, "gpu.timings", "gpu.timings [on|off]", "Log the last frame GPU time per pass (GPUTiming.Pass), or turn GPU timing on/off", &GPUDiagnosticsCommandsInternal::runTimings );
} // namespace sw
