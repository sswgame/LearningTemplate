/**
 * @file RHIMemoryLedger.h
 * @brief 디바이스 하나가 만든 GPU 자원의 크기를 종류별로 세는 장부입니다.
 * @details 백엔드는 자원을 만드는 자리 하나에서 `recordAllocation`, 실제로 놓는 자리(지연 해제 콜백) 하나에서 `recordFree` 를 부릅니다.
 *          크기는 장부가 키별로 기억하므로 해제 쪽은 키만 넘기고, 더한 만큼 정확히 뺍니다. 정책(무엇을 보여 줄지)은 Engine 의 것이고
 *          백엔드는 크기와 키만 냅니다 — 백엔드는 별도 모듈이라 Engine 전역을 볼 수 없어 장부는 디바이스(`IRHIDevice`)가 소유합니다.
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
     * @brief GPU 메모리 장부의 줄입니다. 자원이 무엇에 쓰이는가로 나눕니다.
     * @details 새 줄을 더하면 `RHIMemoryLedger::getKindName` 의 이름 표에도 한 줄을 더합니다(줄 수는 static_assert 가 봅니다).
     */
    enum class RHIMemoryKind : uint8
    {
        Texture,       ///< 샘플링 전용 텍스처(에셋 · 폰트 · 에디터 아이콘)
        RenderTarget,  ///< 렌더 타깃 · 깊이 첨부 · UAV 텍스처 중 트랜지언트 풀 밖의 것(게임뷰 · TAA 히스토리 · 캡처)
        TransientPool, ///< 렌더 그래프의 트랜지언트 첨부 풀(`TransientAttachmentPool`)
        Buffer,        ///< 상수 · 구조 · 정점 · 인덱스 버퍼
        Staging,       ///< 디바이스가 들고 있는 업로드 스테이징 링
        Descriptor,    ///< 셰이더 가시 디스크립터 힙 · 디스크립터 풀
        MaxKinds
    };

    /** @brief 장부 줄 수입니다(`MaxKinds` 를 뺀 값의 개수). */
    inline constexpr uint32 kRHIMemoryKindCount = static_cast<uint32>( RHIMemoryKind::MaxKinds );

    /** @brief `recordAllocation` 에 넘기는 "크기를 모름" 입니다. 그 자원은 바이트가 아니라 "크기 모름" 칸에서 셉니다. */
    inline constexpr uint64 kRHIMemoryUnknownBytes = 0;

    /**
     * @brief 장부의 바이트가 무엇을 센 것인지입니다.
     * @details DX12 · Vulkan 은 드라이버가 답하는 실제 할당 크기(정렬 · 패딩 포함)를 적습니다. DX11 · GL 은 그것을 물을 API 가 없어
     *          서술(크기 · 포맷 · 밉 · 면)로 계산한 논리 크기를 적습니다 — 드라이버의 정렬 · 압축 · 메타데이터만큼 실제보다 작습니다.
     */
    enum class RHIMemorySizeBasis : uint8
    {
        Allocation, ///< 드라이버가 답한 할당 크기
        Logical     ///< 서술로 계산한 크기(실제 할당보다 작을 수 있다)
    };

    /** @brief 장부 키의 공간입니다. 버퍼 핸들과 텍스처 핸들은 서로 다른 표라 같은 정수가 둘 다에 있을 수 있습니다. */
    enum class RHIMemoryKeySpace : uint8
    {
        Buffer,       ///< `RHIBufferHandle`
        Texture,      ///< `RHITextureHandle`
        DeviceObject, ///< 핸들이 없는 디바이스 내부 객체(스테이징 링 · 디스크립터 힙 · 풀) — 네이티브 객체 주소로 가린다
        MaxSpaces
    };

    /** @brief 키 공간 수입니다. */
    inline constexpr uint32 kRHIMemoryKeySpaceCount = static_cast<uint32>( RHIMemoryKeySpace::MaxSpaces );

    /** @brief 장부의 키입니다. 만들 때 준 키를 놓을 때 그대로 줍니다. */
    struct RHIMemoryKey
    {
        uint64            _id{ 0 };
        RHIMemoryKeySpace _space{ RHIMemoryKeySpace::Buffer };

        /** @brief 버퍼 핸들의 키입니다. */
        static constexpr RHIMemoryKey makeBuffer( uint64 handle ) { return RHIMemoryKey{ handle, RHIMemoryKeySpace::Buffer }; }
        /** @brief 텍스처 핸들의 키입니다. */
        static constexpr RHIMemoryKey makeTexture( uint64 handle ) { return RHIMemoryKey{ handle, RHIMemoryKeySpace::Texture }; }
        /** @brief 핸들이 없는 디바이스 내부 객체의 키입니다. 살아 있는 동안 주소가 겹치지 않는 네이티브 객체를 줍니다. */
        static RHIMemoryKey makeDeviceObject( const void* pObject )
        {
            return RHIMemoryKey{ static_cast<uint64>( reinterpret_cast<uintptr_t>( pObject ) ), RHIMemoryKeySpace::DeviceObject };
        }
    };

    /** @brief 장부 한 줄의 지금 값입니다. */
    struct RHIMemoryKindStats
    {
        uint64 _liveBytes{ 0 };        ///< 크기를 아는 살아 있는 자원의 바이트 합
        uint32 _liveCount{ 0 };        ///< 크기를 아는 살아 있는 자원 수
        uint32 _unknownSizeCount{ 0 }; ///< 크기를 모르는 살아 있는 자원 수 — 바이트 합에 들어 있지 않다
    };

    /**
     * @class RHIMemoryLedger
     * @brief 디바이스 하나의 GPU 자원 크기를 키별로 기억하고 종류별로 합합니다. 여러 스레드(게임 · 렌더 · 로더)에서 불러도 됩니다.
     */
    class SW_API RHIMemoryLedger
    {
    public:
        /** @brief 줄의 표시 이름을 반환합니다. 보고와 에디터 패널이 같은 이름을 씁니다. */
        static const utf8* getKindName( RHIMemoryKind kind );
        /** @brief 크기 기준의 표시 이름을 반환합니다. */
        static const utf8* getSizeBasisName( RHIMemorySizeBasis basis );
        /**
         * @brief 텍스처 서술이 어느 줄에 드는지 정합니다. 네 백엔드가 이 판정 하나를 씁니다.
         * @details 트랜지언트 풀이 만든 것(`_bIsTransient`)이 먼저이고, 렌더 타깃 · 깊이 · UAV 면 RenderTarget, 나머지는 Texture 입니다.
         */
        static RHIMemoryKind classifyTexture( const RHITextureDesc& desc );
        /**
         * @brief 서술로 텍스처의 논리 바이트를 계산합니다(밉 전부 × 면 수). 할당 크기를 물을 수 없는 백엔드(DX11 · GL)가 씁니다.
         * @return 포맷의 크기를 모르면 `kRHIMemoryUnknownBytes`.
         */
        static uint64 computeTextureLogicalBytes( const RHITextureDesc& desc );

    public:
        /** @brief 빈 장부를 만듭니다. 기준은 Allocation 입니다. */
        RHIMemoryLedger();
        /** @brief 복사를 금지합니다. */
        RHIMemoryLedger( const RHIMemoryLedger& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        RHIMemoryLedger& operator=( const RHIMemoryLedger& ) = delete;

        /**
         * @brief 자원 하나를 장부에 올립니다. 백엔드가 자원을 만드는 자리 하나에서 부릅니다.
         * @param bytes 할당 바이트. `kRHIMemoryUnknownBytes` 면 "크기 모름" 칸에서 셉니다.
         * @details 이미 올라 있는 키면 옛 값을 먼저 빼고 새 값으로 바꿉니다(합이 어긋나지 않게). 그것은 해제를 빠뜨린 것이라 경고를 남깁니다.
         */
        void recordAllocation( const RHIMemoryKey& key, RHIMemoryKind kind, uint64 bytes );
        /**
         * @brief 자원 하나를 장부에서 내립니다. 백엔드가 자원을 실제로 놓는 자리(GPU 지연 해제 콜백) 하나에서 부릅니다.
         * @details 올린 때의 종류 · 바이트를 그대로 뺍니다. 올라 있지 않은 키는 무시합니다(장부를 켜기 전에 만든 자원).
         */
        void recordFree( const RHIMemoryKey& key );

        /** @brief 줄 하나의 지금 값입니다. */
        RHIMemoryKindStats getStats( RHIMemoryKind kind ) const;
        /** @brief 모든 줄의 바이트 합입니다(크기 모름 칸 제외). */
        uint64 getTrackedBytes() const;
        /** @brief 줄을 바이트가 큰 순서로 늘어놓습니다(같으면 enum 순서). 보고와 에디터 패널이 같은 순서를 씁니다. */
        array<RHIMemoryKind, kRHIMemoryKindCount> makeKindOrderByLiveBytes() const;

        /** @brief 이 장부의 바이트 기준입니다. */
        RHIMemorySizeBasis getSizeBasis() const { return _sizeBasis; }
        /** @brief 백엔드가 자기 기준을 적습니다(초기화 때 한 번). */
        void setSizeBasis( RHIMemorySizeBasis basis ) { _sizeBasis = basis; }

    private:
        /** @brief 키 하나가 올라 있는 동안 기억하는 값입니다. 해제는 이 값을 그대로 뺍니다. */
        struct LiveEntry
        {
            uint64        _bytes{ 0 };
            RHIMemoryKind _kind{ RHIMemoryKind::Texture };
        };

        /** @brief 장부에서 한 항목을 뺍니다. `_mutex` 를 쥐고 부릅니다. */
        void subtractLocked( const LiveEntry& entry );

        mutable mutex                    _mutex;
        unordered_map<uint64, LiveEntry> _arrMapIdToEntry[kRHIMemoryKeySpaceCount];
        RHIMemoryKindStats               _arrStat[kRHIMemoryKindCount];
        RHIMemorySizeBasis               _sizeBasis;
    };
} // namespace sw
