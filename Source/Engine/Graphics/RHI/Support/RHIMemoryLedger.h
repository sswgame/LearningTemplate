/**
 * @file RHIMemoryLedger.h
 * @brief 디바이스 하나가 만든 GPU 자원의 크기를 종류별로 세는 장부와, 드라이버가 알려 주는 사용량과 예산을 정의합니다.
 * @details 백엔드는 자원을 만드는 곳 한 군데에서 `recordAllocation` 을, 실제로 해제하는 곳(지연 해제 콜백) 한 군데에서 `recordFree` 를 부른다.
 *          크기는 장부가 키별로 기억하므로 해제 쪽은 키만 넘기고, 더한 만큼 정확히 뺀다. 정책(무엇을 보여 줄지)은 Engine 이 정하고
 *          백엔드는 크기와 키만 낸다. 백엔드는 별도 모듈이라 Engine 전역을 볼 수 없어 장부는 디바이스(`IRHIDevice`)가 소유한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
    struct RHITextureDesc;

    /**
     * @brief GPU 메모리 장부의 항목입니다. 자원이 무엇에 쓰이는지로 나눕니다.
     * @details 새 항목을 추가하면 `RHIMemoryLedger::getKindName` 의 이름 테이블에도 한 줄을 추가한다(항목 수는 static_assert 가 검사한다).
     */
    enum class RHIMemoryKind : uint8
    {
        Texture,       ///< 샘플링 전용 텍스처(에셋, 폰트, 에디터 아이콘)
        RenderTarget,  ///< 렌더 타깃, 깊이 첨부, UAV 텍스처 중 트랜지언트 풀 밖의 것(게임 뷰, TAA 히스토리, 캡처)
        TransientPool, ///< 렌더 그래프의 트랜지언트 첨부 풀(`TransientAttachmentPool`)
        Buffer,        ///< 상수, 구조화, 정점, 인덱스 버퍼
        Staging,       ///< 업로드 스테이징: 디바이스가 가진 업로드 힙 · 링(DX12 · Vulkan · GL), DX11 은 업로드마다 만들어 전송이 끝나면 놓는 STAGING 텍스처
        Descriptor,    ///< 셰이더 가시 디스크립터 힙과 디스크립터 풀
        MaxKinds
    };

    /** @brief 장부 항목 수입니다(`MaxKinds` 를 뺀 값의 개수). */
    inline constexpr uint32 kRHIMemoryKindCount = static_cast<uint32>( RHIMemoryKind::MaxKinds );

    /** @brief `recordAllocation` 에 넘기는 "크기를 모름" 값입니다. 그 자원은 바이트가 아니라 "크기 모름" 항목으로 셉니다. */
    inline constexpr uint64 kRHIMemoryUnknownBytes = 0;

    /**
     * @brief 장부의 바이트가 무엇을 센 것인지 나타냅니다.
     * @details DX12 와 Vulkan 은 드라이버가 답하는 실제 할당 크기(정렬과 패딩 포함)를 기록한다. DX11 과 GL 은 그것을 물을 API 가 없어
     *          서술(크기, 포맷, 밉, 면)로 계산한 논리 크기를 기록한다. 드라이버의 정렬, 압축, 메타데이터만큼 실제보다 작다.
     */
    enum class RHIMemorySizeBasis : uint8
    {
        Allocation, ///< 드라이버가 답한 할당 크기
        Logical     ///< 서술로 계산한 크기(실제 할당보다 작을 수 있다)
    };

    /** @brief 장부 키의 공간입니다. 버퍼 핸들과 텍스처 핸들은 서로 다른 테이블이라 같은 정수가 둘 다에 있을 수 있습니다. */
    enum class RHIMemoryKeySpace : uint8
    {
        Buffer,       ///< `RHIBufferHandle`
        Texture,      ///< `RHITextureHandle`
        DeviceObject, ///< 핸들이 없는 디바이스 내부 객체(스테이징 링, 디스크립터 힙, 풀). 네이티브 객체 주소로 구분한다.
        MaxSpaces
    };

    /** @brief 키 공간 수입니다. */
    inline constexpr uint32 kRHIMemoryKeySpaceCount = static_cast<uint32>( RHIMemoryKeySpace::MaxSpaces );

    /** @brief 장부의 키입니다. 만들 때 준 키를 해제할 때 그대로 줍니다. */
    struct RHIMemoryKey
    {
        uint64            _id{ 0 };
        RHIMemoryKeySpace _space{ RHIMemoryKeySpace::Buffer };

        /** @brief 버퍼 핸들의 키입니다. */
        static constexpr RHIMemoryKey makeBuffer( uint64 handle ) { return RHIMemoryKey{ handle, RHIMemoryKeySpace::Buffer }; }
        /** @brief 텍스처 핸들의 키입니다. */
        static constexpr RHIMemoryKey makeTexture( uint64 handle ) { return RHIMemoryKey{ handle, RHIMemoryKeySpace::Texture }; }
        /** @brief 핸들이 없는 디바이스 내부 객체의 키입니다. 살아 있는 동안 주소가 겹치지 않는 네이티브 객체를 넘깁니다. */
        static RHIMemoryKey makeDeviceObject( const void* pObject )
        {
            return RHIMemoryKey{ static_cast<uint64>( reinterpret_cast<uintptr_t>( pObject ) ), RHIMemoryKeySpace::DeviceObject };
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 장부 항목 하나의 현재 값입니다. */
    struct RHIMemoryKindStats
    {
        uint64 _liveBytes{ 0 };        ///< 크기를 아는 살아 있는 자원의 바이트 합
        uint32 _liveCount{ 0 };        ///< 크기를 아는 살아 있는 자원 수
        uint32 _unknownSizeCount{ 0 }; ///< 크기를 모르는 살아 있는 자원 수. 바이트 합에는 들어 있지 않다.
    };
} // namespace sw

namespace sw
{
    /** @brief 드라이버 사용량이 누구의 것인지 나타냅니다. */
    enum class RHIMemoryScope : uint8
    {
        Process, ///< 이 프로세스의 사용량(DXGI `QueryVideoMemoryInfo`, `VK_EXT_memory_budget`)
        Device   ///< 디바이스 전체의 사용량이며 다른 프로세스 몫을 포함한다(`GL_NVX_gpu_memory_info`).
    };

    /**
     * @brief 드라이버가 알려 주는 GPU 메모리 총량과 예산입니다. 모르는 항목은 플래그가 꺼져 있고 값은 0 이며, 임의로 채우지 않습니다.
     * @details 사용량은 로컬(비디오 메모리)과 비로컬(GPU 가 보는 시스템 메모리, 곧 업로드 힙)을 합한 값이고, 예산은 로컬 예산이다.
     *          엔진 장부도 업로드 힙 버퍼를 세므로 둘을 비교할 수 있다.
     */
    struct SW_API RHIMemoryBudget
    {
        uint64                 _usageBytes;          ///< 드라이버가 센 사용량(로컬 + 비로컬)
        uint64                 _budgetBytes;         ///< 로컬 메모리 예산(넘으면 OS 가 내쫓기 시작한다)
        uint64                 _availableBytes;      ///< 드라이버가 답한 남은 양
        RHIMemoryScope         _scope;               ///< 사용량이 누구의 것인가
        uint8                  _bUsageKnown     : 1; ///< `_usageBytes` 가 드라이버 값이다.
        uint8                  _bBudgetKnown    : 1; ///< `_budgetBytes` 가 드라이버 값이다.
        uint8                  _bAvailableKnown : 1; ///< `_availableBytes` 가 드라이버 값이다.
        [[maybe_unused]] uint8 _reserved        : 5;

        /** @brief 모든 항목을 "모름" 으로 둡니다. */
        RHIMemoryBudget() noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 장부와 드라이버 값을 맞춰 본 결과입니다. 로그 보고와 에디터 패널이 같은 계산을 씁니다.
     * @details "엔진 밖" 은 드라이버 사용량에서 장부 합을 뺀 값이다. 스왑체인, 드라이버 내부, 장부에 오르지 않은 디바이스 자원이 여기에 속한다.
     *          사용량을 모르거나 그것이 디바이스 전체 값이면(다른 프로세스 몫이 섞인다) 계산하지 않는다. 장부가 논리 크기면 음수가 될 수도 있어
     *          부호 있는 값이다.
     */
    struct RHIMemorySummary
    {
        RHIMemoryBudget    _budget{};                                    ///< 마지막으로 읽은 드라이버 값
        uint64             _trackedBytes{ 0 };                           ///< 장부의 바이트 합(크기 모름 항목 제외)
        int64              _outsideBytes{ 0 };                           ///< 드라이버 사용량 − 장부 합
        uint32             _unknownSizeCount{ 0 };                       ///< 크기를 모르는 살아 있는 자원 수(모든 항목의 합)
        RHIMemorySizeBasis _sizeBasis{ RHIMemorySizeBasis::Allocation }; ///< 장부 바이트의 기준
        uint8              _bOutsideKnown{ SW_FALSE };                   ///< `_outsideBytes` 를 계산했는가
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHIMemoryLedger
     * @brief 디바이스 하나의 GPU 자원 크기를 키별로 기억하고 종류별로 합합니다. 여러 스레드(게임, 렌더, 로더)에서 불러도 됩니다.
     */
    class SW_API RHIMemoryLedger
    {
    public:
        /** @brief 항목의 표시 이름을 반환합니다. 보고와 에디터 패널이 같은 이름을 씁니다. */
        static const utf8* getKindName( RHIMemoryKind kind );
        /** @brief 크기 기준의 표시 이름을 반환합니다. */
        static const utf8* getSizeBasisName( RHIMemorySizeBasis basis );
        /**
         * @brief 텍스처 서술이 어느 항목에 속하는지 정합니다. 네 백엔드가 이 판정 하나를 씁니다.
         * @details 트랜지언트 풀이 만든 것(`_bIsTransient`)이 먼저이고, 렌더 타깃, 깊이, UAV 면 RenderTarget, 나머지는 Texture 다.
         */
        static RHIMemoryKind classifyTexture( const RHITextureDesc& desc );
        /**
         * @brief 서술로 텍스처의 논리 바이트를 계산합니다(밉 전부 × 면 수). 할당 크기를 물을 수 없는 백엔드(DX11, GL)가 씁니다.
         * @return 포맷의 크기를 모르면 `kRHIMemoryUnknownBytes` 입니다.
         */
        static uint64 computeTextureLogicalBytes( const RHITextureDesc& desc );

    public:
        /** @brief 빈 장부를 만듭니다. 기준은 Allocation 이고 드라이버 값은 "모름" 입니다. */
        RHIMemoryLedger();
        /** @brief 복사를 금지합니다. */
        RHIMemoryLedger( const RHIMemoryLedger& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        RHIMemoryLedger& operator=( const RHIMemoryLedger& ) = delete;

        /**
         * @brief 자원 하나를 장부에 올립니다. 백엔드가 자원을 만드는 곳 한 군데에서 부릅니다.
         * @param bytes 할당 바이트입니다. `kRHIMemoryUnknownBytes` 면 "크기 모름" 항목으로 셉니다.
         * @details 이미 올라 있는 키면 옛 값을 먼저 빼고 새 값으로 바꾼다(합이 어긋나지 않게). 그것은 해제를 빠뜨린 것이라 경고를 남긴다.
         */
        void recordAllocation( const RHIMemoryKey& key, RHIMemoryKind kind, uint64 bytes );
        /**
         * @brief 자원 하나를 장부에서 내립니다. 백엔드가 자원을 실제로 해제하는 곳(GPU 지연 해제 콜백) 한 군데에서 부릅니다.
         * @details 올린 때의 종류와 바이트를 그대로 뺀다. 올라 있지 않은 키는 무시한다(장부를 켜기 전에 만든 자원).
         */
        void recordFree( const RHIMemoryKey& key );

        /** @brief 항목 하나의 현재 값입니다. */
        RHIMemoryKindStats getStats( RHIMemoryKind kind ) const;
        /** @brief 모든 항목의 바이트 합입니다(크기 모름 항목 제외). */
        uint64 getTrackedBytes() const;
        /** @brief 항목을 바이트가 큰 순서로 늘어놓습니다(같으면 enum 순서). 보고와 에디터 패널이 같은 순서를 씁니다. */
        array<RHIMemoryKind, kRHIMemoryKindCount> makeKindOrderByLiveBytes() const;

        /** @brief 이 장부의 바이트 기준입니다. */
        RHIMemorySizeBasis getSizeBasis() const { return _sizeBasis; }
        /** @brief 백엔드가 자기 기준을 기록합니다(초기화 때 한 번). */
        void setSizeBasis( RHIMemorySizeBasis basis ) { _sizeBasis = basis; }

        /** @brief 마지막으로 읽은 드라이버 값입니다. */
        RHIMemoryBudget getDriverBudget() const;
        /** @brief 드라이버 값을 기록합니다(`IRHIDevice::refreshGPUMemoryBudget`). */
        void setDriverBudget( const RHIMemoryBudget& budget );

        /** @brief 드라이버 값과 장부를 맞춰 보고 "엔진 밖" 까지 계산합니다. */
        RHIMemorySummary makeSummary() const;

        /**
         * @brief `-gv_profileFrames` 보고의 GPU 테이블을 Info 로그로 남깁니다. CPU 의 "memory by tag" 테이블과 같은 형태입니다.
         * @details 항목별(바이트가 큰 순서)로 살아 있는 바이트, 비율, 개수, 크기 모름 수와 드라이버 사용량, 예산, 남은 양, 엔진 밖을 적는다.
         *          모르는 값은 "모름" 으로 찍는다. Info 로그가 빠지는 구성(Shipping)에서는 아무것도 하지 않는다.
         * @param pBackendName 테이블 머리에 적을 백엔드 이름입니다.
         */
        void report( const utf8* pBackendName ) const;

    private:
        /** @brief 키 하나가 올라 있는 동안 기억하는 값입니다. 해제는 이 값을 그대로 뺍니다. */
        struct LiveEntry
        {
            uint64        _bytes{ 0 };
            RHIMemoryKind _kind{ RHIMemoryKind::Texture };
        };

        /** @brief 장부에서 항목 하나를 뺍니다. `_mutex` 를 잡고 부릅니다. */
        void subtractLocked( const LiveEntry& entry );

        mutable mutex                    _mutex;
        unordered_map<uint64, LiveEntry> _arrMapIDToEntry[kRHIMemoryKeySpaceCount] SW_GUARDED_BY( _mutex );
        RHIMemoryKindStats               _arrStat[kRHIMemoryKindCount];
        RHIMemoryBudget _driverBudget    SW_GUARDED_BY( _mutex );
        RHIMemorySizeBasis               _sizeBasis;
    };
} // namespace sw
