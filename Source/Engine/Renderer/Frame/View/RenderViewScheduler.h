/**
 * @file RenderViewScheduler.h
 * @brief 추가 뷰(CCTV, 백미러, PiP) 중 이번 프레임에 그릴 것을 고릅니다. 갱신 주기, 보이는지 여부, 프레임 예산을 따집니다(게임 스레드).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @struct RenderViewBudget
     * @brief 한 프레임에 그리는 추가 뷰의 예산입니다. 뷰 수(뷰마다 드는 컬링 · 패스 기록 비용)와 그리는 픽셀 수(채우기 비용) 두 축이고, 0 이면 그 축은 제한이 없습니다.
     * @details 시간 예산이 아니라 결정적인 비용 모형입니다. GPU 타임스탬프는 프로파일러를 켠 빌드에만 있어 Shipping 의 예산이 될 수 없고, 같은 입력에 같은 뷰를
     *          골라야 테스트와 리플레이가 맞습니다. 예산은 렌더 텍스처 뷰(CCTV, 백미러)만 받습니다 — 화면 사각형 뷰(분할 화면, PiP)는 주 화면의 일부라
     *          미루면 그 칸이 멈춰 보입니다(그 뷰의 빈도는 갱신 주기로 정합니다).
     */
    struct RenderViewBudget
    {
        uint32 _maxViewCount{ 0 };  ///< 한 프레임에 그리는 뷰의 최대 수
        uint32 _maxPixelCount{ 0 }; ///< 한 프레임에 그리는 뷰들의 내부 해상도 픽셀 합의 최대
    };
} // namespace sw

namespace sw
{
    /**
     * @class RenderViewScheduler
     * @brief 뷰마다 마지막으로 그린 시각을 보관하고, 후보 목록에서 이번 프레임에 그릴 것을 표시합니다. 렌더러를 모르는 순수 계산이라 씬 없이 테스트합니다.
     * @details 규칙은 다음과 같습니다. 보이지 않는 뷰는 그리지 않습니다. 갱신 주기가 0 이면 매 프레임, 아니면 지난 이미지에서 1/주기 가 지나야 "때가 된" 것입니다(처음 보는 뷰는 항상
     *          때가 됐습니다). 예산을 받는 뷰 중 때가 된 것은 가장 오래 기다린(늦은 정도가 큰) 것부터 예산(`RenderViewBudget`)이 찰 때까지 고릅니다.
     *          가장 늦은 뷰 하나는 픽셀 예산보다 커도 그리고, 다음 뷰가 예산을 넘으면 거기서 멈춥니다 — 작은 뷰가 큰 뷰를 건너뛰어 채우면 큰 뷰가 굶습니다.
     *          고르지 못한 뷰는 다음 프레임에 더 늦은 채로 다시 겨룹니다(늦은 정도가 같으면 덜 그린 뷰가 먼저입니다). 예산을 받지 않는 뷰는 때가 되면 그립니다.
     *          그린 뷰만 시각을 새로 적습니다. 이번 후보에 없는 뷰는 잊습니다(카메라가 사라졌습니다).
     *          언리얼 SceneCapture 의 bCaptureEveryFrame/CaptureRate, 유니티에서 카메라를 손으로 Render() 하는 갱신 주기, HDRP 반사 프로브의 타임 슬라이싱에 해당합니다.
     */
    class SW_API RenderViewScheduler
    {
    public:
        /** @brief 뷰 후보 하나입니다. */
        struct Candidate
        {
            uint64  _viewID{ 0 };
            float32 _updateRate{ 0.0f }; ///< 초당 그리기(0 = 매 프레임)
            uint32  _pixelCount{ 0 };    ///< 내부 해상도 픽셀 수(픽셀 예산이 센다)
            uint8   _bVisible{ SW_TRUE };
            uint8   _bBudgeted{ SW_TRUE }; ///< 예산을 받는다(렌더 텍스처 뷰). 화면 사각형 뷰는 SW_FALSE
        };

        RenderViewScheduler();

        /**
         * @brief 시각 @p now(초)에서 후보마다 이번 프레임에 그릴지를 @p pOutRender 에 적습니다(SW_TRUE / SW_FALSE).
         * @param budget 예산을 받는 뷰에 거는 한 프레임의 예산입니다.
         * @return 그리기로 고른 수입니다(예산을 받지 않는 뷰 포함).
         */
        uint32 schedule( float64 now, const Candidate* pCandidate, uint32 candidateCount, const RenderViewBudget& budget, uint8* pOutRender );
        /** @brief 모든 기록을 잊습니다. */
        void clear() { _listEntry.clear(); }
        /** @brief 기억하는 뷰 수입니다(진단과 테스트용). */
        uint32 getTrackedCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        struct Entry
        {
            uint64  _viewID{ 0 };
            float64 _lastRenderTime{ -1.0 }; ///< 음수면 아직 그린 적이 없다
            uint64  _renderCount{ 0 };       ///< 그린 횟수. 늦은 정도가 같으면 덜 그린 뷰가 먼저다(번호 순으로 고르면 앞 뷰만 이긴다)
            uint8   _bSeen{ SW_FALSE };
        };
        /** @brief 뷰의 기록입니다. 없으면 새로 만들어 "한 번도 안 그림"으로 둡니다. */
        Entry& getOrCreateEntry( uint64 viewID );

        vector<Entry>   _listEntry;
        vector<uint32>  _listScratchDue;         ///< 때가 된 후보 번호
        vector<float64> _listScratchOverdue;     ///< 그 후보의 늦은 정도(처음 보는 뷰는 아주 크다)
        vector<uint64>  _listScratchRenderCount; ///< 그 후보가 지금까지 그려진 횟수
        vector<uint32>  _listScratchOrder;       ///< 예산을 채우는 순서(늦은 정도가 큰 것부터)
    };
} // namespace sw
