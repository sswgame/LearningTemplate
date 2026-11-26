/**
 * @file DevTools/Client/Render/GPUDiagnosticsCommands.cpp
 * @brief GPU 진단 개발 명령입니다. `gpu.inspect x y [첨부] | off`(픽셀 검사 · 셰이더 출력 선택), `gpu.thread n`(컴퓨트 스레드 선택), `gpu.rhicontract`(RHI 계약 검사, `RHIContractCheck`), `gpu.timings [on|off]`(패스별 GPU 시간 줄).
 * @details 언리얼 `r.ShaderPrint` 커서 · `stat gpu`, RenderDoc 픽셀 기록과 같은 자리다. 상태는 엔진 서브시스템(`GPUPixelInspector` · `FrameProfiler`)이 들고, 이 파일은 명령 줄만 푼다.
 *          셰이더 출력(`SW_GPU_PRINT`)은 `-gv_gpuMessages=1` 로 띄운 실행에서만 줄이 나온다(메시지 변형 PSO). 첨부 값 줄(`GPUInspect.Pixel`)은 늘 나온다.
 *          우편함(`RenderDebugRegistry`) 요청을 렌더 스레드에서 처리하는 검사 셋(정점 대조 · RHI 계약 · 버퍼 보기)도 여기서 주 렌더러의 실행기에 건다(`IRenderDebugChecks`).
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "DevTools/Client/Render/GPUNanCheck.h"
#include "DevTools/Client/Render/MeshVertexCheck.h"
#include "DevTools/Client/Render/RHIContractCheck.h"
#include "DevTools/Client/Render/RenderBufferInspector.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"
#include "Engine/Graphics/Shader/Binding/ComputeShaderParameters.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

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
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "inspecting pixel %#,%# - attachment values (GPUInspect.Pixel) after this frame, shader prints (GPUMessage.Print) a few frames later with -gv_gpuMessages=1", x, y );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runThread( const vector<string>& listArgument, string& outReply )
            {
                GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
                uint32                   thread     = 0;
                if ( pInspector == nullptr || listArgument.size() != 1 || parseCoordinate( listArgument[0], thread ) == false )
                    return false;
                pInspector->selectThread( thread );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "SW_GPU_PRINT now writes for compute dispatch thread %#", thread );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runShow( const vector<string>& listArgument, string& outReply )
            {
                GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
                if ( pInspector == nullptr || listArgument.size() != 1 )
                    return false;
                const bool bOff = listArgument[0] == "off";
                pInspector->setVisualizedAttachment( bOff ? string_view{} : string_view( listArgument[0] ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( bOff )
                    replyBuilder.append( "showing the normal output" );
                else
                    replyBuilder.appendFormat( "showing attachment '%#' on screen (gpu.show off to return)", listArgument[0].c_str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runRHIContract( const vector<string>& listArgument, string& outReply )
            {
                RenderDebugRegistry* const pRegistry = engine::findSubsystem<RenderDebugRegistry>();
                if ( pRegistry == nullptr || listArgument.empty() == false )
                    return false;
                pRegistry->requestRHIContractCheck();
                outReply = "checking the RHI contract on this device - RHIContractCheck.Done follows in a few frames";
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
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# GPU scopes logged (GPUTiming.Pass, slowest first)", static_cast<uint32>( listRow.size() ) );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };

        /** @class RenderDebugChecks @brief 실행기 하나(주 렌더러)가 드는 검사 셋입니다. 렌더 스레드에서만 쓴다. */
        class RenderDebugChecks final : public IRenderDebugChecks
        {
        public:
            static unique_ptr<IRenderDebugChecks> create() { return make_unique<RenderDebugChecks>(); }

            void recordAfterFrameUploads( IRHICommandList& cmd, IRHIDevice& device, const GPUScene& scene, RenderDebugRegistry& registry ) override
            {
                _meshVertexCheck.service( cmd, device, scene, registry );
                _rhiContractCheck.service( cmd, device, registry );
            }
            void recordAfterFrameGraph( IRHICommandList& cmd, IRHIDevice* pDevice, const FrameBufferTable& table, const RenderGraphExecutionContext& context, RenderPSOCache& psoCache,
                                        RenderDebugRegistry& registry ) override
            {
                _bufferInspector.service( cmd, pDevice, table, context, psoCache, registry );
            }
            bool initializeNanCheck( IRHIDevice* pDevice ) override { return _nanCheck.initialize( pDevice ); }
            bool isNanCheckActive() const override { return _nanCheck.isActive(); }
            void pollNanCheck() override { _nanCheck.poll(); }

            void recordNanCheck( IRHIDevice& device, const vector<RenderGraphPassDesc>& listPass, const TransientAttachmentPool& pool, const ComputePipeline& pipeline ) override
            {
                if ( pipeline.isValid() == false )
                {
                    SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.GvNanCheckButNaN", "The NaN check is on but its PSO is missing (shader not cooked?) - no output is scanned" );
                    return;
                }
                if ( _nanCheckCommandList == nullptr )
                    _nanCheckCommandList = device.createCommandList();
                IRHICommandList* const pCmd = _nanCheckCommandList.get();
                if ( pCmd == nullptr )
                    return;

                pCmd->beginCommandList();
                _listScannedTexture.clear(); // 같은 텍스처를 가리키는 출력은 한 번만 훑는다
                for ( const RenderGraphPassDesc& pass : listPass )
                {
                    for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
                    {
                        const TransientAttachmentPool::Attachment attachment = pool.find( output._attachment );
                        const bool                                bScannable = attachment._texture != 0 && attachment._srv != kInvalidDescriptorIndex && GPUNanCheck::isScannableFormat( attachment._format );
                        if ( bScannable == false || VectorUtil::contains( _listScannedTexture, attachment._texture ) )
                            continue;
                        _listScannedTexture.push_back( attachment._texture );

                        utf8 arrName[constant::buffer::kMax64];
                        formatstring( arrName, sizeof( arrName ), "%#:%#", pass._name, output._attachment.view() );
                        // 컴퓨트가 엔진 텍스처로 읽도록 셰이더 읽기 상태로 옮기고, 다음 프레임의 패스가 쓰도록 렌더 타깃 상태로 돌려 둔다.
                        pCmd->prepareTextureForShaderRead( attachment._texture );
                        _nanCheck.scan( *pCmd, pipeline, arrName, attachment._srv, attachment._width, attachment._height );
                        pCmd->prepareTextureForRenderTarget( attachment._texture );
                    }
                }
                _nanCheck.requestReadback( *pCmd );
                device.executeCommandList( pCmd->closeCommandList() );
            }

            void publishRenderTargets( RenderTargetRegistry& registry, const TransientAttachmentPool& pool, const vector<RenderGraphPassDesc>& listPass, hashed_string presented,
                                       vector<RenderTargetInfo> listExtraTarget ) override
            {
                vector<RenderTargetInfo> listTarget;
                listTarget.reserve( pool.getAll().size() + listExtraTarget.size() );
                for ( const auto& [name, attachment] : pool.getAll() )
                {
                    if ( attachment._texture == 0 )
                        continue;
                    RenderTargetInfo info{};
                    info._name       = name.c_str();
                    info._bPresented = ( presented.empty() == false && name == presented ) ? SW_TRUE : SW_FALSE;
                    info._texture    = attachment._texture;
                    info._width      = attachment._width;
                    info._height     = attachment._height;
                    info._format     = attachment._format;
                    info._bDepth     = RHIFormatUtil::isDepth( info._format ) ? SW_TRUE : SW_FALSE;
                    listTarget.push_back( std::move( info ) );
                }

                // 생산자 · 소비자 · 수명 — 패스의 입출력 이름을 실제 첨부로 풀어(꺼진 효과의 출력은 원본으로) 붙인다. 순서는 파이프라인 선언 순서다.
                const auto findTarget = [&]( const string& attachmentName ) -> RenderTargetInfo*
                {
                    const hashed_string resolved = pool.resolveName( hashed_string::findInterned( attachmentName ) );
                    for ( RenderTargetInfo& info : listTarget )
                    {
                        if ( resolved.empty() == false && info._name == resolved.c_str() )
                            return &info;
                    }
                    return nullptr;
                };
                const auto markUse = []( RenderTargetInfo& info, int32 passIndex )
                {
                    info._firstPass = info._firstPass.has_value() ? *info._firstPass : passIndex;
                    info._lastPass  = passIndex;
                };
                for ( int32 passIndex = 0; passIndex < static_cast<int32>( listPass.size() ); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = listPass[static_cast<size_t>( passIndex )];
                    for ( const string& input : pass._listInput )
                    {
                        if ( RenderTargetInfo* pInfo = findTarget( input ); pInfo != nullptr )
                        {
                            pInfo->_listConsumer.push_back( pass._name );
                            markUse( *pInfo, passIndex );
                        }
                    }
                    for ( const string& output : pass._listOutput )
                    {
                        if ( RenderTargetInfo* pInfo = findTarget( output ); pInfo != nullptr )
                        {
                            pInfo->_listProducer.push_back( pass._name );
                            markUse( *pInfo, passIndex );
                        }
                    }
                    if ( pass._depthAttachment.empty() == false )
                    {
                        if ( RenderTargetInfo* pInfo = findTarget( pass._depthAttachment ); pInfo != nullptr )
                        {
                            StringBuilder<constant::buffer::kMax128> producer;
                            producer.appendFormat( "%# (depth)", pass._name );
                            pInfo->_listProducer.emplace_back( producer.view() );
                            markUse( *pInfo, passIndex );
                        }
                    }
                }
                for ( RenderTargetInfo& extra : listExtraTarget )
                {
                    listTarget.push_back( std::move( extra ) );
                }

                // 이름순으로 정렬해 둔다. 해시맵 순서는 실행마다 달라서, 정렬하지 않으면 목록이 프레임마다 흔들리는 것처럼 보인다.
                VectorUtil::sort( listTarget, []( const RenderTargetInfo& lhs, const RenderTargetInfo& rhs )
                { return lhs._name < rhs._name; } );
                registry.publish( std::move( listTarget ) );
            }

            void serviceAfterFrameEnd( IRHIResourceFactory& factory ) override
            {
                RenderTargetRegistry* const pRegistry = engine::findSubsystem<RenderTargetRegistry>();
                if ( pRegistry == nullptr )
                    return;
                serviceRenderTargetRead( factory, *pRegistry );
                if ( GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>() )
                    dumpPixelAttachments( factory, *pInspector, *pRegistry );
            }

            void discardRenderTargetRead() override { _pPendingTargetRead.reset(); }

            void release( IRHIDevice* pDevice ) override
            {
                _bufferInspector.release();
                _meshVertexCheck.release( pDevice );
                _rhiContractCheck.release( pDevice );
                _nanCheckCommandList.reset();
                _nanCheck.shutdown();
            }

        private:
            /** @brief 받은 렌더 타깃 읽기 하나입니다. 티켓을 직접 쥐고 소멸할 때 버립니다 — `PendingReadback` 은 쥔 동안 같은 스레드의 `...AndWait`(스크린샷)를 단언으로 막아 쓰지 않는다. */
            struct PendingRenderTargetRead
            {
                IRHIResourceFactory* _pFactory{ nullptr };
                RHITransferTicket    _ticket{};   ///< 제출했으면 유효하다(`Busy` 로 아직 못 냈으면 무효)
                RenderTargetReadback _readback{}; ///< 요청의 텍스처 · 번호와, 제출할 때 받은 행 배치
                uint8                _bSubmitted{ SW_FALSE };

                PendingRenderTargetRead() = default;
                ~PendingRenderTargetRead() { discardTicket(); }
                PendingRenderTargetRead( const PendingRenderTargetRead& )            = delete;
                PendingRenderTargetRead& operator=( const PendingRenderTargetRead& ) = delete;

                void discardTicket()
                {
                    if ( _pFactory != nullptr && _ticket.isValid() )
                        _pFactory->discardTextureReadback( _ticket );
                    _ticket = RHITransferTicket{};
                }
            };

            /** @brief 렌더 타깃 창의 CPU 읽기 요청을 처리합니다 — 기다리지 않는 전송이라 끝남은 이후 프레임이 폴링한다. 읽는 중이면 새 요청은 우편함에 둔 채 이 읽기부터 끝낸다. */
            void serviceRenderTargetRead( IRHIResourceFactory& factory, RenderTargetRegistry& registry )
            {
                if ( _pPendingTargetRead == nullptr )
                {
                    RHITextureHandle texture = 0;
                    uint32           mip     = 0;
                    uint32           serial  = 0;
                    if ( registry.takeReadbackRequest( texture, mip, serial ) == false )
                        return;
                    _pPendingTargetRead                     = make_unique<PendingRenderTargetRead>();
                    _pPendingTargetRead->_pFactory          = &factory;
                    _pPendingTargetRead->_readback._texture = texture;
                    _pPendingTargetRead->_readback._mip     = mip;
                    _pPendingTargetRead->_readback._serial  = serial;
                }

                SW_PROFILE_SCOPE( "RT.ReadRenderTarget" );
                PendingRenderTargetRead& pending = *_pPendingTargetRead;
                // 실패는 조용히 돌려 UI 가 새 핸들로 다시 요청하게 한다.
                const auto publishFailure = [&]()
                {
                    RenderTargetReadback readback = std::move( pending._readback );
                    readback._byte.clear();
                    readback._bSucceeded = SW_FALSE;
                    _pPendingTargetRead.reset(); // 티켓이 남았으면 버린다
                    registry.publishReadback( std::move( readback ) );
                };

                if ( pending._bSubmitted == SW_FALSE )
                {
                    // 요청한 뒤 트랜지언트가 다시 만들어졌으면(창 크기 변경) 핸들이 죽었다 — 읽지 않는다.
                    if ( registry.isPublished( pending._readback._texture ) == false )
                    {
                        publishFailure();
                        return;
                    }
                    const RHITransferSubmit submit = factory.submitTextureReadback( pending._readback._texture, pending._readback._mip, 0, pending._readback._layout );
                    if ( submit._result == RHISubmitResult::Busy )
                        return; // 스테이징이 찼다 — 같은 요청을 다음 프레임에 다시 낸다
                    if ( submit.isAccepted() == false )
                    {
                        publishFailure();
                        return;
                    }
                    pending._ticket     = submit._ticket;
                    pending._bSubmitted = SW_TRUE;
                    return; // 끝남은 이후 프레임이 폴링한다(요청한 프레임에는 기다리지 않는다)
                }

                const RHITransferStatus status = factory.pollTransfer( pending._ticket );
                if ( status == RHITransferStatus::Pending )
                    return;
                bool bTaken = false;
                if ( status == RHITransferStatus::Complete )
                {
                    bTaken = factory.takeTextureReadback( pending._ticket, pending._readback._byte );
                    if ( bTaken )
                        pending._ticket = RHITransferTicket{}; // 가져갔다 — 소멸자가 버리지 않는다
                }
                if ( bTaken == false )
                {
                    publishFailure();
                    return;
                }
                RenderTargetReadback readback = std::move( pending._readback );
                readback._bSucceeded          = SW_TRUE;
                _pPendingTargetRead.reset();
                registry.publishReadback( std::move( readback ) );
            }

            /**
             * @brief `gpu.inspect` 의 첨부 값 읽기 요청이 있으면 공개된 첨부(이름이 맞는 것, 비면 모두)를 읽어 그 픽셀 값을 `GPUInspect.Pixel` 줄로 냅니다.
             * @details GPU 를 기다리는 읽기다(`RHITransferUtil::readbackTexture2DAndWait`) — 개발 명령 한 번에 한 번이라 프레임 경로의 멈춤을 감수한다(RenderDoc 픽셀 기록과 같은 일회성 조회).
             */
            static void dumpPixelAttachments( IRHIResourceFactory& factory, GPUPixelInspector& inspector, const RenderTargetRegistry& registry )
            {
                uint32 x = 0;
                uint32 y = 0;
                string attachment;
                if ( inspector.takeAttachmentDump( x, y, attachment ) == false )
                    return;
                vector<RenderTargetInfo> listTarget;
                registry.snapshot( listTarget );
                uint32 reported = 0;
                for ( const RenderTargetInfo& target : listTarget )
                {
                    const bool bDepth   = target._bDepth != SW_FALSE;
                    const bool bSkipped = ( attachment.empty() == false && target._name != attachment ) || RenderTargetReadback::isDecodableFormat( target._format, bDepth ) == false;
                    const bool bOutside = x >= target._width || y >= target._height;
                    if ( bSkipped )
                        continue;
                    ++reported;
                    if ( bOutside )
                    {
                        SW_LOG_INFO_ID( "GPUInspect.PixelOutside", "pixel %#,%# is outside '%#' (%#x%#)", x, y, target._name.c_str(), target._width, target._height );
                        continue;
                    }
                    vector<uint8>     bytes;
                    RHITextureMipSpan layout{};
                    float4            value{};
                    const bool        bRead = RHITransferUtil::readbackTexture2DAndWait( factory, target._texture, 0, 0, bytes, layout ) &&
                                       RenderTargetReadback::readPixel( bytes, layout, target._format, bDepth, x, y, value );
                    if ( bRead == false )
                    {
                        SW_LOG_WARNING_ID( "GPUInspect.ReadFailed", "pixel %#,%#: '%#' could not be read back", x, y, target._name.c_str() );
                        continue;
                    }
                    const uint32 channelCount = RenderTargetReadback::getChannelCount( target._format, bDepth );
                    SW_LOG_INFO_ID( "GPUInspect.Pixel", "pixel %#,%# '%#' (%# channels): %# %# %# %#", x, y, target._name.c_str(), channelCount, value._x, value._y, value._z, value._w );
                }
                if ( reported == 0 )
                    SW_LOG_WARNING_ID( "GPUInspect.NoAttachment", "pixel %#,%#: no readable attachment named '%#' is published", x, y, attachment.empty() ? "<any>" : attachment.c_str() );
            }

        private:
            MeshVertexCheck                     _meshVertexCheck;
            RHIContractCheck                    _rhiContractCheck;
            RenderBufferInspector               _bufferInspector;
            GPUNanCheck                         _nanCheck;
            unique_ptr<IRHICommandList>         _nanCheckCommandList;
            vector<RHITextureHandle>            _listScannedTexture; ///< 이번 프레임에 훑은 텍스처(용량을 프레임 사이에 다시 쓴다)
            unique_ptr<PendingRenderTargetRead> _pPendingTargetRead; ///< 요청을 받았고 읽기가 끝나지 않은 렌더 타깃 읽기(렌더 스레드만 쓴다)
        };

        const IRenderDebugChecks::Factory   s_renderDebugChecksFactory{ &RenderDebugChecks::create };
        const IRenderDebugChecks::Registrar s_renderDebugChecksRegistrar{ s_renderDebugChecksFactory };
    } // namespace

    SW_DEV_COMMAND( GPUInspectPixel, "gpu.inspect", "gpu.inspect <x> <y> [attachment] | gpu.inspect off",
                    "Log the attachment values at a pixel and select it for SW_GPU_PRINT (shader prints need -gv_gpuMessages=1)", &GPUDiagnosticsCommandsInternal::runInspect );
    SW_DEV_COMMAND( GPUSelectThread, "gpu.thread", "gpu.thread <dispatch thread>", "Select the compute dispatch thread for SW_GPU_PRINT", &GPUDiagnosticsCommandsInternal::runThread );
    SW_DEV_COMMAND( GPUShowAttachment, "gpu.show", "gpu.show <attachment> | gpu.show off", "Show a pipeline attachment (GBuffer, depth, ...) on screen instead of the normal output",
                    &GPUDiagnosticsCommandsInternal::runShow );
    SW_DEV_COMMAND( GPURHIContract, "gpu.rhicontract", "gpu.rhicontract", "Check the common RHI entry points on the live device (rejections, bindings, readback) and log RHIContractCheck.Done",
                    &GPUDiagnosticsCommandsInternal::runRHIContract );
    SW_DEV_COMMAND( GPUTimings, "gpu.timings", "gpu.timings [on|off]", "Log the last frame GPU time per pass (GPUTiming.Pass), or turn GPU timing on/off", &GPUDiagnosticsCommandsInternal::runTimings );
} // namespace sw
