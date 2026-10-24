#include "pch.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    uint32 OpenGLRHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled != SW_FALSE && _bTimestampReady != SW_FALSE ) ? constant::kMaxGPUTimestampSlot : 0u;
    }

    bool OpenGLRHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool OpenGLRHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        // GL_TIMESTAMP 는 기다리지 않고 지금 GPU 시계(나노초, 쿼리 타임스탬프와 같은 시계)를 준다. 컨텍스트를 소유한 스레드에서만 부른다.
        if ( _bInitialized == SW_FALSE || glGetInteger64v == nullptr )
            return false;
        GLint64 now{ 0 };
        glGetInteger64v( GL_TIMESTAMP, &now );
        if ( now <= 0 )
            return false;
        outGPUNanos = RHITimestamp::convertTickToNanos( static_cast<uint64>( now ), 1.0 );
        return true;
    }

    void OpenGLRHIDevice::writeTimestampSlot( uint32 slotIndex )
    {
        const bool bCannotWriteTimestamp = _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE || slotIndex >= constant::kMaxGPUTimestampSlot;
        if ( bCannotWriteTimestamp )
            return;

        // glQueryCounter 는 Begin/End 쌍이 아니다. 한 번 부르면 "여기까지 GPU 가 끝낸 시각" 이 찍힌다.
        glQueryCounter( _arrTimestampQuery[_timestampFrameIndex * constant::kMaxGPUTimestampSlot + slotIndex], GL_TIMESTAMP );
        _arrTimestampMask[_timestampFrameIndex] |= makeGPUTimestampSlotBit( slotIndex );
    }

    void OpenGLRHIDevice::ensureTimestampQueries()
    {
        if ( _bTimestampEnabled == SW_FALSE || _bTimestampReady != SW_FALSE )
            return;
        // GL 3.3 코어이지만 로더가 채우지 못했을 수 있다. 없으면 이 백엔드는 조용히 보고하지 않는다.
        const bool bQueryFunctionsMissing = glGenQueries == nullptr || glQueryCounter == nullptr || glGetQueryObjectui64v == nullptr ||
                                            glGetQueryObjectiv == nullptr;
        if ( bQueryFunctionsMissing )
            return;

        constexpr uint32 kQueryCount = constant::kMaxGPUTimestampSlot * constant::kMaxFrameCountInFlight;
        glGenQueries( static_cast<GLsizei>( kQueryCount ), _arrTimestampQuery );
        if ( _arrTimestampQuery[0] == 0 )
            return;
        _bTimestampReady = SW_TRUE;
    }

    void OpenGLRHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        const GPUTimestampSlotMask writtenMask         = _arrTimestampMask[_timestampFrameIndex];
        const bool                 bNoTimestampsToRead = _bTimestampEnabled == SW_FALSE || _bTimestampReady == SW_FALSE || writtenMask == 0;
        if ( bNoTimestampsToRead )
            return;

        const uint32         base = _timestampFrameIndex * constant::kMaxGPUTimestampSlot;
        uint64               arrTick[constant::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{ 0 };
        for ( uint32 slotIndex = 0; slotIndex < constant::kMaxGPUTimestampSlot; ++slotIndex )
        {
            if ( ( writtenMask & makeGPUTimestampSlotBit( slotIndex ) ) == 0 )
                continue;

            // **가용 여부부터 묻는다.** 바로 결과를 읽으면 GL 이 그 자리에서 GPU 를 기다려,
            // 측정하려던 파이프라인을 멈춰 세운다. 그러면 측정값이 거짓이 된다.
            GLint bAvailable{ 0 };
            glGetQueryObjectiv( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT_AVAILABLE, &bAvailable );
            if ( bAvailable == GL_FALSE )
                continue;

            GLuint64 tick{ 0 };
            glGetQueryObjectui64v( _arrTimestampQuery[base + slotIndex], GL_QUERY_RESULT, &tick );
            arrTick[slotIndex] = tick;
            readyMask |= makeGPUTimestampSlotBit( slotIndex );
        }
        // GL 타임스탬프는 나노초다.
        RHITimestamp::resolve( arrTick, readyMask, 1.0, _timestampFrame );
    }

    void OpenGLRHIDevice::beginFrame()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _platformContext != nullptr )
            _platformContext->reacquireForFrame();

        // 백버퍼(FBO 0) 바인딩과 클리어는 여기서 하지 않는다. beginFrame 은 프레임 수명주기(GL 은
        // 컨텍스트 확보)만 맡고, 백버퍼를 바인딩하고 클리어하는 것은 RenderThread 가 여는 명시적
        // beginRenderPass(핸들 0) 뿐이다. 뷰포트만 창 크기 전체로 되돌려 둔다.
        glViewport( 0, 0, static_cast<GLsizei>( _width ), static_cast<GLsizei>( _height ) );

        // 이 슬롯은 곧 다시 쓴다. 덮어쓰기 전에 지난 바퀴의 결과를 한 번만 묻는다.
        ensureTimestampQueries();
        collectTimestampsForSlot();
        _arrTimestampMask[_timestampFrameIndex] = 0;
    }

    void OpenGLRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // present 여부와 무관하게 이번 프레임의 슬롯은 다 기록됐다. 다음 슬롯으로 넘긴다.
        _timestampFrameIndex = ( _timestampFrameIndex + 1 ) % constant::kMaxFrameCountInFlight;

        if ( bPresent == false )
        {
            retireFrameOnGPU();
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
        // 반드시 필요하고, GLX 는 하지 않는다).
        if ( _platformContext != nullptr )
            _platformContext->present();
        retireFrameOnGPU();
        _releaseQueue.tickFrame();
    }

    void OpenGLRHIDevice::waitIdleInternal()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        {
            ScopedOpenGLContext contextScope( this );
            glFinish();
            // GPU 가 쉰다 — 낸 프레임이 모두 끝났으니 펜스를 놓는다.
            for ( void*& rpSync : _arrFrameDoneSync )
            {
                if ( rpSync != nullptr )
                    glDeleteSync( static_cast<GLsync>( rpSync ) );
                rpSync = nullptr;
            }
            _completedFrameSerial = _frameSerial;
        }
        _releaseQueue.flushAll();
    }

    void OpenGLRHIDevice::retireFrameOnGPU()
    {
        ScopedOpenGLContext contextScope( this );
        const uint64        serial     = _frameSerial + 1;
        void*&              rpSlotSync = _arrFrameDoneSync[( serial - 1 ) % constant::kMaxFrameCountInFlight];
        // 링이 한 바퀴 돌았다. 이 칸은 프레임 serial - kMaxFrameCountInFlight 의 것이니, 그 프레임이 끝나야 칸을 다시 쓴다.
        const bool bRingFull = serial - _completedFrameSerial > constant::kMaxFrameCountInFlight;
        if ( bRingFull )
        {
            constexpr GLuint64 kRingWaitTimeoutNanoseconds = 10ull * 1000ull * 1000ull * 1000ull;
            const GLenum       waitResult                  = glClientWaitSync( static_cast<GLsync>( rpSlotSync ), GL_SYNC_FLUSH_COMMANDS_BIT, kRingWaitTimeoutNanoseconds );
            if ( waitResult == GL_TIMEOUT_EXPIRED || waitResult == GL_WAIT_FAILED )
                SW_LOG_ERROR( "endFrame: the GPU did not finish frame %# (glClientWaitSync result %#)", serial - constant::kMaxFrameCountInFlight, static_cast<uint32>( waitResult ) );
            glDeleteSync( static_cast<GLsync>( rpSlotSync ) );
            rpSlotSync            = nullptr;
            _completedFrameSerial = serial - constant::kMaxFrameCountInFlight;
        }
        rpSlotSync   = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
        _frameSerial = serial;

        // 이미 끝난 프레임을 오래된 순서로 모은다(기다리지 않고 묻기만 한다).
        while ( _completedFrameSerial < _frameSerial )
        {
            const uint64 next       = _completedFrameSerial + 1;
            void*&       rpNextSync = _arrFrameDoneSync[( next - 1 ) % constant::kMaxFrameCountInFlight];
            const GLenum pollResult = glClientWaitSync( static_cast<GLsync>( rpNextSync ), 0, 0 );
            if ( pollResult == GL_TIMEOUT_EXPIRED )
                break;
            glDeleteSync( static_cast<GLsync>( rpNextSync ) );
            rpNextSync            = nullptr;
            _completedFrameSerial = next;
        }
        _releaseQueue.tickCompleted( _completedFrameSerial );
    }

    void OpenGLRHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameSerial + 1 );
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
