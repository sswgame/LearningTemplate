/**
 * @file CanvasGlyphAtlasMirror.h
 * @brief 캔버스 렌더러의 글리프 아틀라스 미러입니다(렌더 스레드). 페이지마다 CPU 미러를 보관하고 페이지 텍스처를 만들어 바뀐 구간만 올립니다.
 * @details `CanvasRenderer` 가 값으로 소유합니다. 언리얼 Slate 의 `FSlateFontAtlasRHI`(폰트 아틀라스의 GPU 쪽)에 해당합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Text/GlyphAtlas.h"

namespace sw
{
    class IRHIDevice;
    class IRHIResourceFactory;

    /**
     * @class CanvasGlyphAtlasMirror
     * @brief 글리프 아틀라스 페이지의 CPU 미러와 그 GPU 텍스처 · SRV 입니다. 기록 전(`CanvasRenderer::prepareFrame`)에 반영 · 업로드하고, 기록 중에는 SRV 만 읽힙니다.
     * @details 디바이스를 다시 만들거나 백엔드를 바꿔도 게임 스레드에 묻지 않고 미러에서 페이지를 다시 업로드합니다(`release` 는 GPU 자원만 놓고 미러는 남깁니다).
     */
    class SW_API CanvasGlyphAtlasMirror
    {
    public:
        /** @brief 페이지 하나에서 이 수를 넘는 구간 업로드는 그 경계 상자 하나로 합칩니다. 업로드 비용은 바이트가 아니라 호출 수가 지배합니다(DX12 호출당 ~3 us). */
        static constexpr uint32 kMaxRegionUploadPerPage = 8;

        CanvasGlyphAtlasMirror();
        ~CanvasGlyphAtlasMirror();

        CanvasGlyphAtlasMirror( const CanvasGlyphAtlasMirror& )            = delete;
        CanvasGlyphAtlasMirror& operator=( const CanvasGlyphAtlasMirror& ) = delete;

        /** @brief 아틀라스 업로드를 미러에 반영하고 업로드할 구간으로 적은 뒤 @p inoutListUpload 를 비웁니다(같은 프레임 묶음을 다시 그려도 두 번 올리지 않게). */
        void applyUploads( vector<GlyphAtlasUpload>& inoutListUpload );
        /**
         * @brief 페이지 텍스처를 갖추고(없으면 만듭니다) 바뀐 구간의 업로드를 냅니다. GPU 완료를 기다리지 않습니다(새 페이지는 첫 업로드가 끝난 이후 프레임부터 그려진다).
         * @details 업로드 호출 수(`getLastUploadCallCount`)를 이번 호출 것으로 새로 셉니다.
         */
        void uploadPages( IRHIDevice& device );
        /** @brief 페이지 @p page 의 SRV 입니다. 첫 업로드가 끝나기 전(SRV 없음)이거나 멈춘 페이지, 없는 페이지면 kInvalidDescriptorIndex 입니다. */
        RHIDescriptorIndex findPageSRV( uint32 page ) const;
        /** @brief GPU 자원을 해제합니다(디바이스가 없으면 핸들만 잊습니다). 미러는 남겨 다음 `uploadPages` 가 텍스처를 다시 만들고 전체를 올립니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 지금 보관 중인 아틀라스 페이지 수입니다(미러 기준). */
        uint32 getPageCount() const { return static_cast<uint32>( _listPage.size() ); }
        /** @brief 마지막 `uploadPages` 가 받아들여진 업로드 제출 수입니다(전체 페이지와 구간의 합이며 진단과 테스트용입니다. `Busy` 는 세지 않습니다). */
        uint32 getLastUploadCallCount() const { return _lastUploadCallCount; }

        /**
         * @brief 한 페이지의 구간 목록이 @p maxRegionCount 를 넘으면 그 경계 상자 하나로 바꿉니다(순수 함수이며 테스트가 직접 부릅니다).
         * @details 구간이 적으면 그대로 둡니다. 작은 구간 여럿을 따로 업로드하는 것이 경계 상자 하나를 업로드하는 것보다 바이트가 적고, 넘으면 호출 수가 더 비쌉니다.
         */
        static void mergeUploadRegions( vector<GlyphAtlasRect>& inoutListRegion, uint32 maxRegionCount );

    private:
        /**
         * @brief 아틀라스 페이지 하나입니다. CPU 미러(SDF 는 R8, MSDF 는 RGBA8, 페이지 크기²)와 그 텍스처, SRV, 아직 올리지 못한 구간을 담습니다.
         * @details 상태는 셋입니다: 첫 업로드 중(`_registerTicket` 유효, SRV 없음 — 그리지 않는다), 그릴 수 있음(SRV 있음), 멈춤(`_bFailed` — 그리지도 올리지도 않는다).
         */
        struct AtlasPage
        {
            vector<uint8>          _bytes{};            ///< 미러(행 우선, `GlyphAtlas::kPageSize` × 텍셀당 바이트 행)
            uint8                  _bytesPerTexel{ 1 }; ///< 미러의 텍셀당 바이트입니다(첫 업로드가 정하고, 페이지 텍스처 형식이 이것을 따른다).
            vector<GlyphAtlasRect> _listDirty{};        ///< 아직 내지 못한 구간(미러 기준). `Busy` 로 못 낸 것은 다음 프레임까지 남는다
            RHITextureHandle       _texture{ 0 };
            RHITransferTicket      _registerTicket{}; ///< 새 텍스처의 첫 전체 업로드 티켓. `Complete` 가 된 프레임에 SRV 를 등록하고 비운다
            RHIDescriptorIndex     _srv{ kInvalidDescriptorIndex };
            uint8                  _bWholePageDirty{ SW_FALSE }; ///< 페이지 전체를 업로드해야 한다(새 텍스처이거나 비운 페이지)
            uint8                  _bFailed{ SW_FALSE };         ///< 생성 · 업로드 · 등록이 다시 시도해도 안 되는 이유로 실패했다(오류를 남겼다). `release` 가 푼다
        };

        /** @brief 업로드 하나를 미러에 반영하고 업로드할 구간으로 적습니다. */
        void applyUpload( const GlyphAtlasUpload& upload );
        /**
         * @brief 페이지 텍스처를 갖추고(없으면 만듭니다) 바뀐 구간의 업로드를 냅니다. GPU 완료를 기다리지 않습니다.
         * @details 새 페이지의 SRV 는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록합니다. `Busy` 는 다음 프레임에 다시 내고, `Rejected` · `Failed` 는 페이지를 멈춥니다.
         */
        void uploadPage( IRHIDevice& device, AtlasPage& page );
        /** @brief 페이지 텍스처가 없으면 만들고 첫 전체 업로드를 표시합니다. 만들지 못해 페이지를 멈췄으면 false 입니다. */
        bool ensurePageTexture( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 새 페이지의 첫 업로드가 끝났으면 bindless 로 등록합니다. 아직 끝나지 않았거나 페이지를 멈췄으면 false 이며 이번 프레임은 더 올리지 않습니다. */
        bool registerPage( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 페이지 전체를 올립니다(`Busy` 는 더티 상태를 두고 다음 프레임). */
        void uploadWholePage( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 더티 구간을 합쳐 잇달아 냅니다(`Busy` 에서 멈추고 나머지는 다음 프레임). */
        void uploadRegions( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 페이지를 멈춥니다: 오류를 남기고, `release` 전까지 그리지도 올리지도 않습니다(폴백 없이 실패를 드러냅니다). */
        void failPage( AtlasPage& page, string_view reason );

        vector<AtlasPage> _listPage;
        vector<uint8>     _regionScratchBytes; ///< 구간 업로드용으로 행 사이의 빈틈을 없앤 사본
        uint32            _lastUploadCallCount;
    };
} // namespace sw
