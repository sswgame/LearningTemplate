/**
 * @file IRHIDevice.h
 * @brief RHI 디바이스 추상화입니다. 프레임 수명주기, 커맨드 리스트, 능력 조회, 네이티브 핸들을 담당합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"

namespace sw
{

    struct RHIDeferredHandleQueue;
    struct RHIMemoryBudget;

    class IRenderSurface;
    class IRHICommandContext;
    class IRHICommandList;
    class IRHIContextThreadBinding;
    class IRHIResourceFactory;
    class IRHITimestampProvider;
    class RHIMemoryLedger;

    /** @brief `IRHIDevice::releaseHandle` 이 해제하는 핸들의 종류입니다. 종류마다 해제에 쓰는 팩터리 함수가 다릅니다. */
    enum class RHIHandleKind : uint8
    {
        BindlessResource = 0, ///< `registerBindlessBufferSrv` 가 돌려준 인덱스이며 `unregisterBindlessResource` 로 해제한다.
        BindlessTexture,      ///< `registerBindlessTexture` 가 돌려준 인덱스이며 `unregisterBindlessTexture` 로 해제한다.
        Buffer,               ///< `createBuffer` 가 돌려준 핸들이며 `destroyBuffer` 로 해제한다.
        Texture               ///< `createTexture` 가 돌려준 핸들이며 `destroyTexture` 로 해제한다.
    };

    /** @brief bindless 인덱스가 속한 공간입니다. DX12 는 둘이 힙 하나를 나눠 쓰고, 나머지 백엔드는 공간마다 번호가 따로입니다. */
    enum class RHIBindlessIndexSpace : uint8
    {
        Texture = 0, ///< `registerBindlessTexture` 가 준 텍스처 SRV 인덱스
        Buffer       ///< `registerBindlessBufferSrv` 가 준 버퍼(구조 버퍼 SRV · 상수 버퍼) 인덱스
    };

    /** @brief `RHINativeHandles` 의 버전 번호입니다. 필드, 순서, 의미가 바뀌면 올립니다. */
    inline constexpr uint32 kRHINativeHandlesVersion = 2;

    /**
     * @struct RHINativeHandles
     * @brief 백엔드 위에서 동작하는 외부 라이브러리(에디터의 ImGui 렌더러 백엔드)에 넘길 네이티브 핸들 묶음입니다.
     * @details 모듈 경계(EditorModule, Engine, RHI_*)를 넘는 것은 이 POD 하나뿐이다. 구체 디바이스 클래스의 레이아웃이나 vtable 은 넘지 않는다.
     *          호출하는 쪽은 자기가 컴파일된 버전 번호(`_version`)와 크기(`_byteSize`)를 기본값 그대로 실어 보낸다.
     *          `IRHIDevice::queryNativeHandles` 는 이 값이 Engine 의 것과 다르면 채우지 않고 거절한다. 모두 `void*` 라 백엔드 헤더가 필요 없다.
     *          그 백엔드에 없는 항목은 nullptr 또는 0 이다.
     */
    struct RHINativeHandles
    {
        uint32     _version{ kRHINativeHandlesVersion };    ///< 호출하는 쪽이 컴파일된 버전 번호
        uint32     _byteSize{ sizeof( RHINativeHandles ) }; ///< 호출하는 쪽이 아는 구조체 크기
        void*      _pInstance{ nullptr };                   ///< VkInstance(Vulkan)
        void*      _pPhysicalDevice{ nullptr };             ///< VkPhysicalDevice(Vulkan)
        void*      _pDevice{ nullptr };                     ///< `getNativeDevice()` 와 같은 값(VkDevice, ID3D12Device, ID3D11Device, GL 표시 장치)
        void*      _pContext{ nullptr };                    ///< `getNativeContext()` 와 같은 값
        void*      _pGraphicsQueue{ nullptr };              ///< `getNativeCommandQueue()` 와 같은 값(VkQueue, ID3D12CommandQueue)
        void*      _pRenderPass{ nullptr };                 ///< 백버퍼 렌더 패스(VkRenderPass, Vulkan)
        void*      _pQueueMutex{ nullptr };                 ///< 그래픽스 큐 제출 잠금(`sw::mutex*`, Vulkan). 큐는 외부 동기화 대상이라 외부 라이브러리가 이 큐에 제출하거나 대기할 때 잡아야 한다.
        uint32     _queueFamily{ 0 };                       ///< 그래픽스 큐 패밀리 인덱스(Vulkan)
        uint32     _minImageCount{ 0 };                     ///< 스왑체인 최소 이미지 수(Vulkan)
        uint32     _imageCount{ 0 };                        ///< 스왑체인 이미지 수(Vulkan)
        RHIBackend _backend{ RHIBackend::DirectX12 };       ///< 핸들을 채운 디바이스의 백엔드
    };
} // namespace sw

namespace sw
{
    /**
     * @class IRHIDevice
     * @brief DX11, DX12, Vulkan, OpenGL 하드웨어 디바이스의 추상화입니다.
     * @details 프레임 수명주기(beginFrame/endFrame/resize), 커맨드 리스트 생성과 제출, 능력 조회, 네이티브 핸들을
     *          담당한다. 리소스 생성과 파괴는 `getResourceFactory()` 가 반환하는 `IRHIResourceFactory` 가 담당한다.
     *          기록 스트림은 두 가지다. 하나는 디바이스가 소유한 프레임 스트림(`getFrameStreamContext`)이고,
     *          다른 하나는 패스마다 만드는 커맨드 리스트(`createCommandList`)다.
     */
    class SW_API IRHIDevice
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명: initialize/shutdown, resize
        // ------------------------------------------------------------------------------
        /** @brief 가상 소멸자입니다. */
        virtual ~IRHIDevice();
        /** @brief 빈 디바이스를 만듭니다. initialize 전에 setRenderSurface 를 불러야 합니다. */
        IRHIDevice();
        /** @brief 복사를 금지합니다. */
        IRHIDevice( const IRHIDevice& ) = delete;
        /** @brief 대입을 금지합니다. */
        IRHIDevice& operator=( const IRHIDevice& ) = delete;

        /** @brief 표면(`setRenderSurface`)에서 스왑체인 서술(크기, 백버퍼 3개, 포맷, VSync)을 채우고 `initializeInternal` 을 부릅니다. */
        virtual bool initialize();
        /**
         * @brief 마지막 `initialize` 의 결과입니다. 실패한 경우 원인이 환경(드라이버가 기능을 제공하지 않음)인지, 결함일 수 있는지를 구분합니다.
         * @details 백엔드는 환경 때문에 물러날 때만 `initializeInternal` 안에서 `_initResult` 를 `DriverUnsupported` 로 기록하고 false 를 반환한다.
         */
        RHIInitResult getInitResult() const { return _initResult; }
        /** @brief 실제로 소프트웨어 어댑터(WARP, CPU Vulkan)를 쓰는지 반환합니다(`gv_rhiSoftwareAdapter`, 환경 변수 `SW_RHI_SOFTWARE_ADAPTER`). */
        bool isRunningOnSoftwareAdapter() const { return _bSoftwareAdapter; }
        /**
         * @brief 디바이스를 종료합니다. 단계 순서는 이 함수 하나가 정하고, 백엔드는 단계별 훅만 채웁니다.
         * @details 1) 자원을 가진 쪽에 알린다(`RHIRenderResource::releaseAllFor`). 디바이스가 살아 있으므로 자원을 돌려줄 수 있다.
         *          2) GPU 를 기다리고 해제 큐를 비운다(`waitIdleInternal`). 1단계에서 넘긴 자원이 여기서 실제로 사라진다.
         *          3) 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 디바이스에서 분리한다(`detachCommandRecordingInternal`).
         *          4) 백엔드 자원, 스왑체인, 네이티브 디바이스를 해제한다(`shutdownInternal`).
         *
         *          렌더 스레드는 이미 멈춰 있어야 한다. 이 함수는 렌더 스레드를 비우지 않는다(`waitIdle` 과 다른 점).
         *          2 · 4 단계 뒤에 해제 큐가 비었는지 단언한다(`getPendingReleaseCount`). 4 단계에서 넣은 해제나, 끝난 종료 뒤에 넣은 해제는
         *          네이티브 디바이스가 사라진 뒤(백엔드 소멸자 · 다음 `shutdown`)에 불리기 때문이다. 두 번째 `shutdown`(소멸자)은 그 사이에 쌓인 것을 먼저 단언한다.
         */
        void shutdown();

        /** @brief RHI 디바이스와 스왑체인을 초기화합니다. */
        virtual bool initializeInternal( const RHISwapChainDesc& desc ) = 0;

        /** @brief 종료 4단계입니다. 백엔드 자원, 스왑체인, 네이티브 디바이스를 해제합니다. GPU 대기와 커맨드 리스트 분리는 이미 끝나 있습니다. */
        virtual void shutdownInternal() = 0;

        /** @brief 백버퍼(스왑체인)를 새 크기로 다시 만듭니다. `resize` 가 크기를 기록한 뒤 부릅니다. */
        virtual void resizeInternal( uint32 width, uint32 height ) = 0;

        /**
         * @brief `setVSync` 가 값을 기록한 뒤 부릅니다. 동기화를 프레젠트 인자로 고르는 백엔드(DXGI, GL)는 할 일이 없습니다.
         * @details 스왑체인의 present 모드가 동기화를 정하는 백엔드(Vulkan)는 여기서 스왑체인을 다시 만들도록 표시합니다.
         */
        virtual void applyVSyncInternal() {}

        /**
         * @brief GPU 에 제출된 작업이 모두 끝날 때까지 기다립니다.
         * @details **렌더 스레드가 실행 중일 때 다른 스레드에서 부르면, 먼저 렌더 스레드가 받은 작업을 모두 끝낼 때까지 기다린다**
         *          (`setRenderThreadDrain`). 게임 스레드에서 부르는 쪽(비동기 씬 로드 완료인 `SceneManager::updateTransitions`, 텍스처와 머티리얼
         *          핫 리로드)도 이 순서를 따른다. 실제 대기는 백엔드의 `waitIdleInternal` 이 한다.
         * @warning 렌더 스레드가 프레임을 기록하거나 제출하는 도중에 펜스 Signal 과 해제 큐 비우기가 끼어들면, DX12 는 아직 실행 전인 커맨드
         *          얼로케이터를 재사용해 장치가 제거되고, Vulkan 은 큐 외부 동기화를 어긴다. 에디터 빌드는 UI 가 렌더 스레드를 먼저 비워
         *          이 문제를 가리므로, 이 경로는 에디터가 없는 빌드(Shipping)로 확인한다.
         */
        void waitIdle();

        /**
         * @brief 지금 기록 중인 프레임의 GPU 작업이 끝난 뒤에 @p releaseDelegate 를 부릅니다. 프레임 밖이면 다음에 제출하는 프레임을 기준으로 합니다.
         * @details 백엔드 해제 큐(`RHIReleaseQueue`)에 그 백엔드의 기준으로 넣는다. DX12 와 Vulkan 은 GPU 펜스를, DX11 과 GL 은 프레임 지연을 쓴다.
         *          엔진 바깥 모듈(에디터의 ImGui 렌더러)이 자기 네이티브 자원(디스크립터)을 엔진 자원과 같은 기준으로 해제할 때 쓰는 함수다.
         *          **프레임을 기록하는 스레드(렌더 스레드)에서 불러야 한다.** 펜스 값을 읽는 스레드가 그 값을 올리는 스레드여야 "이 프레임" 이 맞는다.
         *          콜백은 백엔드가 완료를 확인하는 스레드에서 불리고, `waitIdle` 과 `shutdown` 이 남은 콜백을 모두 부른다.
         */
        virtual void enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate ) = 0;
        /** @brief 백엔드 해제 큐에서 아직 불리지 않은 해제 수입니다. `waitIdle` 과 `shutdown` 뒤에는 0 이어야 합니다(`shutdown` 이 단언한다). */
        virtual uint32 getPendingReleaseCount() const = 0;
        /**
         * @brief 장치 제거(TDR)를 흉내 냅니다(개발 명령 `rhi.removeDevice`). 할 수 있는 백엔드만 true 를 반환합니다(DX12 — `ID3D12Device5::RemoveDevice`, Vulkan — 제거 API 가 없어 `VK_ERROR_DEVICE_LOST` 를 처음 본 뒤의 상태로 둔다).
         * @details 실제 TDR 은 되풀이하면 어댑터가 망가지므로, 제거 뒤의 대기 · 종료 경로가 멈추지 않는지는 이것으로 확인한다. 어느 스레드에서 불러도 된다.
         */
        virtual bool debugRemoveDevice() { return false; }
        /**
         * @brief 검증 레이어가 이 디바이스에 알린 경고 · 오류 수입니다. Debug 의 DX12 만 셉니다(D3D12 디버그 레이어, 메시지가 난 자리에서 로그로도 남긴다). 다른 백엔드와 배포본은 0 입니다.
         * @details 디버그 레이어의 메시지 큐는 장치 실패 때만 비우므로 평소 로그에 보이지 않는다. 테스트가 "이 그리기는 검증 경고를 내지 않는다" 를 단언할 때 쓴다.
         */
        virtual uint32 getValidationMessageCount() const { return 0; }

        /** @brief 렌더 스레드가 받은 작업을 모두 끝낼 때까지 기다리는 함수의 타입입니다. 호출하는 쪽은 렌더 스레드가 아닙니다. */
        using RenderThreadDrainFunction = void ( * )( void* pContext );

        /**
         * @brief 렌더 스레드가 이 디바이스로 기록을 시작할 때 설정하고(`RenderThread::start`), 멈춘 뒤 해제합니다(nullptr).
         * @param pfnDrain         다른 스레드의 `waitIdle` 이 먼저 부를 함수입니다. nullptr 이면 해제합니다.
         * @param pContext         그 함수에 넘길 값(렌더 스레드 객체)입니다.
         * @param renderThreadID   렌더 스레드입니다. 이 스레드에서 부른 `waitIdle` 은 자기 자신을 기다리지 않습니다.
         */
        void setRenderThreadDrain( RenderThreadDrainFunction pfnDrain, void* pContext, std::thread::id renderThreadID );

        /**
         * @brief 자원 핸들 하나를 해제합니다. 렌더 스레드가 떠 있을 때 다른 스레드가 부르면, **다음에 넘길 프레임**이 끝날 때(병렬 기록 밖)까지 미룹니다.
         * @details 게임 스레드는 GPU 자원을 가진 객체(`Material`, `MaterialInstance`, `Texture2D`)의 마지막 소유를 아무 때나 놓을 수 있다.
         *          GPUScene 후보를 덮는 수집 잡, 핫 리로드로 정리한 뷰, 로드 중 텍스처를 진짜로 바꾸는 머티리얼이 그 예다. 그 순간 렌더 스레드가 기록 중이면 bindless 테이블을
         *          바꾸면 안 되고(`setParallelRecording`), 게임 스레드가 이미 지었지만 아직 넘기지 않은 패킷도 옛 인덱스를 값으로 들고 있다(배치의 머티리얼 텍스처 SRV).
         *          그래서 객체의 소멸은 그 자리에서, 핸들의 반환은 해제 시점의 "넘긴 수 + 1" 번째 프레임이 끝난 뒤에 한다 — 렌더 스레드가 쉬고 있어도 미룬다.
         *          언리얼의 `FDeferredCleanupInterface`, `BeginReleaseResource` 에 해당한다. 렌더 스레드 자신이 부르거나, 렌더 스레드가 없고 넘긴 프레임도 없으면 바로 해제한다.
         *          주의: 게임 스레드가 프레임을 넘기지 않는 동안에는 미룬 핸들이 쌓인다 — `waitIdle` · 렌더 스레드 해제 · `shutdown` 이 모두 내린다.
         */
        void releaseHandle( RHIHandleKind kind, uint64 handle );
        /** @brief 미뤄 둔 핸들을 기한과 무관하게 모두 해제합니다. `waitIdle` 에서, 렌더 스레드를 세우거나 해제할 때, `shutdown` 에서 부릅니다. */
        void flushDeferredHandleReleases();
        /** @brief 렌더 스레드에 프레임 하나를 넘겼음을 알립니다(`RenderThread::submit`). */
        void notifyRenderFrameQueued();
        /** @brief 렌더 스레드가 프레임 하나를 끝냈음을 알리고, 그 프레임까지 기다리던 핸들을 해제합니다. 렌더 스레드(기록 밖)에서 부릅니다. */
        void notifyRenderFrameRetired();
        /** @brief 아직 해제하지 않고 미뤄 둔 핸들 수입니다(진단과 테스트용). */
        size_t getDeferredHandleCount() const;

        // ------------------------------------------------------------------------------
        // 2) 능력과 스레드: 백엔드 종류, bindless, 컨텍스트 소유
        // ------------------------------------------------------------------------------
        /**
         * @brief 이 디바이스가 실제로 채택한 백버퍼 컬러 포맷입니다.
         * @details 기본은 계약값 `constant::kBackBufferFormat` 이다. 하드웨어나 서피스 제약 때문에 그 값을
         *          낼 수 없는 백엔드(Vulkan)만 override 해서 실제 값을 보고한다. 조용히 다른 포맷을
         *          쓰면 백버퍼를 타깃으로 하는 PSO 가 모두 렌더 패스 비호환이 된다.
         */
        virtual RHIFormat getBackBufferFormat() const { return constant::kBackBufferFormat; }

        /** @brief 백엔드 능력을 조회합니다.
         * @note 정적 테이블은 RHIAvailability::query 입니다. DX12 와 Vulkan 의 네이티브 bindless 는
         *       supportsNativeBindlessSampling() 과 getCapabilities() override 가 런타임에 확정합니다.
         */
        virtual RHICapabilities getCapabilities() const { return RHIAvailability::query( getBackendType() ); }
        /**
         * @brief 프레임 기록을 엽니다. 프레임당 정확히 한 번, 그 프레임의 어떤 기록보다 먼저 부릅니다.
         * @details **수명주기 전용**이다. 펜스 대기(GPU 백프레셔), 스왑체인 이미지 획득, 기록 시작만 하고 렌더 타깃은 바인딩하지 않는다.
         *          백버퍼 바인딩과 클리어는 `IRHICommandList::beginRenderPass`(타깃 핸들 0)가 명시적으로 한다(`RenderThread::executeFrameBody`).
         *          프레임 수명주기(beginFrame, endFrame, resize)는 디바이스의 일이다. 스왑체인 쪽에 두면 디바이스의 private 상태를 건드려야 한다.
         * @note 스왑체인 자체(백버퍼, 이미지 인덱스, 동기화, present)는 백엔드 안의 구체 클래스
         *       `<백엔드>RHISwapChain` 이 소유합니다. 가상 인터페이스로 되돌리지 않습니다(RHI/README.md). GL 에는 없습니다.
         */
        virtual void beginFrame() = 0;
        /** @brief 기록을 닫고 큐에 제출합니다. bPresent=false 면 제출만 하고 Present 는 하지 않습니다. */
        virtual void endFrame( bool vsync = true, bool bPresent = true ) = 0;

        /**
         * @brief GPU 타임스탬프 계측 능력입니다. 이 능력이 없는 디바이스(테스트 목)는 nullptr 입니다.
         * @details 선택 능력이라 `IRHIDevice` 에 빈 기본 구현으로 두지 않고 따로 뽑았다(`IRHITimestampProvider`). 가상 호출이 아니라 멤버 하나를
         *          읽으므로 패스마다 물어도 된다. 백엔드를 바꾸면 디바이스가 바뀌므로 포인터를 디바이스 밖에 오래 들고 있지 않는다.
         */
        IRHITimestampProvider* findTimestampProvider() const { return _pTimestampProvider; }
        /**
         * @brief 백버퍼 크기를 바꿉니다. 채택된 크기는 `getBackBufferWidth/Height` 가 알려 줍니다.
         * @details 크기를 여기에 기록하는 이유는 렌더러가 첨부 크기를 정할 때 **창이 아니라 디바이스에 묻게**
         *          하려는 것이다. 렌더러가 보는 것은 OS 창이 아니라 스왑체인이다(언리얼 `FRHIViewport` 에 해당한다).
         *          그래서 Graphics 는 Window 를 include 하지 않는다.
         */
        void resize( uint32 width, uint32 height );
        /** @brief 백버퍼 너비입니다. initialize 때는 표면 크기이고, 그 뒤에는 마지막 `resize` 값입니다. */
        uint32 getBackBufferWidth() const { return _backBufferWidth; }
        /** @brief 백버퍼 높이입니다. initialize 때는 표면 크기이고, 그 뒤에는 마지막 `resize` 값입니다. */
        uint32 getBackBufferHeight() const { return _backBufferHeight; }
        /** @brief 리소스를 만들고 파괴하는 팩터리입니다. 팩터리가 없는 디바이스(테스트 목)는 nullptr 를 돌려줍니다. */
        virtual IRHIResourceFactory* getResourceFactory() = 0;

        /**
         * @brief 디바이스가 소유한 **프레임 스트림**에 기록하는 컨텍스트입니다.
         * @details beginFrame/endFrame 이 여는 디바이스 커맨드 리스트(버퍼)에 그대로 기록한다.
         *          RenderThread 가 백버퍼 렌더 패스를 여는 경로가 이것이다. 패스별 기록은 자기
         *          네이티브 버퍼를 소유하는 IRHICommandList 가 따로 한다. 프레임 스트림은 디바이스마다 하나다.
         */
        virtual IRHICommandContext* getFrameStreamContext() = 0;

        /** @brief 현재 RHI 백엔드 종류를 반환합니다. */
        virtual RHIBackend getBackendType() const = 0;

        /**
         * @brief 셰이더가 디스크립터 인덱스로 텍스처를 샘플링할 수 있는지 반환합니다(DX12 텍스처 배열 테이블, Vulkan 디스크립터 인덱싱).
         * @note false 면 CPU 가 그 인덱스로 슬롯을 바인딩해야 합니다(DX11, GL 에뮬레이션).
         */
        virtual bool supportsNativeBindlessSampling() const { return false; }

#if !defined( SW_SHIPPING )
        /**
         * @brief @p index 가 @p space 에서 지금 살아 있는 리소스를 가리키면 true 입니다(Debug · Dev 진단 전용, Shipping 에는 없습니다).
         * @details 해제했거나(해제 대기 포함) 발급한 적 없는 번호, 다른 종류(텍스처 자리에 버퍼)를 가리키는 번호는 false 다.
         *          셰이더에 그 번호를 넘기면 DX12 · Vulkan 은 무효 디스크립터를 읽어 디바이스를 잃을 수 있다. 정책(언제 묻고 무엇을 남기나)은
         *          Engine 의 `BindlessIndexValidator` 가, 생존 판정(등록 표 읽기)은 백엔드가 한다. `kInvalidDescriptorIndex` 는 묻지 않는다.
         *          병렬 기록 스레드에서 불러도 된다(등록 표를 읽기 잠금으로 읽는다). 등록 표가 없는 디바이스(테스트 목)는 늘 true 다.
         */
        virtual bool isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
        {
            (void)space;
            (void)index;
            return true;
        }
#endif

        /**
         * @brief 그래픽스 VS 가 GPUScene 인스턴스 구조 버퍼(InstanceData)를 읽을 수 있으면 true 를 반환합니다.
         * @details true 면 FrameRenderer 가 인스턴스 버퍼를 바인딩하고 배치를 drawIndirect(멀티 드로우)로 그리며, 인스턴스마다의
         *          월드 행렬과 materialIndex 는 구조 버퍼에서 읽는다. false 면 씬 메시를 그릴 수 없다. 드로우마다 g_World 를 쓰는 폴백 경로는 없다.
         */
        virtual bool supportsInstancedSceneDraw() const = 0;

        /**
         * @brief 컨텍스트 스레드 바인딩 능력입니다. 컨텍스트가 스레드에 묶이지 않는 디바이스(DX12, Vulkan, 테스트 목)는 nullptr 입니다.
         * @details 선택 능력이라 따로 뽑았다(`IRHIContextThreadBinding`). 가상 호출이 아니라 멤버 하나를 읽는다.
         */
        IRHIContextThreadBinding* findContextThreadBinding() const { return _pContextThreadBinding; }

        /**
         * @brief 드로우와 Present 컨텍스트를 소유 스레드 하나에서만 써야 하면 true 를 반환합니다.
         * @details DX11 즉시 컨텍스트와 OpenGL(wgl/egl MakeCurrent)이 그렇습니다. DX12 와 Vulkan 은 false 입니다.
         *          컨텍스트 스레드 바인딩 능력(`findContextThreadBinding`)이 있는지와 같은 질문입니다.
         */
        bool requiresExclusiveContextThread() const { return _pContextThreadBinding != nullptr; }

        /** @brief 백엔드 이름 문자열을 반환합니다. */
        virtual const utf8* getBackendName() const = 0;

        // ------------------------------------------------------------------------------
        // 3) 네이티브 핸들: 디바이스, 컨텍스트, 스왑체인, 큐
        // ------------------------------------------------------------------------------
        /** @brief 네이티브 디바이스 포인터(ID3D12Device, VkDevice 등)를 반환합니다. */
        virtual void* getNativeDevice() const = 0;

        /** @brief 네이티브 컨텍스트 포인터(ID3D11DeviceContext, EGLContext 등)를 반환합니다. */
        virtual void* getNativeContext() const = 0;

        /** @brief 네이티브 커맨드 큐 포인터를 반환합니다. */
        virtual void* getNativeCommandQueue() const = 0;

        /**
         * @brief 외부 라이브러리를 초기화할 때 쓸 네이티브 핸들 묶음을 채웁니다.
         * @details 비가상 함수다. 버전 대조는 Engine 이 이 한 곳에서 하고, 백엔드는 `queryNativeHandlesInternal` 만 채운다.
         *          `inoutHandles` 의 `_version` 과 `_byteSize` 가 이 엔진의 `kRHINativeHandlesVersion`, `sizeof( RHINativeHandles )` 와 다르면
         *          (따로 빌드한 모듈이 다른 버전의 구조체를 보낸 경우) 아무것도 쓰지 않고 false 를 반환한다. 백엔드가 디바이스를 내놓지 못해도 false 다.
         */
        [[nodiscard]] bool queryNativeHandles( RHINativeHandles& inoutHandles ) const;

        /**
         * @brief 네이티브 텍스처를 포인터 형태로 반환합니다. 없는 핸들이면 nullptr 입니다.
         * @details D3D12 는 `ID3D12Resource*`, D3D11 은 `ID3D11Texture2D*`, Vulkan 은 이미지 뷰, OpenGL 은 텍스처 이름(GLuint)을 포인터 폭으로 넓힌 값이다.
         * @note 에디터 MODULE 이 RHI_* 디바이스 MODULE 의 구체 타입에 링크하지 않도록 가상 함수로 둡니다.
         */
        virtual void* getNativeTexturePointer( RHITextureHandle texture ) const = 0;

        /** @brief 스왑체인을 만들 표면을 저장합니다. `initialize` 가 핸들과 크기를 여기서 읽습니다. */
        void setRenderSurface( IRenderSurface* pSurface ) { _pSurface = pSurface; }
        /** @brief CLI 에 -vsync 가 없을 때 쓸 스왑체인 VSync 를 정합니다. `initialize` 전에 부릅니다. */
        void setPreferredVSync( bool bVSync ) { _bPreferredVSync = bVSync; }
        /**
         * @brief 실제로 채택된 VSync 값입니다. 설정값을 먼저 적용하고, CLI `-vsync` 가 있으면 그 값으로 덮어씁니다.
         * @details **프레젠트 경로가 읽어야 하는 값이 이것이다.** 주의: `endFrame` 의 vsync 인자를 상수로 박으면 설정도
         *          CLI 도 효과가 없어 프레임이 모니터 주사율에 묶인다.
         */
        bool isVSyncEnabled() const { return _bPreferredVSync; }
        /**
         * @brief 실행 중에 VSync 를 바꿉니다. **렌더 스레드가 멈춘 상태에서만** 부릅니다(`RenderThread::waitIdle` 뒤이며 리사이즈와 같은 규칙입니다).
         * @details 다음 `endFrame` 부터 새 값으로 프레젠트한다. DXGI 스왑체인은 티어링을 지원하면 항상 티어링을 허용하도록 만들어 두므로 다시 만들지
         *          않는다. Vulkan 은 present 모드가 바뀌므로 다음 `beginFrame` 에서 스왑체인을 다시 만든다.
         */
        void setVSync( bool bVSync );
        // ------------------------------------------------------------------------------
        // 4) 커맨드 리스트: 생성, 그래픽스 스레드에서만 execute
        // ------------------------------------------------------------------------------

        /** @brief 독립 커맨드 리스트를 만듭니다. 기록 전용이며, GPU 에 넘기는 것은 executeCommandList 입니다. */
        virtual unique_ptr<IRHICommandList> createCommandList() = 0;

        /**
         * @brief 커맨드 리스트를 **프레임 스트림 순서에 맞춰** 제출 대기열에 넣습니다.
         * @details beginFrame 과 endFrame 사이에서 쓴다. 디바이스는 이 지점에서 프레임 스트림을 잘라
         *          [지금까지의 세그먼트][이 리스트][새 세그먼트] 순서로 연결하고 endFrame 에서 한 번에
         *          큐로 넘긴다. 같은 큐의 제출 순서가 곧 실행 순서다. 그래픽스 실행 스레드 전용이다.
         */
        virtual void executeCommandList( IRHICommandList* pCmdList ) = 0;

        /**
         * @brief 프레임 스트림과 무관하게 **곧바로** 큐에 제출합니다.
         * @details 프레임 밖의 일회성 작업(오프스크린 스모크, 리소스 업로드, 썸네일 렌더 등)에 쓴다.
         *          프레임 순서 보장이 필요 없고 beginFrame 이 열려 있지 않을 때만 써야 한다.
         *          프레임 중에 부르면 스트림 순서를 건너뛰므로 렌더 결과가 어긋난다.
         *          기본 구현은 executeCommandList 로 넘긴다(DX11/GL 처럼 기록이 곧 실행인 백엔드).
         */
        virtual void executeCommandListImmediate( IRHICommandList* pCmdList ) { executeCommandList( pCmdList ); }

        /**
         * @brief 커맨드 리스트가 제출될 때마다 프레임 스트림을 **곧바로** 큐로 내보낼지 정합니다.
         * @details 기본값(false)은 프레임 끝에 한 번에 제출한다. 켜면 executeCommandList 마다 큐 제출이
         *          한 번씩 일어나 오버헤드가 크지만, 두 모드 모두 [세그먼트][리스트] 순서를 지키므로
         *          실행 순서는 같다. 달라지는 것은 제출 '시점'뿐이다. GPU 오류(DEVICE_HUNG, 검증 레이어)가
         *          어느 제출에서 났는지 좁힐 때 쓴다.
         * @note 백엔드마다 "즉시" 가 가리키는 것이 다르다. DX12/Vulkan 은 모아 둔 커맨드 리스트를 그
         *       자리에서 큐로 제출한다. DX11/GL 은 기록 스트림이 곧 제출 스트림이라 순서는
         *       이미 맞으므로 `Flush`/`glFlush` 로 GPU 에 밀어내기만 한다. 어느 쪽이든 효과는 같다.
         *       리스트 경계에서 GPU 작업이 끊긴다.
         * @note 값의 출처는 Engine 의 `gv_rhiImmediateSubmit` 이고, RenderThread 가 프레임마다 전달한다.
         *       백엔드는 MODULE DLL 로 따로 빌드되므로 전역 변수를 그쪽까지 export 하지 않는다.
         *       정책은 Engine 이 정하고 디바이스는 메커니즘만 가진다.
         */
        void setImmediateSubmit( bool bEnable ) { _bImmediateSubmit = bEnable; }

        /**
         * @brief 지금이 여러 스레드가 동시에 패스를 기록하는 구간인지 알립니다.
         * @details **이 구간에서는 bindless 레지스트리를 바꿀 수 없다.** 레지스트리는 기록 중에
         *          드로우마다 읽히는데, 그 사이에 register/unregister 가 resize 를 일으키면 읽는
         *          쪽이 잡아 둔 참조가 dangling 이 되고 GPU 가 쓰레기 디스크립터를 읽는다.
         *
         *          그래서 등록은 모두 기록 **밖**(그래프 셋업, 리소스 로드)에서 끝낸다. 그러면
         *          기록 중 레지스트리는 불변이라 읽기에 락이 필요 없다. 락을 잘 거는 대신 애초에
         *          공유하지 않는 쪽을 택한 것이고, 상용 엔진(UE RDG)도 같은 방식이다.
         * @note 이 플래그는 그 규칙을 **감시**하기 위한 것이다. 디버그 빌드에서 규칙이 깨지면
         *       백엔드가 로그로 알린다. 규칙이 조용히 무너지는 것을 막는 것이 목적이다.
         * @note 값의 출처는 `RenderGraph::executeParallel` 이다. 백엔드는 MODULE DLL 로 따로
         *       빌드되므로 전역 변수를 export 하지 않는다. 정책은 Engine, 메커니즘은 디바이스가 맡는다.
         */
        void setParallelRecording( bool bEnable ) { _bParallelRecording = bEnable; }

        /**
         * @brief 지금 리소스 테이블(bindless 레지스트리, 텍스처 레코드)을 바꿔도 되는 시점인지 확인합니다.
         * @details 백엔드의 register/unregister 진입부에서 부른다. 규칙이 깨지면 로그를 남기고
         *          **디버거를 세운다**(`SW_LOG_ASSERT`). 로그만으로는 다른 줄에 묻혀 지나치기 쉽다.
         *          Release 에서는 제거된다.
         * @param pWhat 로그에 남길 호출 지점 이름입니다.
         */
        void assertRegistryMutableNow( const utf8* pWhat ) const;

        /**
         * @brief 병렬 기록 중에 배리어가 나왔음을 **진단으로만** 남깁니다(디버그 전용, 8회까지).
         * @details 배리어는 레벨 프롤로그(`RenderGraph::setLevelPrologue`)가 단일 스레드에서 미리
         *          발행하는 것이 원칙이다. 다만 프롤로그는 그래프가 **선언한** 입출력만 알 수 있어서
         *          선언이 빠진 자원(패스 콜백이 직접 바인딩하는 텍스처)은 놓친다. 그래서 이것은 assert 가 아니다.
         *          깊이 첨부(`_depthAttachment`)는 `FrameRenderer::bindPassCallbacks` 가 그래프에 선언한다.
         *          실제 전이는 락이 보호하므로 남아 있어도 안전하다. 여기 찍히는 이름이 곧
         *          "파이프라인 선언을 채우면 사라질 항목" 이다.
         * @note bindless 레지스트리 쪽(`assertRegistryMutableNow`)은 불변식이 확실해서 assert 다.
         *       두 감시의 강도가 다른 이유가 이것이다.
         */
        void reportBarrierDuringRecording( const utf8* pWhat ) const;

        // ------------------------------------------------------------------------------
        // 5) GPU 메모리: 자원 장부
        // ------------------------------------------------------------------------------
        /**
         * @brief 이 디바이스가 만든 GPU 자원의 장부입니다. 백엔드가 자원을 만들 때와 실제로 해제할 때 기록하고, 엔진(프로파일 보고, 에디터 패널)이 읽습니다.
         * @details 장부는 디바이스와 수명이 같다. 백엔드를 바꾸면 새 디바이스가 빈 장부로 시작한다.
         */
        RHIMemoryLedger& getMemoryLedger() { return *_memoryLedger; }
        /** @brief 읽기 전용 장부입니다. */
        const RHIMemoryLedger& getMemoryLedger() const { return *_memoryLedger; }
        /**
         * @brief 드라이버에게 GPU 메모리 사용량과 예산을 물어 장부에 기록합니다(`RHIMemoryLedger::getDriverBudget`). 백엔드가 물을 수 없으면 모든 항목이 "모름" 입니다.
         * @details **프레임을 기록하는 스레드(렌더 스레드)에서 불러야 한다.** GL 은 컨텍스트를 가진 채 묻는데, 다른 스레드에서 부르면 렌더 스레드가 그 프레임의
         *          컨텍스트를 놓을 때까지 기다린다. 엔진은 Present 뒤 몇 프레임마다 부르고(`RenderThread`), 읽는 쪽(프로파일 보고, 에디터 패널)은
         *          장부에 기록된 마지막 값을 본다.
         */
        void refreshGPUMemoryBudget();

    protected:
        /** @brief 백엔드의 실제 GPU 대기와 해제 큐 비우기입니다. `waitIdle` 이 렌더 스레드를 먼저 비운 뒤, `shutdown` 이 2단계로 부릅니다. */
        virtual void waitIdleInternal() = 0;
        /**
         * @brief 종료 3단계입니다. 프레임 스트림 컨텍스트를 해제하고 살아 있는 커맨드 리스트를 디바이스에서 분리합니다.
         * @details 리스트는 디바이스보다 오래 살 수 있다(렌더 그래프가 프레임을 넘겨 보관한다). 분리하지 않으면 그쪽 소멸자가 이미 종료된 디바이스에 자원을 반납하려 한다.
         */
        virtual void detachCommandRecordingInternal() = 0;
        /**
         * @brief 백엔드가 자기 네이티브 핸들을 채웁니다. 버전 대조는 이미 끝났고 `outHandles` 는 기본값(`_backend` 만 채운 상태)입니다.
         * @details 기본 구현은 `getNativeDevice`, `getNativeContext`, `getNativeCommandQueue` 세 항목을 채우고, 디바이스가 없으면 false 를 반환한다.
         *          더 내놓을 것이 있는 백엔드(Vulkan 의 인스턴스, 물리 디바이스, 큐 패밀리, 렌더 패스)가 재정의한다.
         */
        [[nodiscard]] virtual bool queryNativeHandlesInternal( RHINativeHandles& outHandles ) const;
        /**
         * @brief 백엔드가 드라이버의 GPU 메모리 사용량과 예산을 채웁니다. `outBudget` 은 모든 항목이 "모름" 인 기본값으로 전달됩니다.
         * @details 물을 수 없으면(API 나 확장이 없음) false 를 반환한다. 아는 항목만 플래그를 켜고, 모르는 항목은 임의로 채우지 않는다.
         */
        [[nodiscard]] virtual bool queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget ) = 0;

        IRenderSurface* _pSurface;
        /// @brief 타임스탬프 계측 능력입니다(findTimestampProvider 참고). 능력을 가진 백엔드가 생성자에서 자기를 적습니다.
        IRHITimestampProvider* _pTimestampProvider;
        /// @brief 컨텍스트 스레드 바인딩 능력입니다(findContextThreadBinding 참고). 능력을 가진 백엔드가 생성자에서 자기를 적습니다.
        IRHIContextThreadBinding* _pContextThreadBinding;
        RenderThreadDrainFunction _pfnRenderThreadDrain;
        void*                     _pRenderThreadDrainContext;
        std::thread::id           _renderThreadID;
        uint32                    _backBufferWidth;
        uint32                    _backBufferHeight;
        bool                      _bPreferredVSync;
        /// @brief 프레임 스트림을 자를 때마다 곧바로 제출할지 나타냅니다(setImmediateSubmit 참고).
        bool _bImmediateSubmit;
        /// @brief 지금이 병렬 패스 기록 구간인지 나타냅니다(setParallelRecording 참고).
        bool _bParallelRecording;
        /// @brief 백엔드가 실제로 소프트웨어 어댑터(WARP, CPU Vulkan)를 쓰는지 나타냅니다. 백엔드가 `initializeInternal` 에서 실제 어댑터를 보고 기록합니다.
        bool _bSoftwareAdapter;
        /// @brief 마지막 `initialize` 의 결과입니다(getInitResult 참고).
        RHIInitResult _initResult;
        /// @brief `shutdown` 을 끝냈다. 그 뒤에 쌓인 해제는 다음 `shutdown`(백엔드 소멸자)이 단언한다. `initialize` 가 지운다.
        bool _bShutDown;
        /// @brief GPU 자원 장부입니다(getMemoryLedger 참고). 기반 클래스 멤버이므로 해제 큐를 비우는 백엔드 소멸자보다 오래 삽니다.
        unique_ptr<RHIMemoryLedger> _memoryLedger;
        /// @brief 렌더 스레드가 프레임을 끝낼 때까지 미룬 핸들 반환과, 넘긴 · 끝낸 프레임 수입니다(releaseHandle 참고).
        unique_ptr<RHIDeferredHandleQueue> _pDeferredHandleQueue;

    private:
        /** @brief 미룬 핸들 중 기다리던 프레임이 끝난 것만 해제합니다(`notifyRenderFrameRetired`). */
        void releaseRetiredHandles();
    };
} // namespace sw
