/**
 * @file OpenGLRHIDevice.h
 * @brief OpenGL 4.6 코어 프로파일 RHI 백엔드 디바이스를 정의합니다.
 * @note glad(GL 심볼)는 이 헤더가 아니라 GL 의 .cpp 들이 직접, 또는 OpenGLRHIDeviceInternal.h 를 거쳐 include 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"

namespace sw
{
    class IOpenGLPlatformContext;
    class OpenGLRHICommandContext;
    class OpenGLRHIResource;

    /**
     * @struct OpenGLRecordingState
     * @brief "지금 GL 컨텍스트에 무엇이 걸려 있나" 를 담는 바인딩 캐시 한 자리입니다.
     * @details **다른 백엔드와 달리 이것은 디바이스가 소유합니다. 그게 맞습니다.** DX12 · Vulkan · DX11 은
     *          리스트마다 독립된 기록 스트림(커맨드 리스트 / Deferred Context)을 갖고 기록을 나중에
     *          재생하므로 캐시도 리스트마다 있어야 합니다. 전역이면 서로의 캐시를 덮습니다(DX11 에서
     *          실제로 그 일이 일어났습니다). OpenGL 은 커맨드 버퍼가 없는 **스레드 종속 상태 머신**이고
     *          `OpenGLRHICommandList` 는 호출을 즉시 GL API 로 흘려보냅니다. 실제 상태가 하나뿐이므로
     *          그것을 비추는 캐시도 하나여야 합니다. 리스트마다 두면 캐시가 진짜 GL 상태와 어긋납니다.
     *          (그래서 GL 은 `_bParallelCommandRecording = 0` 입니다.)
     */
    struct OpenGLRecordingState
    {
        RHIBufferHandle _boundMeshVb{ 0 };
        uint32          _boundMeshStride{ 0 };
        uint32          _boundMeshOffset{ 0 };
        RHIBufferHandle _boundInstanceSlotVb{ 0 }; ///< 슬롯 1: 인스턴스 슬롯 스트림 (0 = 안 걸림)
        uint32          _boundInstanceSlotOffset{ 0 };
        RHIBufferHandle _boundIndexBuffer{ 0 };
        uint32          _boundIndexStride{ 4 };
        uint32          _boundIndexOffset{ 0 };

        RHIPipelineStateHandle _boundGraphicsPso{ 0 };
        RHIPipelineStateHandle _boundComputePso{ 0 }; ///< setComputePipelineState 가 마지막으로 건 컴퓨트 PSO. dispatchCompute 는 이 프로그램을 쓴다
        /// @brief 지금 텍스처가 걸려 있는 유닛 비트마스크(비트 i = 유닛 i)입니다. beginRenderPass 가 이 유닛들만 뗍니다.
        uint32 _boundTextureUnitMask{ 0 };
    };

    /**
     * @class OpenGLRHIDevice
     * @brief OpenGL 4.6 그래픽스 · 컴퓨트 디바이스 구현입니다(SSBO · UBO 바인딩).
     */
    class OpenGLRHIDevice : public IRHIDevice
    {
        friend class OpenGLRHICommandContext;

    public:
        RHIBufferHandle createBuffer( const RHIBufferDesc& desc );
        RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride );
        friend class OpenGLRHIResource;
        /** @brief 빈 OpenGL 디바이스를 만듭니다. */
        OpenGLRHIDevice();
        /** @brief 컨텍스트와 GL 객체를 정리합니다. */
        virtual ~OpenGLRHIDevice() override;

        /** @brief 플랫폼 GL 컨텍스트(WGL · GLX · NSGL)를 만들고 glad 로 GL 함수를 불러옵니다. */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief GL 객체와 컨텍스트를 해제합니다. */
        void shutdownInternal() override;

        /** @brief 크기를 적고 glViewport 를 맞춥니다. */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임을 엽니다(컨텍스트 되찾기 · 뷰포트 · 타임스탬프 수거). 백버퍼 클리어는 beginRenderPass(핸들 0)가 합니다. */
        void beginFrame( const float4& clearColor ) override;

        /** @brief 프레임을 닫고, bPresent 면 플랫폼 컨텍스트로 백버퍼를 내보냅니다(SwapBuffers 등). */
        void endFrame( bool vsync, bool bPresent = true ) override;

        void   setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32 getTimestampSlotCount() const override;
        bool   readTimestampsMicros( vector<float32>& outListMicro ) override;
        /**
         * @brief 커맨드 리스트가 부르는 기록 지점입니다. GL 은 커맨드 버퍼가 없어 그 자리에서 발행합니다.
         * @details 그래서 락이 없습니다. GL 호출은 컨텍스트를 쥔 한 스레드에서만 나갑니다.
         */
        void writeTimestampSlot( uint32 slotIndex );

        IRHIResource* getResource() override;
        /** @brief 프레임 스트림 컨텍스트입니다. 백버퍼 패스 · Present 가 여기에 기록합니다. */
        IRHICommandContext* getFrameStreamContext() override;

        /** @brief GPU 가 끝날 때까지 기다리고(glFinish) 지연 해제 큐를 비웁니다. */
        void waitIdle() override;

        /** @brief 백엔드 타입(OpenGL)을 반환합니다. */
        RHIBackend getBackendType() const override { return RHIBackend::OpenGL; }

        /** @brief VS 가 GPUScene 인스턴스 버퍼를 SSBO(g_SwInstances, t4)로 읽을 수 있어 true 입니다(glBindBufferBase). */
        bool supportsInstancedSceneDraw() const override { return true; }

        /** @brief RHI 텍스처 핸들에 대응하는 GL 텍스처 이름을 반환합니다. 없으면 0 입니다. */
        uint32 getGlTextureName( RHITextureHandle texture ) const;

        /** @brief 네이티브 GL 텍스처 이름을 반환합니다. */
        uint32 getNativeTextureName( RHITextureHandle texture ) const override { return getGlTextureName( texture ); }

        /** @brief 네이티브 GL 텍스처 핸들을 포인터 형태로 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override { return reinterpret_cast<void*>( static_cast<uintptr_t>( getGlTextureName( texture ) ) ); }

        /** @brief 백엔드 이름 문자열을 반환합니다. */
        const utf8* getBackendName() const override { return "OpenGL (glad 4.6 Core)"; }

        /** @brief 플랫폼 디바이스 컨텍스트(HDC · Display* · NSView)를 반환합니다. */
        void* getNativeDevice() const override { return _pHDC; }

        /** @brief 플랫폼 렌더 컨텍스트(HGLRC · GLXContext · NSOpenGLContext)를 반환합니다. */
        void* getNativeContext() const override { return _pHRC; }

        bool requiresExclusiveContextThread() const override { return true; }

        /** @brief 컨텍스트를 기다리다 포기하는 시간입니다. 렌더 워커는 프레임 끝마다 놓으므로 한 프레임이면 넉넉합니다. */
        static constexpr uint32 kContextAcquireTimeoutMs = 250;

        /**
         * @brief 컨텍스트를 이 스레드로 가져옵니다. 다른 스레드가 쥐고 있으면 놓을 때까지 기다립니다.
         * @details GL 컨텍스트는 한 스레드만 current 로 가질 수 있습니다. 렌더 워커는 프레임이 끝나면
         *          놓으므로(`RenderThread::executePacket`) 기다리면 한 프레임 안에 차례가 옵니다.
         *          그래서 `bindGraphicsContext` 처럼 한 번 시도하고 포기하는 대신 조용히 다시 집습니다.
         * @param timeoutMs 포기까지 기다리는 시간(ms).
         * @return 가져왔으면 true. 제한 시간을 넘기면 false 이며 그때만 로그를 남깁니다.
         */
        bool acquireGraphicsContextBlocking( uint32 timeoutMs = kContextAcquireTimeoutMs );
        bool bindGraphicsContext() override;
        /** @brief 이 스레드에 이미 우리 컨텍스트가 current 인지 묻습니다. ScopedOpenGLContext 가 남의 바인딩을 풀지 않게 하는 근거입니다. */
        bool isGraphicsContextCurrent() const;
        /** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
        void unbindGraphicsContext() override;

        /** @brief OpenGL 에는 커맨드 큐가 없어 nullptr 를 반환합니다. */
        void* getNativeCommandQueue() const override { return nullptr; }

        /** @brief 커맨드 리스트를 만듭니다. */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트를 제출합니다. GL 은 기록이 곧 실행이라 즉시 모드에서만 glFlush 합니다. */
        void executeCommandList( IRHICommandList* pCmdList ) override;

    private:
        /** @brief 타임스탬프 쿼리 객체를 한 번만 만듭니다. */
        void ensureTimestampQueries();
        /** @brief 다시 쓰기 직전의 묶음에서 결과를 마이크로초로 풉니다 (기다리지 않습니다). */
        void collectTimestampsForSlot();

        /** @brief 컴퓨트 루트 상수 UBO 를 확보합니다. */
        bool ensureComputeRootConstantUbo();
        /** @brief MRT 합성 FBO 를 확보합니다. */
        uint32 ensureCompositeFboMrt( const RHITextureHandle* pColor, uint32 colorCount, RHITextureHandle depth );
        /** @brief 불투명 핸들을 GLuint 이름으로 풉니다. */
        uint32 resolveGlBuffer( RHIBufferHandle handle ) const;
        /** @brief GL 버퍼 이름을 테이블에 넣고 핸들을 반환합니다. */
        RHIBufferHandle storeGlBuffer( uint32 glName );
        struct OpenGLTextureRecord;
        /** @brief 불투명 텍스처 핸들을 OpenGLTextureRecord 로 풉니다. */
        OpenGLTextureRecord*       resolveTexture( RHITextureHandle handle );
        const OpenGLTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief setComputeRootConstants 의 실제 용량(dword)입니다. RHITypes.h 의
         *         constant::kMinComputeRootConstantDwords(DX12 기준, 네 백엔드 공통 안전값) 참고. */
        static constexpr uint32 kMaxComputeRootConstantDwords = 64;

        /// @brief 드로우 때 바인드할 버퍼 · 텍스처 슬롯입니다.
        struct BindlessResourceRecord
        {
            RHIBufferHandle  _buffer{ 0 };
            RHITextureHandle _texture{ 0 }; ///< UAV 등록부에서 RW 텍스처(이미지 유닛)면 0 이 아니다

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessResourceRecord& other ) const
            {
                return _buffer == other._buffer && _texture == other._texture;
            }
            bool operator!=( const BindlessResourceRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief GLuint 텍스처와 타깃 · 포맷입니다.
        struct OpenGLTextureRecord
        {
            uint32    _texture{ 0 };
            uint32    _fbo{ 0 };
            uint32    _width{ 0 };
            uint32    _height{ 0 };
            uint32    _mipLevels{ 1 };
            RHIFormat _format = RHIFormat::R8G8B8A8_UNORM;
            uint32    _internalFormat{ 0 }; ///< GL 내부 포맷. glBindImageTexture 가 쓴다
            uint8     _bDepthStencil : 1;
            uint8     _bUAV          : 1;
            uint8     _reserved      : 6;
        };

        /// @brief MRT FBO 캐시 키입니다.
        struct CompositeFboKey
        {
            RHITextureHandle _arrColor[kMaxColorAttachments]{};
            uint32           _colorCount{ 0 };
            RHITextureHandle _depth{ 0 };
            /** @brief 키가 같으면 true 를 반환합니다. */
            bool operator==( const CompositeFboKey& other ) const
            {
                if ( _colorCount != other._colorCount || _depth != other._depth )
                    return false;
                for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
                {
                    if ( _arrColor[colorIndex] != other._arrColor[colorIndex] )
                        return false;
                }
                return true;
            }
        };

        /// @brief CompositeFboKey 의 해시입니다.
        struct CompositeFboKeyHash
        {
            /** @brief 키의 해시를 반환합니다. */
            size_t operator()( const CompositeFboKey& key ) const
            {
                size_t hash = static_cast<size_t>( key._depth ) * 1315423911u;
                hash ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
                for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
                {
                    hash ^= static_cast<size_t>( key._arrColor[colorIndex] ) + 0x9e3779b9u + ( hash << 6 ) + ( hash >> 2 );
                }
                return hash;
            }
        };

        /// @brief bindless 텍스처 인덱스 한 칸입니다(인덱스 = 목록 위치, 값 = 텍스처 핸들).
        struct BindlessTextureRecord
        {
            RHITextureHandle _texture{ 0 };

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessTextureRecord& other ) const { return _texture == other._texture; }
            bool operator!=( const BindlessTextureRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief 프로그램과 래스터 · 블렌드 · 깊이 상태입니다.
        struct OpenGLPipelineStateRecord
        {
            uint32               _program{ 0 };
            uint32               _vao{ 0 };
            RHIPrimitiveTopology _topology = RHIPrimitiveTopology::TriangleList;
            RHIFillMode          _fillMode = RHIFillMode::Solid;
            RHICullMode          _cullMode = RHICullMode::None;
            uint8                _bEnableDepthTest  : 1;
            uint8                _bEnableDepthWrite : 1;
            uint8                _bEnableBlend      : 1;
            uint8                _reserved          : 5;
        };

        /// @brief 렌더 패스 서술 캐시입니다.
        struct OpenGLRenderPassRecord
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive   : 1;
            uint8             _reserved : 7;
        };

        /** @brief 플랫폼 GL 컨텍스트(WGL · GLX · NSGL)입니다. 생성 · 바인딩 · 프레젠트 · VSync 를 모두 여기가 압니다. */
        unique_ptr<IOpenGLPlatformContext> _platformContext;

        /** @brief getNativeDevice 계약용 복사본입니다. 실제 소유는 _platformContext 입니다. */
        void*  _pHDC;
        void*  _pHRC;
        void*  _pHWnd;
        uint32 _width;
        uint32 _height;
        uint32 _shaderProgram;
        uint32 _vao;
        uint32 _vbo;     ///< 풀스크린 삼각형(정점 3개)
        uint32 _meshVao; ///< 씬 메시 드로우용 VAO
        uint32 _defaultSampler;
        uint32 _defaultTexture;

        RHIHandleTable<uint32> _gpuBuffers;
        /// @brief 기록 상태입니다. GL 은 실제 상태가 하나라 리스트도 이것을 함께 씁니다(OpenGLRecordingState 참고).
        OpenGLRecordingState _recordingState;

        vector<BindlessResourceRecord> _listRegisteredBindless;
        vector<uint32>                 _listBindlessFree;
        vector<BindlessResourceRecord> _listRegisteredUAV;
        vector<uint32>                 _listUavFree;

        RHIHandleTable<OpenGLTextureRecord>                         _gpuTextures;
        unordered_map<CompositeFboKey, uint32, CompositeFboKeyHash> _mapCompositeFbo;

        vector<BindlessTextureRecord> _listRegisteredTexture;
        vector<uint32>                _listTextureFree;

        /**
         * @brief GPU 타임스탬프입니다. 프레임 링만큼 쿼리 묶음을 돌려 씁니다.
         * @details 읽기는 그 묶음을 **다시 쓰기 직전**(= 링 한 바퀴 뒤)에 GL_QUERY_RESULT_AVAILABLE
         *          로 먼저 물어보고 준비된 칸만 풉니다. 준비 안 된 칸을 바로 읽으면 GL 이 거기서 막습니다.
         */
        uint32          _arrTimestampQuery[constant::kMaxGpuTimestampSlot * constant::kMaxFrameCountInFlight];
        uint32          _arrTimestampMask[constant::kMaxFrameCountInFlight];
        uint32          _timestampFrameIndex;
        uint8           _bTimestampEnabled; ///< 엔진이 켜기 전에는 쿼리도 만들지 않는다.
        uint8           _bTimestampReady;
        vector<float32> _listTimestampMicro;

        uint32 _computeRootConstantUbo;
        uint32 _arrComputeRootConstantShadow[kMaxComputeRootConstantDwords];

        RHIHandleTable<OpenGLPipelineStateRecord> _pipelineStates;
        vector<OpenGLRenderPassRecord>            _listRenderPass;

        RHIReleaseQueue _releaseQueue;

        sw::unique_ptr<OpenGLRHICommandContext> _frameStreamContext;
        sw::unique_ptr<OpenGLRHIResource>       _resourceImpl;

        int8                   _lastVsync; ///< -1 = 아직 안 정함, 0/1 = 마지막으로 적용한 값
        uint8                  _bInitialized  : 1;
        [[maybe_unused]] uint8 _reservedFlags : 7;
    };

    /**
     * @struct ScopedOpenGLContext
     * @brief 배타적 GL 컨텍스트 바인딩이 필요한 작업 동안 컨텍스트를 가져오고 놓는 RAII 가드입니다.
     * @details 컨텍스트가 **이미 이 스레드에 current 면 아무것도 하지 않습니다.** 예전에는 무조건 바인딩하고
     *          무조건 풀어서, 렌더 스레드(또는 디바이스를 초기화한 스레드)가 걸어 둔 바인딩을 첫
     *          createTexture2D 가 지워 버렸습니다. 그 뒤의 createConstantBuffer 는 가드 없이 glGenBuffers 를
     *          불러 0 을 받았습니다. 앱에서는 beginFrame 이 매 프레임 다시 바인딩해 가려졌고 RHITest 에서 드러났습니다.
     */
    struct ScopedOpenGLContext
    {
        OpenGLRHIDevice* _pDevice{ nullptr };
        bool             _bNeedsUnbind{ false };

        explicit ScopedOpenGLContext( OpenGLRHIDevice* pDevice )
            : _pDevice{ pDevice }
            , _bNeedsUnbind{ false }
        {
            if ( _pDevice != nullptr && _pDevice->requiresExclusiveContextThread() && _pDevice->isGraphicsContextCurrent() == false )
            {
                // **기다려서** 가져온다. 한 번 시도하고 포기하면 `_bNeedsUnbind` 만 false 가 되고 본문은
                // 그대로 실행돼서, 컨텍스트 없이 glGen* 이 나가 리소스가 조용히 만들어지지 않았다.
                // 렌더 워커가 프레임 끝마다 놓으므로 차례를 기다리는 편이 맞다.
                _bNeedsUnbind = _pDevice->acquireGraphicsContextBlocking();
            }
        }

        ~ScopedOpenGLContext()
        {
            if ( _bNeedsUnbind && _pDevice != nullptr )
                _pDevice->unbindGraphicsContext();
        }
    };
} // namespace sw
