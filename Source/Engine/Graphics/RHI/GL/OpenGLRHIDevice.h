/**
 * @file OpenGLRHIDevice.h
 * @brief OpenGL 4.6 Core Profile 기반 RHI 백엔드 클래스 정의
 * @note GLAD/OpenGL 심볼은 OpenGLRHIDevice.cpp 에서만 include합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/GL/Platform/IOpenGLPlatformContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIHandleTable.h"
#include "Engine/Graphics/RHI/Support/RHIReleaseQueue.h"

namespace sw
{
    class OpenGLRHICommandContext;
    class OpenGLRHIResource;

    /**
     * @struct OpenGLRecordingState
     * @brief "지금 GL 컨텍스트에 무엇이 걸려 있나" — 바인딩 캐시 한 자리.
     * @details **다른 백엔드와 달리 이것은 디바이스가 소유한다. 그게 맞다.** DX12 · Vulkan · DX11 은
     *          리스트마다 독립된 기록 스트림(커맨드 리스트 / Deferred Context)을 갖고 기록을 나중에
     *          재생하므로 캐시도 리스트마다 있어야 한다 — 전역이면 서로의 캐시를 덮는다(DX11 에서
     *          실제로 그 일이 일어났다). OpenGL 은 커맨드 버퍼가 없는 **스레드 종속 상태 머신**이고
     *          `OpenGLRHICommandList` 는 호출을 즉시 GL API 로 흘려보낸다. 실제 상태가 하나뿐이므로
     *          그것을 비추는 캐시도 하나여야 한다 — 리스트마다 두면 캐시가 진짜 GL 상태와 어긋난다.
     *          (그래서 GL 은 `_bParallelCommandRecording = 0` 이다.)
     */
    struct OpenGLRecordingState
    {
        RHIBufferHandle _boundMeshVb{ 0 };
        uint32          _boundMeshStride{ 0 };
        uint32          _boundMeshOffset{ 0 };
        RHIBufferHandle _boundInstanceSlotVb{ 0 }; ///< 슬롯 1 — 인스턴스 슬롯 스트림 (0 = 안 걸림)
        uint32          _boundInstanceSlotOffset{ 0 };
        RHIBufferHandle _boundIndexBuffer{ 0 };
        uint32          _boundIndexStride{ 4 };
        uint32          _boundIndexOffset{ 0 };

        RHIPipelineStateHandle _boundGraphicsPso{ 0 };
        RHIPipelineStateHandle _boundComputePso{ 0 }; ///< setComputePipelineState 가 마지막으로 건 컴퓨트 PSO — dispatchCompute 는 이 프로그램을 쓴다
        /// @brief 지금 텍스처가 걸려 있는 유닛 비트마스크 — 다음 패스가 쓰지 않는 유닛을 떼는 데 쓴다.
        uint32 _boundTextureUnitMask{ 0 };
    };

    /**
     * @class OpenGLRHIDevice
     * @brief OpenGL 4.6 그래픽스 및 컴퓨트 디바이스 구현체 (SSBO/UBO 바인딩 지원)
     */
    class OpenGLRHIDevice : public IRHIDevice
    {
        friend class OpenGLRHICommandContext;

    public:
        RHIBufferHandle createBuffer( const RHIBufferDesc& desc );
        RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride );
        friend class OpenGLRHIResource;
        /** @brief 빈 OpenGL 디바이스. */
        OpenGLRHIDevice();
        /** @brief 컨텍스트와 GL 객체를 정리합니다. */
        virtual ~OpenGLRHIDevice() override;

        /** @brief OpenGL 렌더링 컨텍스트 (wglCreateContext / EGL) 및 GLAD 로드 */
        bool initializeInternal( const RHISwapChainDesc& desc ) override;

        /** @brief OpenGL 컨텍스트 및 GL 객체 해제 */
        void shutdownInternal() override;

        /** @brief glViewport 크기 변경 */
        void resizeInternal( uint32 width, uint32 height ) override;

        /** @brief 프레임 시작 (glClearColor 및 glClear) */
        void beginFrame( const float4& clearColor ) override;

        /** @brief 프레임 종료 (SwapBuffers / wglSwapBuffers) */
        void endFrame( bool vsync, bool bPresent = true ) override;

        void   setTimestampEnabled( bool bEnabled ) override { _bTimestampEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        uint32 getTimestampSlotCount() const override;
        bool   readTimestampsMicros( vector<float32>& outListMicro ) override;
        /**
         * @brief 커맨드 리스트가 부르는 기록 지점 — GL 은 커맨드 버퍼가 없어 그 자리에서 발행한다.
         * @details 그래서 락이 없다. GL 호출은 컨텍스트를 쥔 한 스레드에서만 나간다.
         */
        void writeTimestampSlot( uint32 slotIndex );

        IRHIResource* getResource() override;
        /** @brief Present/offscreen/replay Immediate Context. */
        IRHICommandContext* getFrameStreamContext() override;
        /** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */

        /** @brief glFinish — GPU 대기 */
        void waitIdle() override;

        /** @brief 백엔드 타입 반환 (OpenGL) */
        RHIBackend getBackendType() const override { return RHIBackend::OpenGL; }

        /** @brief VS 가 SSBO(g_SwInstances)로 GPUScene 인스턴스 버퍼를 읽는다 (glBindBufferBase). */
        bool supportsInstancedSceneDraw() const override { return true; }

        /** @brief RHI 텍스처 핸들에 대응하는 GL texture name (없으면 0) */
        uint32 getGlTextureName( RHITextureHandle texture ) const;

        /** @brief 네이티브 GL 텍스처 이름을 반환합니다. */
        uint32 getNativeTextureName( RHITextureHandle texture ) const override { return getGlTextureName( texture ); }

        /** @brief 네이티브 GL 텍스처 핸들을 포인터 형태로 반환합니다. */
        void* getNativeTexturePointer( RHITextureHandle texture ) const override { return reinterpret_cast<void*>( static_cast<uintptr_t>( getGlTextureName( texture ) ) ); }

        /** @brief 백엔드 이름 문자열 반환 */
        const utf8* getBackendName() const override { return "OpenGL (glad 4.6 Core)"; }

        /** @brief Native DC / Display 포인터 반환 */
        void* getNativeDevice() const override { return _pHDC; }

        /** @brief Native HGLRC 컨텍스트 포인터 반환 */
        void* getNativeContext() const override { return _pHRC; }

        bool requiresExclusiveContextThread() const override { return true; }

        /** @brief 컨텍스트를 기다리다 포기하는 시간. 렌더 워커는 프레임 끝마다 놓으므로 한 프레임이면 넉넉하다. */
        static constexpr uint32 kContextAcquireTimeoutMs = 250;

        /**
         * @brief 컨텍스트를 이 스레드로 가져옵니다. 다른 스레드가 쥐고 있으면 놓을 때까지 기다립니다.
         * @details GL 컨텍스트는 한 스레드만 current 로 가질 수 있다. 렌더 워커는 프레임이 끝나면
         *          놓으므로(`RenderThread::executePacket`) 기다리면 한 프레임 안에 차례가 온다.
         *          그래서 `bindGraphicsContext` 처럼 한 번 시도하고 포기하는 대신 조용히 다시 집는다.
         * @param timeoutMs 포기까지 기다리는 시간(ms).
         * @return 가져왔으면 true. 제한 시간을 넘기면 false 이며 그때만 로그를 남긴다.
         */
        bool acquireGraphicsContextBlocking( uint32 timeoutMs = kContextAcquireTimeoutMs );
        bool bindGraphicsContext() override;
        /** @brief 이 스레드에 이미 우리 컨텍스트가 current 인지. ScopedOpenGLContext 가 남의 바인딩을 풀지 않게 하는 근거. */
        bool isGraphicsContextCurrent() const;
        /** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
        void unbindGraphicsContext() override;

        /** @brief OpenGL은 커맨드 큐가 없음 (nullptr) */
        void* getNativeCommandQueue() const override { return nullptr; }

        /** @brief glMultiDrawArraysIndirect when available. */

        /** @brief 커맨드 리스트 객체 생성 */
        unique_ptr<IRHICommandList> createCommandList() override;

        /** @brief 커맨드 리스트 실행 */
        void executeCommandList( IRHICommandList* pCmdList ) override;

    private:
        /** @brief 타임스탬프 쿼리 객체를 한 번만 만듭니다. */
        void ensureTimestampQueries();
        /** @brief 다시 쓰기 직전의 묶음에서 결과를 마이크로초로 풉니다 (기다리지 않습니다). */
        void collectTimestampsForSlot();

        /**
         * @brief 풀스크린 삼각형 VAO/VBO를 만듭니다.
         */

        /** @brief 컴퓨트 루트 상수 UBO를 확보합니다. */
        bool ensureComputeRootConstantUbo();
        /** @brief MRT 합성 FBO를 확보합니다. */
        uint32 ensureCompositeFboMrt( const RHITextureHandle* pColor, uint32 colorCount, RHITextureHandle depth );
        /** @brief 불투명 핸들을 GLuint 이름으로 풉니다. */
        uint32 resolveGlBuffer( RHIBufferHandle handle ) const;
        /** @brief GL 버퍼 이름을 테이블에 넣고 핸들을 반환합니다. */
        RHIBufferHandle storeGlBuffer( uint32 glName );
        /** @brief 불투명 텍스처 핸들을 OpenGLTextureRecord로 풉니다. */
        struct OpenGLTextureRecord;
        OpenGLTextureRecord*       resolveTexture( RHITextureHandle handle );
        const OpenGLTextureRecord* resolveTexture( RHITextureHandle handle ) const;

        /** @brief setComputeRootConstants 실제 용량(dword). RHITypes.h의
         *         constant::kMinComputeRootConstantDwords(=DX12 기준, 4개 백엔드 공통 안전값) 참고. */
        static constexpr uint32 kMaxComputeRootConstantDwords = 64;

        /// @brief 드로우 시 바인드할 버퍼/텍스처 슬롯
        struct BindlessResourceRecord
        {
            RHIBufferHandle  _buffer{ 0 };
            RHITextureHandle _texture{ 0 }; ///< UAV 레지스트리에서 RW 텍스처(이미지 유닛)면 0 이 아니다

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessResourceRecord& other ) const
            {
                return _buffer == other._buffer && _texture == other._texture;
            }
            bool operator!=( const BindlessResourceRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief GLuint 텍스처 + 타깃/포맷
        struct OpenGLTextureRecord
        {
            uint32    _texture{ 0 };
            uint32    _fbo{ 0 };
            uint32    _width{ 0 };
            uint32    _height{ 0 };
            uint32    _mipLevels{ 1 };
            RHIFormat _format = RHIFormat::R8G8B8A8_UNORM;
            uint32    _internalFormat{ 0 }; ///< GL 내부 포맷 — glBindImageTexture 가 쓴다
            uint8     _bDepthStencil : 1;
            uint8     _bUAV          : 1;
            uint8     _reserved      : 6;
        };

        /// @brief MRT FBO 캐시 키
        struct CompositeFboKey
        {
            RHITextureHandle _arrColor[kMaxColorAttachments]{};
            uint32           _colorCount{ 0 };
            RHITextureHandle _depth{ 0 };
            /** @brief 같으면 true를 반환합니다. */
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

        /// @brief CompositeFboKey 해시
        struct CompositeFboKeyHash
        {
            /** @brief 호출 연산자입니다. */
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

        /// @brief 인덱스 → GL 텍스처 유닛 매핑
        struct BindlessTextureRecord
        {
            RHITextureHandle _texture{ 0 };

            /** @brief `releaseFreeListIndex` 가 "이 슬롯이 비었는가" 를 묻는 데 씁니다. */
            bool operator==( const BindlessTextureRecord& other ) const { return _texture == other._texture; }
            bool operator!=( const BindlessTextureRecord& other ) const { return ( *this == other ) == false; }
        };

        /// @brief 프로그램 + 래스터/블렌드/깊이 상태
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

        /// @brief 렌더 패스 서술 캐시
        struct OpenGLRenderPassRecord
        {
            RHIRenderPassDesc _desc{};
            uint8             _bAlive   : 1;
            uint8             _reserved : 7;
        };

        /** @brief 플랫폼 GL 컨텍스트(WGL/GLX/NSGL). 생성·바인딩·프레젠트·VSync 를 전부 여기가 안다. */
        unique_ptr<IOpenGLPlatformContext> _platformContext;

        /** @brief getNativeDevice 계약용 복사본 — 실제 소유는 _platformContext 다. */
        void*  _pHDC;
        void*  _pHRC;
        void*  _pHWnd;
        uint32 _width;
        uint32 _height;
        uint32 _shaderProgram;
        uint32 _vao;
        uint32 _vbo;     ///< Fullscreen stub (3 verts)
        uint32 _meshVao; ///< VAO for scene mesh draw()
        uint32 _defaultSampler;
        uint32 _defaultTexture;

        RHIHandleTable<uint32> _gpuBuffers;
        /// @brief 즉시 컨텍스트가 쓰는 기록 상태. 리스트는 각자 자기 것을 갖는다.
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
         * @brief GPU 타임스탬프 — 프레임 링만큼 쿼리 묶음을 돌려 쓴다.
         * @details 읽기는 그 묶음을 **다시 쓰기 직전**(= 링 한 바퀴 뒤)에 GL_QUERY_RESULT_AVAILABLE
         *          로 먼저 물어보고 준비된 칸만 푼다 — 준비 안 된 칸을 바로 읽으면 GL 이 거기서 막는다.
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

        int8                   _lastVsync; ///< -1 unset, 0/1 last applied
        uint8                  _bInitialized  : 1;
        [[maybe_unused]] uint8 _reservedFlags : 7;
        /// @brief bindShaderResource가 실제로 바인딩한 텍스처 유닛 비트마스크(비트 i = 유닛 i).
        /// beginRenderPass가 패스 시작마다 방어적으로 0..15 유닛을 전부 언바인드하던 것을, 실제로
        /// 바인딩된 유닛만 언바인드하도록 줄이는 데 쓴다.
    };

    /**
     * @struct ScopedOpenGLContext
     * @brief OpenGL 배타적 그래픽스 컨텍스트 바인딩이 필요한 작업 시 컨텍스트를 획득하고 해제하는 RAII 가드
     * @details 컨텍스트가 **이미 이 스레드에 current 면 아무것도 하지 않는다.** 예전엔 무조건 바인딩하고
     *          무조건 풀어서, 렌더 스레드(또는 디바이스를 초기화한 스레드)가 걸어 둔 바인딩을 첫
     *          createTexture2D 가 지워 버렸다 — 그 뒤의 createConstantBuffer 는 가드 없이 glGenBuffers 를
     *          불러 0 을 받았다. 앱에서는 beginFrame 이 매 프레임 다시 바인딩해 가려졌고 RHITest 에서 드러났다.
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
