#include "pch.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/Support/RHIGpuTimestamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    namespace
    {
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    uint32 OpenGLRHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled != SW_FALSE && _bTimestampReady != SW_FALSE ) ? constant::kMaxGpuTimestampSlot : 0u;
    }

    bool OpenGLRHIDevice::readTimestampsMicros( vector<float32>& outListMicro )
    {
        outListMicro = _listTimestampMicro;
        return outListMicro.empty() == false;
    }

    void OpenGLRHIDevice::writeTimestampSlot( uint32 slotIndex )
    {
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE || slotIndex >= constant::kMaxGpuTimestampSlot )
            return;

        // glQueryCounter 는 Begin/End 쌍이 아니다. 한 번 부르면 "여기까지 GPU 가 끝낸 시각" 이 찍힌다.
        glQueryCounter( _arrTimestampQuery[_timestampFrameIndex * constant::kMaxGpuTimestampSlot + slotIndex], GL_TIMESTAMP );
        _arrTimestampMask[_timestampFrameIndex] |= ( 1u << slotIndex );
    }

    void OpenGLRHIDevice::ensureTimestampQueries()
    {
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady != SW_FALSE )
            return;
        // GL 3.3 코어이지만 로더가 못 채웠을 수 있다. 없으면 이 백엔드는 조용히 보고하지 않는다.
        if ( glGenQueries == nullptr || glQueryCounter == nullptr || glGetQueryObjectui64v == nullptr ||
             glGetQueryObjectiv == nullptr )
            return;

        constexpr uint32 kQueryCount = constant::kMaxGpuTimestampSlot * constant::kMaxFrameCountInFlight;
        glGenQueries( static_cast<GLsizei>( kQueryCount ), _arrTimestampQuery );
        if ( _arrTimestampQuery[0] == 0 )
            return;
        _bTimestampReady = SW_TRUE;
    }

    void OpenGLRHIDevice::collectTimestampsForSlot()
    {
        _listTimestampMicro.clear();
        const uint32 writtenMask = _arrTimestampMask[_timestampFrameIndex];
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE || writtenMask == 0 )
            return;

        const uint32 base = _timestampFrameIndex * constant::kMaxGpuTimestampSlot;
        uint64       arrTick[constant::kMaxGpuTimestampSlot]{};
        uint32       readyMask{ 0 };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGpuTimestampSlot; ++slotIndex )
        {
            if ( ( writtenMask & ( 1u << slotIndex ) ) == 0 )
                continue;

            // **가용 여부부터 묻는다.** 바로 결과를 읽으면 GL 이 그 자리에서 GPU 를 기다려,
            // 재려던 파이프라인을 멈춰 세운다. 그러면 숫자가 거짓이 된다.
            GLint bAvailable{ 0 };
            glGetQueryObjectiv( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT_AVAILABLE, &bAvailable );
            if ( bAvailable == GL_FALSE )
                continue;

            GLuint64 tick{ 0 };
            glGetQueryObjectui64v( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT, &tick );
            arrTick[slotIndex] = tick;
            readyMask |= ( 1u << slotIndex );
        }
        // GL 타임스탬프는 나노초다.
        RHIGpuTimestamp::resolveMicro( arrTick, readyMask, 0.001, _listTimestampMicro );
    }

    void OpenGLRHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _platformContext != nullptr )
            _platformContext->reacquireForFrame();

        // 백버퍼(FBO 0) 바인딩과 클리어는 여기서 하지 않는다. beginFrame 은 프레임 수명주기(GL 은
        // 컨텍스트 확보)만 맡고, 백버퍼 타깃팅은 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). 뷰포트만 창 크기 전체로 되돌려 둔다.
        (void)clearColor;
        glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );

        // 이 묶음은 곧 다시 쓴다. 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampQueries();
        collectTimestampsForSlot();
        _arrTimestampMask[_timestampFrameIndex] = 0;
    }

    void OpenGLRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // present 여부와 무관하게 이번 프레임의 칸은 다 찍혔다. 다음 묶음으로 넘긴다.
        _timestampFrameIndex = ( _timestampFrameIndex + 1 ) % constant::kMaxFrameCountInFlight;

        if ( bPresent == false )
        {
            _releaseQueue.tickFrame();
            return;
        }

        const int8 desired = vsync ? 1 : 0;
        if ( _lastVsync != desired )
        {
            if ( _platformContext != nullptr )
                _platformContext->setSwapInterval( vsync );
            _lastVsync = desired;
        }

        // 스왑 직전 컨텍스트 되찾기는 플랫폼이 필요할 때만 한다(WGL 은 ImGui 멀티 뷰포트 때문에
        // 반드시 필요하고, GLX · NSGL 은 예전에도 하지 않았다).
        if ( _platformContext != nullptr )
            _platformContext->present();
        _releaseQueue.tickFrame();
    }

    void OpenGLRHIDevice::waitIdle()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        ScopedOpenGLContext ctxScope( this );
        glFinish();
        _releaseQueue.flushAll();
    }

    unique_ptr<IRHICommandList> OpenGLRHIDevice::createCommandList()
    {
        return make_unique<OpenGLRHICommandList>( this );
    }

    void OpenGLRHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        // 기록이 이미 GL 호출로 즉시 나갔으므로(beginCommandList 에서 컨텍스트 재바인딩까지 마침)
        // 순서를 맞출 일은 없다. 다만 GL 도 커맨드를 모아 뒀다가 보내므로, 즉시 모드에서는
        // 리스트 경계마다 밀어내 오류가 어느 리스트에서 났는지 드러나게 한다.
        (void)pCmdList;
        if ( _bImmediateSubmit )
            glFlush();
    }
} // namespace sw
