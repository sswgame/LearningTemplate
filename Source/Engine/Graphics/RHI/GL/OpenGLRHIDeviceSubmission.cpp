#include "pch.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHICommandList.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResourceFactory.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "OpenGL" );

    uint32 OpenGLRHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _bTimestampReady.load( std::memory_order_acquire ) != SW_FALSE ) ? constant::kMaxGPUTimestampSlot : 0u;
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
        const bool bCannotWriteTimestamp = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || slotIndex >= constant::kMaxGPUTimestampSlot;
        if ( bCannotWriteTimestamp )
            return;

        // glQueryCounter 는 Begin/End 쌍이 아니다. 한 번 부르면 "여기까지 GPU 가 끝낸 시각" 이 찍힌다.
        glQueryCounter( _arrTimestampQuery[_timestampFrameIndex * constant::kMaxGPUTimestampSlot + slotIndex], GL_TIMESTAMP );
        _arrTimestampMask[_timestampFrameIndex] |= makeGPUTimestampSlotBit( slotIndex );
    }

    void OpenGLRHIDevice::ensureTimestampQueries()
    {
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_relaxed ) != SW_FALSE )
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
        _bTimestampReady.store( SW_TRUE, std::memory_order_release ); // 위에서 만든 쿼리를 다른 스레드가 본다
    }

    void OpenGLRHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        const GPUTimestampSlotMask writtenMask         = _arrTimestampMask[_timestampFrameIndex];
        const bool                 bNoTimestampsToRead = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _bTimestampReady.load( std::memory_order_acquire ) == SW_FALSE || writtenMask == 0;
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
            // glFinish 가 낸 전송도 모두 끝냈다. 펜스를 놓고 워터마크를 올린다.
            _transferQueue.retireAll( []( void*& pSync )
            {
                glDeleteSync( static_cast<GLsync>( pSync ) ); // 펜스 없는 항목(glFenceSync 실패)은 0 이라 GL 이 무시한다
                pSync = nullptr;
            } );
        }
        _releaseQueue.flushAll();
    }

    uint64 OpenGLRHIDevice::endTransfer()
    {
        // 펜스는 앞선 모든 명령의 끝을 알린다. glFlush 로 명령을 GPU 로 내보내야 프레임을 돌리지 않아도 신호가 선다.
        void* pSync = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
        glFlush();
        // 펜스가 없으면 이 전송이 끝났는지 알 길이 없다 — 끝난 것으로 치지 않고 장치 손실로 둔다(티켓은 Failed, 이후 제출은 Rejected).
        if ( pSync == nullptr && _transferSerials.markLost() )
            SW_LOG_ERROR( "endTransfer: glFenceSync returned no fence; treating the GL device as lost" );
        const uint64 serial = _transferQueue.enqueue( pSync );
        retireFinishedTransfers(); // 앞서 낸 전송 가운데 끝난 것을 모은다(이 스레드가 컨텍스트를 쥐고 있다)
        return serial;
    }

    void OpenGLRHIDevice::retireFinishedTransfers()
    {
        // 큐의 펜스는 모두 endTransfer 가 단 것이다. 펜스가 없는 항목(glFenceSync 실패)은 glClientWaitSync 가 GL_WAIT_FAILED 로 돌려 손실로 남는다.
        _transferQueue.retireDone(
            [this]( void*& pSync )
        {
            const GLenum waitResult = glClientWaitSync( static_cast<GLsync>( pSync ), 0, 0 );
            if ( waitResult == GL_WAIT_FAILED )
            {
                // 실패한 항목은 큐 맨 앞에 남아 매 프레임 다시 여기로 온다 — 처음 표시한 때만 로그를 남긴다.
                if ( _transferSerials.markLost() )
                    SW_LOG_ERROR( "pollTransfer: glClientWaitSync failed on a transfer fence" );
                return false;
            }
            return waitResult != GL_TIMEOUT_EXPIRED;
        },
            []( void*& pSync )
        {
            glDeleteSync( static_cast<GLsync>( pSync ) ); // 끝났다고 본 펜스만 온다 — 펜스가 없는 항목은 위에서 끝나지 않는다
            pSync = nullptr;
        } );
        // 장부가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다. 버퍼는 컨텍스트가 있어야 지우므로 컨텍스트를 쥔 이곳(프레임 끝 · 제출 · 폴링)에서만 한다.
        const uint64 watermark = _transferSerials.getWatermark();
        deleteIdleSoloBuffer( _stagingRing, _stagingSolo, watermark );
        deleteIdleSoloBuffer( _readbackRing, _readbackSolo, watermark );
    }

    void OpenGLRHIDevice::deleteIdleSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, uint64 completedSerial )
    {
        if ( rSolo._buffer == 0 )
            return;
        ring.freeCompleted( completedSerial );
        if ( ring.hasLiveSolo() )
            return;
        deleteSoloBuffer( rSolo );
    }

    void OpenGLRHIDevice::deleteSoloBuffer( OpenGLSoloTransferBuffer& rSolo )
    {
        if ( rSolo._buffer == 0 )
            return;
        GLuint name = rSolo._buffer;
        glDeleteBuffers( 1, &name ); // 매핑된 버퍼를 지우면 매핑도 풀린다
        getMemoryLedger().recordFree( RHIMemoryKey::makeDeviceObject( &rSolo ) );
        rSolo = OpenGLSoloTransferBuffer{};
    }

    RHISubmitResult OpenGLRHIDevice::attachSoloBuffer( RHIStagingAllocator& ring, OpenGLSoloTransferBuffer& rSolo, const RHIStagingSpan& span, uint32 mapFlags,
                                                       const utf8* pCaller )
    {
        // 버퍼를 만드는 동안만 컨텍스트를 빌린다. 바이트 복사(업로드)는 돌아간 뒤 컨텍스트 밖에서 한다.
        ScopedOpenGLContext contextScope( this );
        if ( isGraphicsContextCurrent() == false )
        {
            ring.free( span );
            SW_LOG_ERROR( "%#: the GL context could not be acquired to create a %#-byte single transfer buffer; submit again later", pCaller, span._sizeBytes );
            return RHISubmitResult::Busy;
        }
        deleteSoloBuffer( rSolo ); // 장부가 이미 돌려받았지만 아직 지우지 못한 앞 단독 버퍼
        rSolo._pMapped = createPersistentRingBuffer( span._sizeBytes, mapFlags, rSolo._buffer );
        if ( rSolo._pMapped == nullptr )
        {
            ring.free( span );
            SW_LOG_ERROR( "%#: a %#-byte single transfer buffer could not be created or mapped; the transfer is rejected", pCaller, span._sizeBytes );
            return RHISubmitResult::Rejected;
        }
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( &rSolo ), RHIMemoryKind::Staging, span._sizeBytes );
        return RHISubmitResult::Accepted;
    }

    uint8* OpenGLRHIDevice::getStagingBytes( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _stagingSolo._pMapped : _pStagingRingMapped + span._offsetBytes;
    }

    uint32 OpenGLRHIDevice::getStagingBuffer( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _stagingSolo._buffer : _stagingRingBuffer;
    }

    const uint8* OpenGLRHIDevice::getReadbackBytes( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _readbackSolo._pMapped : _pReadbackRingMapped + span._offsetBytes;
    }

    uint32 OpenGLRHIDevice::getReadbackBuffer( const RHIStagingSpan& span ) const
    {
        return span._bSolo != SW_FALSE ? _readbackSolo._buffer : _readbackRingBuffer;
    }

    bool OpenGLRHIDevice::tryRetireFinishedTransfers()
    {
        if ( isGraphicsContextCurrent() )
        {
            retireFinishedTransfers();
            return true;
        }
        const bool bBorrowed = requiresExclusiveContextThread() && tryMakeCurrentAndRecordOwner();
        if ( bBorrowed == false )
            return false;
        retireFinishedTransfers();
        unbindGraphicsContext();
        return true;
    }

    RHISubmitResult OpenGLRHIDevice::allocateStagingSpan( uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        if ( _stagingRingBuffer == 0 )
        {
            SW_LOG_ERROR( "%#: the GL staging ring is unavailable (needs OpenGL 4.4 / ARB_buffer_storage); the upload is rejected", pCaller );
            return RHISubmitResult::Rejected;
        }
        RHIStagingAllocateResult result = _stagingRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full && tryRetireFinishedTransfers() )
            result = _stagingRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        // 결과 옮기기는 RHITransferSerials 한 곳이다 — 장치를 잃었으면(펜스 확인이 GL_WAIT_FAILED) 링이 다시 비지 않으므로 Full 도 Busy 가 아니라 Rejected 다(계약 2).
        const RHISubmitResult submitResult = _transferSerials.classifyStagingAllocation( result );
        if ( submitResult == RHISubmitResult::Accepted )
        {
            if ( outSpan._bSolo == SW_FALSE )
                return RHISubmitResult::Accepted;
            return attachSoloBuffer( _stagingRing, _stagingSolo, outSpan, GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, pCaller );
        }
        if ( submitResult == RHISubmitResult::Busy )
            return RHISubmitResult::Busy; // 앞선 전송이 끝나면 빈다 — 호출자가 다시 제출한다(계약 2)
        if ( result == RHIStagingAllocateResult::Allocated )
            _stagingRing.free( outSpan );
        if ( result == RHIStagingAllocateResult::TooLarge )
        {
            SW_LOG_ERROR( "%#: %# bytes exceed the %#-byte staging ring and the %#-byte single upload limit; raise gv_rhiStagingSingleMaxMegabytes to upload it", pCaller,
                          sizeBytes, _stagingRing.getCapacityBytes(), _stagingRing.getSoloMaxBytes() );
        }
        else
        {
            SW_LOG_ERROR( "%#: the GL device is lost; the staging ring no longer drains and the upload is rejected", pCaller );
        }
        return RHISubmitResult::Rejected;
    }

    RHISubmitResult OpenGLRHIDevice::allocateReadbackSpan( uint64 sizeBytes, RHIStagingSpan& outSpan )
    {
        if ( _readbackRingBuffer == 0 )
        {
            SW_LOG_ERROR( "submitTextureReadback: the GL readback ring is unavailable (needs OpenGL 4.4 / ARB_buffer_storage); the readback is rejected" );
            return RHISubmitResult::Rejected;
        }
        // 워터마크는 버린 되읽기(markSubmitted)의 구간만 돌려준다 — 그래서 차 있으면 펜스를 한 번 모아 본다. 가져갈 되읽기의 구간은 take 가 free 로 놓는다.
        RHIStagingAllocateResult result = _readbackRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full && tryRetireFinishedTransfers() )
            result = _readbackRing.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        switch ( result )
        {
            case RHIStagingAllocateResult::Allocated:
            {
                if ( outSpan._bSolo == SW_FALSE )
                    return RHISubmitResult::Accepted;
                return attachSoloBuffer( _readbackRing, _readbackSolo, outSpan, GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT, "submitTextureReadback" );
            }
            case RHIStagingAllocateResult::Full:
            {
                return RHISubmitResult::Busy; // 앞선 되읽기를 가져가거나 버리면 빈다 — 호출자가 다시 제출한다(계약 2)
            }
            case RHIStagingAllocateResult::TooLarge:
            {
                SW_LOG_ERROR( "submitTextureReadback: %# bytes exceed the %#-byte readback ring and the %#-byte single readback limit; raise gv_rhiStagingSingleMaxMegabytes to read it",
                              sizeBytes, _readbackRing.getCapacityBytes(), _readbackRing.getSoloMaxBytes() );
                return RHISubmitResult::Rejected;
            }
        }
        return RHISubmitResult::Rejected;
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
        // 프레임을 돌리는 스레드는 컨텍스트를 쥐고 있으니 여기서 전송 워터마크도 올린다(컨텍스트 없는 스레드의 폴링은 이 값을 본다).
        retireFinishedTransfers();
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
