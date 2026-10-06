/**
 * @file CanvasRenderer.h
 * @brief 캔버스 그리기 목록을 GPU 로 그립니다(렌더 스레드) — 글리프 아틀라스 거울 · 구간 업로드, 사각형 구조버퍼, 일괄마다 인스턴스 드로우.
 * @details 언리얼 `FSlateRHIRenderer` 의 자리입니다(게임 스레드가 만든 그리기 목록만 읽는다). `FrameRenderer` 가 소유합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Text/GlyphAtlas.h"

namespace sw
{
    struct CanvasDrawList;
    struct CanvasFrameData;
    struct CanvasQuad;
    struct CanvasTextureRef;

    class IRHICommandList;
    class IRHIDevice;
    class Texture2D;

    /**
     * @class CanvasRenderer
     * @brief 캔버스 렌더러입니다. 기록 전(`prepareFrame`)에 자원을 갖추고, 기록 중(`drawList`)에는 조회 · 기록만 합니다.
     * @details 아틀라스 페이지마다 CPU 거울을 들어, 디바이스를 다시 만들거나 백엔드를 바꿔도 게임 스레드에 묻지 않고 페이지를 다시 올립니다
     *          (`release` 는 GPU 자원만 놓고 거울은 남긴다). 사각형 버퍼는 **프레임에 한 번** 갱신합니다 — 같은 프레임에 같은 버퍼를 두 번 갱신하지 않는다.
     *          내용 번호(`CanvasFrameData::_contentRevision`)가 지난번과 같으면 갱신을 건너뜁니다.
     */
    class SW_API CanvasRenderer
    {
    public:
        /** @brief 페이지 하나에서 이 수를 넘는 구간 업로드는 그 경계 상자 하나로 합칩니다 — 업로드 비용은 바이트가 아니라 호출 수가 지배한다(DX12 호출당 ~3 us). */
        static constexpr uint32 kMaxRegionUploadPerPage = 8;

        CanvasRenderer();
        ~CanvasRenderer();

        CanvasRenderer( const CanvasRenderer& )            = delete;
        CanvasRenderer& operator=( const CanvasRenderer& ) = delete;

        /**
         * @brief 프레임의 캔버스를 그릴 준비를 합니다 — **기록 시작 전에** 부릅니다(버퍼 · 텍스처 생성과 bindless 등록은 기록 중에 할 수 없다).
         * @details 아틀라스 업로드를 거울에 반영하고(소비한 업로드는 @p inoutFrame 에서 비운다), 페이지 텍스처를 만들거나(없으면) 바뀐 구간을 올리고,
         *          사각형 버퍼를 갱신합니다 — 주 출력 다음에 렌더 텍스처 대상들을 이어 한 버퍼에(같은 프레임에 버퍼를 두 번 갱신하지 않게), 대상마다 시작 자리는
         *          `getTargetQuadBase`. RT.Canvas.Upload.
         */
        void prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame );
        /**
         * @brief 목록 하나를 지금 열린 렌더 패스에 그립니다. 일괄마다 가위 · 루트 상수(시작 · 텍스처 넷 · 대상 크기 · 색각 보정) · `drawInstanced( 6, n )`.
         * @param quadBase       사각형 버퍼에서 이 목록이 시작하는 자리(주 출력 0, 대상은 `getTargetQuadBase`)
         * @param pso            대상 포맷의 캔버스 PSO
         * @param targetWidth    대상 픽셀 크기(가위가 없는 일괄의 가위 · 셰이더의 NDC 변환)
         * @param bNativeBindless DX12 · Vulkan 이면 텍스처를 bindless 전역 번호로, 아니면 t5..t8 에 서수로 건다
         * @param colorVisionMode 색각 보정 방식(`CanvasFrameData::_colorVisionMode` — 주 출력만, 렌더 텍스처 대상은 0)
         * @return 그린 일괄 수
         */
        uint32 drawList( IRHICommandList& cmd, const CanvasDrawList& list, uint32 quadBase, RHIPipelineStateHandle pso, uint32 targetWidth, uint32 targetHeight,
                         bool bNativeBindless, uint32 colorVisionMode = 0 ) const;
        /** @brief 마지막 `prepareFrame` 에서 대상 @p targetIndex 의 사각형이 버퍼에서 시작하는 자리입니다. */
        uint32 getTargetQuadBase( uint32 targetIndex ) const { return targetIndex < _listTargetQuadBase.size() ? _listTargetQuadBase[targetIndex] : 0; }
        /**
         * @brief GPU 자원을 놓습니다(디바이스가 없으면 핸들만 잊는다). 아틀라스 거울은 남깁니다 — 다음 `prepareFrame` 이 다시 올린다.
         * @details 경로로 빌린 그림은 캐시에 돌려줍니다(다음 `prepareFrame` 이 다시 빌린다).
         */
        void release( IRHIDevice* pDevice );

        /** @brief 지금 들고 있는 아틀라스 페이지 수입니다(거울 기준). */
        uint32 getAtlasPageCount() const { return static_cast<uint32>( _listAtlasPage.size() ); }
        /** @brief 마지막 `prepareFrame` 이 올린 아틀라스 업로드 호출 수입니다(전체 페이지 · 구간 합 — 진단 · 시험). */
        uint32 getLastUploadCallCount() const { return _lastUploadCallCount; }

        /**
         * @brief 한 페이지의 구간 목록이 @p maxRegionCount 를 넘으면 그 경계 상자 하나로 바꿉니다(순수 함수 — 시험이 직접 부른다).
         * @details 구간이 적으면 그대로 둔다 — 작은 구간 여럿을 따로 올리는 것이 경계 상자 하나를 올리는 것보다 바이트가 적고, 넘으면 호출 수가 더 비싸다.
         */
        static void mergeUploadRegions( vector<GlyphAtlasRect>& inoutListRegion, uint32 maxRegionCount );

    private:
        /** @brief 아틀라스 페이지 하나입니다 — CPU 거울(R8, 페이지 크기²)과 그 텍스처 · SRV, 이번 프레임에 바뀐 구간. */
        struct AtlasPage
        {
            vector<uint8>          _bytes{};     ///< 거울(행 우선, `GlyphAtlas::kPageSize` 바이트 행)
            vector<GlyphAtlasRect> _listDirty{}; ///< 올릴 구간(거울 기준)
            RHITextureHandle       _texture{ 0 };
            RHIDescriptorIndex     _srv{ kInvalidDescriptorIndex };
            uint8                  _bWholePageDirty{ SW_FALSE }; ///< 페이지 전체를 올려야 한다(새 텍스처 · 비운 페이지)
        };

        /** @brief 업로드 하나를 거울에 옮기고 올릴 구간으로 적습니다. */
        void applyAtlasUpload( const GlyphAtlasUpload& upload );
        /** @brief 페이지 텍스처를 갖추고(없으면 만든다) 바뀐 구간을 올립니다. */
        void uploadAtlasPage( IRHIDevice& device, AtlasPage& page );
        /** @brief 경로로 빌린 그림 하나입니다(텍스처는 `TextureCache` 가 소유 — `release` 가 돌려준다). */
        struct PathTexture
        {
            hashed_string _path{};
            Texture2D*    _pTexture{ nullptr }; ///< 못 읽었으면 nullptr(캐시가 이유를 남겼다 — 다시 묻지 않는다)
        };

        /** @brief 일괄 텍스처 하나의 SRV 입니다. 없으면 kInvalidDescriptorIndex. */
        RHIDescriptorIndex findTextureSrv( const CanvasTextureRef& texture ) const;
        /** @brief 목록의 경로 그림을 `TextureCache` 에서 빌립니다(처음 보는 경로만). 엔진 서비스가 없으면(시험 하네스 밖) 아무것도 하지 않는다. */
        void acquirePathTextures( IRHIDevice& device, const CanvasDrawList& list );

        vector<AtlasPage>       _listAtlasPage;
        vector<PathTexture>     _listPathTexture;    ///< 경로 그림(HUD 아이콘 · 조준선 — 몇 개뿐이라 선형 검색)
        vector<uint8>           _regionScratchBytes; ///< 구간 업로드용 빈틈없는 행 사본
        vector<CanvasQuad>      _listQuadScratch;    ///< 대상이 있을 때 주 출력 + 대상 사각형을 이어 붙인 사본
        vector<uint32>          _listTargetQuadBase; ///< 대상마다 버퍼 안 시작 자리
        RHIStructuredBufferSlot _quadBuffer;
        uint64                  _uploadedRevision; ///< 사각형 버퍼에 지금 든 내용의 서명(주 출력 · 대상 내용 번호 — 0 = 없음)
        uint32                  _uploadedQuadCount;
        uint32                  _lastUploadCallCount;
    };
} // namespace sw
