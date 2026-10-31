/**
 * @file CanvasRenderer.h
 * @brief 캔버스 그리기 목록을 GPU 로 그립니다(렌더 스레드). 준비 순서를 정하고 일괄마다 인스턴스 드로우와 렌더 텍스처 대상(월드 공간 UI) 그리기를 기록합니다.
 * @details 언리얼 `FSlateRHIRenderer` 에 해당합니다(게임 스레드가 만든 그리기 목록만 읽습니다). `FrameRenderer` 가 소유합니다.
 *          자원은 협력 객체가 나눠 듭니다: 글리프 아틀라스 미러(`CanvasGlyphAtlasMirror`), 캐시에서 빌린 텍스처(`CanvasTextureLeases`),
 *          사각형 구조 버퍼(`CanvasQuadBuffer`), 화면 마커 투영(`CanvasMarkerProjection`). 이 클래스는 그 순서와 드로우 기록만 갖습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Canvas/CanvasGlyphAtlasMirror.h"
#include "Engine/Renderer/Canvas/CanvasMarkerProjection.h"
#include "Engine/Renderer/Canvas/CanvasQuadBuffer.h"
#include "Engine/Renderer/Canvas/CanvasTextureLeases.h"

namespace sw
{
    struct CanvasDrawList;
    struct CanvasFrameData;
    struct CanvasTextureRef;

    class GPUAssertMonitor;
    class IRHICommandList;
    class IRHIDevice;
    class RenderPSOCache;

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
         *          대상 포맷의 캔버스 PSO 가 셋업(@p psoCache, `FramePSOManager::collectOutputPSOJobs`)에 없으면 그 대상은 처음 빌릴 때 오류로 거절하고 그리지 않습니다.
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
         * @brief 화면 마커 투영 컴퓨트(`canvas_marker.hlsl`)를 기록합니다. 마커 입력(`CanvasFrameData::_listMarker`)이 있는 프레임에만 돌고, 렌더 패스 밖(컴퓨트 프리패스)에서 부릅니다 —
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
        uint32 getMainQuadBase() const { return _quadBuffer.getMainQuadBase(); }
        /** @brief 마지막 `prepareFrame` 에서 대상 @p targetIndex 의 사각형이 버퍼에서 시작하는 위치입니다. */
        uint32 getTargetQuadBase( uint32 targetIndex ) const { return _quadBuffer.getTargetQuadBase( targetIndex ); }
        /**
         * @brief GPU 자원을 해제합니다(디바이스가 없으면 핸들만 잊습니다). 아틀라스 미러는 남깁니다. 다음 `prepareFrame` 이 다시 업로드합니다.
         * @details 경로로 빌린 이미지와 렌더 텍스처 대상은 캐시에 돌려줍니다(다음 `prepareFrame` 이 다시 빌립니다).
         */
        void release( IRHIDevice* pDevice );

        /** @brief 글리프 아틀라스 미러입니다(페이지 수 · 업로드 호출 수 진단). */
        const CanvasGlyphAtlasMirror& getAtlas() const { return _atlas; }

    private:
        /** @brief 일괄 텍스처 하나의 SRV 입니다(직접 텍스처 · 경로 이미지 · 아틀라스 페이지 순). 없으면 kInvalidDescriptorIndex 입니다. */
        RHIDescriptorIndex findTextureSRV( const CanvasTextureRef& texture ) const;

        CanvasGlyphAtlasMirror _atlas;      ///< 글리프 아틀라스 페이지의 CPU 미러와 GPU 텍스처
        CanvasTextureLeases    _textures;   ///< `TextureCache` 에서 빌린 경로 이미지와 렌더 텍스처 대상
        CanvasQuadBuffer       _quadBuffer; ///< 앵커 · 주 출력 · 대상 사각형을 이은 구조 버퍼
        CanvasMarkerProjection _markers;    ///< 화면 마커 투영 컴퓨트의 입력과 디스패치
        uint32                 _lastDrawnTargetCount;
    };
} // namespace sw
