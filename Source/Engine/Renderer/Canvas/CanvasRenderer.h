/**
 * @file CanvasRenderer.h
 * @brief 캔버스 그리기 목록을 GPU 로 그립니다(렌더 스레드). 글리프 아틀라스 미러와 구간 업로드, 사각형 구조 버퍼, 일괄마다 인스턴스 드로우, 렌더 텍스처 대상(월드 공간 UI)을 다룹니다.
 * @details 언리얼 `FSlateRHIRenderer` 에 해당합니다(게임 스레드가 만든 그리기 목록만 읽습니다). `FrameRenderer` 가 소유합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Text/GlyphAtlas.h"

namespace sw
{
    struct CanvasAnchor;
    struct CanvasDrawList;
    struct CanvasFrameData;
    struct CanvasQuad;
    struct CanvasTextureRef;

    class GPUAssertMonitor;
    class IRHICommandList;
    class IRHIDevice;
    class IRHIResourceFactory;
    class RenderPSOCache;
    class Texture2D;

    /**
     * @class CanvasRenderer
     * @brief 캔버스 렌더러입니다. 기록 전(`prepareFrame`)에 자원을 갖추고, 기록 중(`drawList`)에는 조회와 기록만 합니다.
     * @details 아틀라스 페이지마다 CPU 미러를 보관해, 디바이스를 다시 만들거나 백엔드를 바꿔도 게임 스레드에 묻지 않고 페이지를 다시 업로드합니다
     *          (`release` 는 GPU 자원만 놓고 미러는 남깁니다). 사각형 버퍼는 **프레임에 한 번** 갱신합니다. 같은 프레임에 같은 버퍼를 두 번 갱신하지 않습니다.
     *          내용 번호(`CanvasFrameData::_contentRevision`)가 지난번과 같으면 갱신을 건너뜁니다.
     */
    class SW_API CanvasRenderer
    {
    public:
        /** @brief 페이지 하나에서 이 수를 넘는 구간 업로드는 그 경계 상자 하나로 합칩니다. 업로드 비용은 바이트가 아니라 호출 수가 지배합니다(DX12 호출당 ~3 us). */
        static constexpr uint32 kMaxRegionUploadPerPage = 8;

        CanvasRenderer();
        ~CanvasRenderer();

        CanvasRenderer( const CanvasRenderer& )            = delete;
        CanvasRenderer& operator=( const CanvasRenderer& ) = delete;

        /**
         * @brief 프레임의 캔버스를 그릴 준비를 합니다. **기록 시작 전에** 부릅니다(버퍼와 텍스처 생성, bindless 등록은 기록 중에 할 수 없습니다).
         * @details 아틀라스 업로드를 미러에 반영하고(소비한 업로드는 @p inoutFrame 에서 비웁니다), 페이지 텍스처를 만들거나(없으면) 바뀐 구간의 업로드를 내고
         *          (GPU 완료를 기다리지 않는다 — 새 페이지는 첫 업로드가 끝난 이후 프레임부터 그려진다),
         *          사각형 버퍼를 갱신합니다. 주 출력 다음에 렌더 텍스처 대상들을 이어 한 버퍼에 담아(같은 프레임에 버퍼를 두 번 갱신하지 않게) 갱신하고, 대상마다 시작 위치는
         *          `getTargetQuadBase` 로 얻습니다. 프로파일 항목은 RT.Canvas.Upload 입니다.
         *          그 앞에 렌더 텍스처 대상을 `TextureCache` 에서 빌리고(위젯이 그리기 크기를 바꾸면 다시 만듭니다) 목록에서 사라진 대상은 돌려줍니다.
         *          대상 포맷의 캔버스 PSO 가 셋업(@p psoCache, `FramePSOManager::createOutputPSOVariants`)에 없으면 그 대상은 처음 빌릴 때 오류로 거절하고 그리지 않습니다.
         */
        void prepareFrame( IRHIDevice& device, CanvasFrameData& inoutFrame, const RenderPSOCache& psoCache );
        /**
         * @brief 목록 하나를 지금 열린 렌더 패스에 그립니다. 일괄마다 가위, 루트 상수(시작, 텍스처 넷, 대상 크기, 색각 보정), `drawInstanced( 6, n )` 을 기록합니다.
         * @param quadBase       사각형 버퍼에서 이 목록이 시작하는 위치(주 출력 0, 대상은 `getTargetQuadBase`)
         * @param pso            대상 포맷의 캔버스 PSO
         * @param targetWidth    대상 픽셀 크기(가위가 없는 일괄의 가위와 셰이더의 NDC 변환에 씁니다)
         * @param bNativeBindless DX12 와 Vulkan 이면 텍스처를 bindless 전역 번호로, 아니면 t16..t19 에 서수로 바인딩합니다
         * @param colorVisionMode 색각 보정 방식(`CanvasFrameData::_colorVisionMode`, 주 출력만 쓰고 렌더 텍스처 대상은 0)
         * @return 그린 일괄 수입니다
         */
        uint32 drawList( IRHICommandList& cmd, const CanvasDrawList& list, uint32 quadBase, RHIPipelineStateHandle pso, uint32 targetWidth, uint32 targetHeight,
                         bool bNativeBindless, uint32 colorVisionMode = 0 ) const;
        /**
         * @brief 렌더 텍스처 대상(월드 공간 UI)을 지우고 그립니다. 내용 번호와 텍스처가 지난번과 같은 대상은 건너뜁니다(언리얼 위젯 컴포넌트의 다시 그리기 조건에 해당합니다).
         * @details 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트에 기록한다. PSO 는 대상 포맷의 출력 PSO(`RenderPSOCache::findOutputPSO`)뿐이다 —
         *          셋업에 없는 포맷의 대상은 등록(`prepareFrame`)이 이미 거절하고 알렸으므로 여기서는 그리지 않는다(다른 포맷의 PSO 로 대신 그리지 않는다).
         *          그린 텍스처는 셰이더가 읽으므로(머티리얼, 에디터 이미지) 읽기 상태로 둔다.
         */
        void drawTargets( IRHICommandList& cmd, IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache );
        /**
         * @brief 화면 마커 투영 컴퓨트(`canvasmarker.hlsl`)를 기록합니다. 마커 입력(`CanvasFrameData::_listMarker`)이 있는 프레임에만 돌고, 렌더 패스 밖(컴퓨트 프리패스)에서 부릅니다 —
         *        `prepareFrame` 이 사각형 버퍼에 올린 앵커 원소 중 켜진 마커의 것을 화면 좌표 · 배율로 덮어쓰고, 캔버스 그리기(셰이더 읽기)를 위해 읽기 상태로 돌려놓는다.
         * @param instanceBuffer GPUScene 인스턴스 버퍼와 그 SRV 입니다(`GPUScene::getInstanceBuffer` · `getInstanceSRV`). 인스턴스 칸을 읽는 마커가 있으면 읽기 상태로 전환해 바인딩한다. 없으면 @p placeholderSRV 를 건다(읽지 않는다).
         * @param pso `RenderPassType::CanvasMarker` 의 엔진 PSO 입니다. 0 이면(셰이더가 쿠킹되지 않았거나 컴퓨트 능력이 없는 백엔드) 오류를 한 번 남기고 아무것도 하지 않습니다 —
         *        그 마커는 CPU 가 앵커 배율을 0 으로 올렸으므로 그려지지 않는다(CPU 투영으로 대신하지 않는다).
         * @param bShowInstanceSlots 보기 모드 `MarkerSlots` 입니다 — 참이면 컴퓨트가 앵커의 네 번째 값에 인스턴스 칸 유무 코드를 써 캔버스가 마커 위젯을 칠한다.
         * @param inoutAssert 셰이더 assert 링입니다. assert 변형 PSO 일 때 디스패치 앞에서 u4 에 겁니다(꺼져 있으면 아무것도 하지 않는다).
         */
        void recordMarkerProjection( IRHICommandList& cmd, RHIPipelineStateHandle pso, RHIBufferHandle instanceBuffer, RHIDescriptorIndex instanceSRV, RHIDescriptorIndex placeholderSRV,
                                     GPUAssertMonitor& inoutAssert, bool bShowInstanceSlots );
        /** @brief 마지막 `drawTargets` 가 다시 그린 렌더 텍스처 대상 수입니다(테스트와 진단용). */
        uint32 getLastDrawnTargetCount() const { return _lastDrawnTargetCount; }
        /** @brief 마지막 `prepareFrame` 에서 주 출력 사각형이 버퍼에서 시작하는 위치입니다(버퍼 맨 앞은 화면 앵커입니다). */
        uint32 getMainQuadBase() const { return _mainQuadBase; }
        /** @brief 마지막 `prepareFrame` 에서 대상 @p targetIndex 의 사각형이 버퍼에서 시작하는 위치입니다. */
        uint32 getTargetQuadBase( uint32 targetIndex ) const { return targetIndex < _listTargetQuadBase.size() ? _listTargetQuadBase[targetIndex] : 0; }
        /**
         * @brief GPU 자원을 해제합니다(디바이스가 없으면 핸들만 잊습니다). 아틀라스 미러는 남깁니다. 다음 `prepareFrame` 이 다시 업로드합니다.
         * @details 경로로 빌린 이미지와 렌더 텍스처 대상은 캐시에 돌려줍니다(다음 `prepareFrame` 이 다시 빌립니다).
         */
        void release( IRHIDevice* pDevice );

        /** @brief 지금 보관 중인 아틀라스 페이지 수입니다(미러 기준). */
        uint32 getAtlasPageCount() const { return static_cast<uint32>( _listAtlasPage.size() ); }
        /** @brief 마지막 `prepareFrame` 이 받아들여진 아틀라스 업로드 제출 수입니다(전체 페이지와 구간의 합이며 진단과 테스트용입니다. `Busy` 는 세지 않습니다). */
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

        /** @brief 화면 앵커를 버퍼 맨 앞에 업로드합니다. @p bForce 가 아니면 지난번과 같을 때 업로드하지 않습니다. */
        void uploadAnchors( IRHIDevice& device, const vector<CanvasAnchor>& listAnchor, bool bForce );
        /** @brief 마커 입력 행을 만들어 올리고(`_markerRowBuffer`), 사각형 버퍼가 UAV 를 가지게 합니다. 켜진 마커가 없으면 `_markerCount` 를 0 으로 둡니다. */
        void prepareMarkers( IRHIDevice& device, const CanvasFrameData& frame );
        /** @brief 업로드 하나를 미러에 반영하고 업로드할 구간으로 적습니다. */
        void applyAtlasUpload( const GlyphAtlasUpload& upload );
        /**
         * @brief 페이지 텍스처를 갖추고(없으면 만듭니다) 바뀐 구간의 업로드를 냅니다. GPU 완료를 기다리지 않습니다.
         * @details 새 페이지의 SRV 는 첫 업로드 티켓이 `Complete` 가 된 이후 프레임에 등록합니다. `Busy` 는 다음 프레임에 다시 내고, `Rejected` · `Failed` 는 페이지를 멈춥니다.
         */
        void uploadAtlasPage( IRHIDevice& device, AtlasPage& page );
        /** @brief 페이지 텍스처가 없으면 만들고 첫 전체 업로드를 표시합니다. 만들지 못해 페이지를 멈췄으면 false 입니다. */
        bool ensureAtlasPageTexture( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 새 페이지의 첫 업로드가 끝났으면 bindless 로 등록합니다. 아직 끝나지 않았거나 페이지를 멈췄으면 false 이며 이번 프레임은 더 올리지 않습니다. */
        bool registerAtlasPage( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 페이지 전체를 올립니다(`Busy` 는 더티 상태를 두고 다음 프레임). */
        void uploadWholeAtlasPage( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 더티 구간을 합쳐 잇달아 냅니다(`Busy` 에서 멈추고 나머지는 다음 프레임). */
        void uploadAtlasRegions( IRHIResourceFactory& factory, AtlasPage& page );
        /** @brief 페이지를 멈춥니다: 오류를 남기고, `release` 전까지 그리지도 올리지도 않습니다(폴백 없이 실패를 드러냅니다). */
        void failAtlasPage( AtlasPage& page, string_view reason );
        /** @brief 경로로 빌린 이미지 하나입니다(텍스처는 `TextureCache` 가 소유하며 `release` 가 돌려줍니다). */
        struct PathTexture
        {
            hashed_string _path{};
            Texture2D*    _pTexture{ nullptr }; ///< 읽지 못했으면 nullptr(캐시가 이유를 남겼으므로 다시 묻지 않는다)
        };

        /** @brief 렌더 텍스처 대상 하나입니다. 빌린 텍스처와 마지막으로 그린 내용 번호, 텍스처를 가집니다. */
        struct TargetState
        {
            hashed_string    _path{};
            Texture2D*       _pTexture{ nullptr }; ///< 캐시가 소유하며, 돌려줄 때까지 빌려 쓴다.
            uint64           _drawnRevision{ 0 };  ///< 마지막으로 그린 내용 번호(0이면 그린 적 없음)
            RHITextureHandle _drawnTexture{ 0 };   ///< 그때의 텍스처(다시 만들어졌으면 내용이 없으므로 다시 그린다)
            uint8            _bSeen{ SW_FALSE };   ///< 이번 프레임 목록에 있다.
        };

        /**
         * @brief 이번 목록의 렌더 텍스처 대상을 빌리고, 사라진 대상은 돌려줍니다. 텍스처 생성과 bindless 등록은 기록 중에 할 수 없으므로 `prepareFrame` 이 부릅니다.
         * @details 처음 빌린 대상의 포맷에 셋업 PSO 가 없으면 여기서 오류로 거절한다(대상마다 한 번 — 다음 프레임에는 빌린 상태라 다시 묻지 않는다).
         */
        void prepareTargets( IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache );
        /** @brief 경로로 대상 상태를 찾습니다. 이번 목록과 순서가 같지 않으므로 경로로 찾습니다. 없으면 nullptr 입니다. */
        TargetState* findTarget( const hashed_string& path );
        /** @brief 일괄 텍스처 하나의 SRV 입니다. 없으면 kInvalidDescriptorIndex 입니다. */
        RHIDescriptorIndex findTextureSRV( const CanvasTextureRef& texture ) const;
        /** @brief 목록의 경로 이미지를 `TextureCache` 에서 빌립니다(처음 보는 경로만). 엔진 서비스가 없으면(테스트 하네스 밖) 아무것도 하지 않습니다. */
        void acquirePathTextures( IRHIDevice& device, const CanvasDrawList& list );

        vector<AtlasPage>       _listAtlasPage;
        vector<PathTexture>     _listPathTexture;      ///< 경로 이미지(HUD 아이콘, 조준선. 몇 개뿐이라 선형 검색)
        vector<uint8>           _regionScratchBytes;   ///< 구간 업로드용으로 행 사이의 빈틈을 없앤 사본
        vector<CanvasQuad>      _listAnchorScratch;    ///< 앵커를 사각형 원소 형태(rect = x, y, 배율, -)로 옮긴 사본
        vector<float4>          _listMarkerRowScratch; ///< 마커 입력 행의 사본입니다(`canvasmarker.hlsl` 의 kRow* 배치)
        vector<CanvasAnchor>    _listUploadedAnchor;   ///< 버퍼 앞에 지금 있는 앵커(같으면 다시 업로드하지 않는다)
        vector<CanvasQuad>      _listQuadScratch;      ///< 대상이 있을 때 주 출력과 대상 사각형을 이어 붙인 사본
        vector<uint32>          _listTargetQuadBase;   ///< 대상마다 버퍼 안 시작 위치
        vector<TargetState>     _listTarget;           ///< 렌더 텍스처 대상(월드 공간 UI)
        RHIStructuredBufferSlot _quadBuffer;
        RHIStructuredBufferSlot _markerRowBuffer;  ///< 마커 입력 행(float4)입니다. 마커가 있는 프레임마다 다시 올린다.
        uint64                  _uploadedRevision; ///< 사각형 버퍼에 지금 있는 내용의 서명(주 출력과 대상 내용 번호, 0이면 없음)
        uint32                  _uploadedQuadCount;
        uint32                  _mainQuadBase;          ///< 주 출력 사각형이 버퍼에서 시작하는 위치(앞의 앵커 수)
        uint32                  _markerCount;           ///< 이번 프레임 컴퓨트가 돌릴 마커 기록 수(0 이면 돌리지 않는다)
        bool                    _bMarkerReadsInstances; ///< 이번 프레임 마커 중 GPUScene 인스턴스 칸을 읽는 것이 있다(인스턴스 버퍼를 읽기 상태로 둔다).
        uint32                  _lastUploadCallCount;
        uint32                  _lastDrawnTargetCount;
    };
} // namespace sw
