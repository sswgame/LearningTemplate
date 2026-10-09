/**
 * @file RenderViewScheduler.h
 * @brief 추가 뷰(CCTV · 백미러 · PiP) 중 이번 프레임에 그릴 것을 고릅니다 — 갱신 주기 · 보이는가 · 프레임 예산(게임 스레드).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @class RenderViewScheduler
     * @brief 뷰마다 마지막으로 그린 시각을 들고, 후보 목록에서 이번 프레임에 그릴 것을 표시합니다. 렌더러를 모르는 순수 계산이라 씬 없이 시험합니다.
     * @details 규칙 — 보이지 않는 뷰는 그리지 않는다. 갱신 주기가 0 이면 매 프레임, 아니면 지난 그림에서 1/주기 가 지나야 "때가 된" 것이다(처음 보는 뷰는 늘
     *          때가 됐다). 때가 된 뷰가 예산보다 많으면 가장 오래 기다린(늦은 정도가 큰) 것부터 고르고, 고르지 못한 뷰는 다음 프레임에 더 늦은 채로 다시 겨룬다
     *          (굶지 않는다 — 늦은 정도가 같으면 덜 그린 뷰가 먼저다). 그린 뷰만 시각을 새로 적는다. 이번 후보에 없는 뷰는 잊는다(카메라가 사라졌다).
     *          언리얼 SceneCapture 의 bCaptureEveryFrame/CaptureRate · 유니티 카메라를 손으로 Render() 하는 갱신 주기와 같은 자리입니다.
     */
    class SW_API RenderViewScheduler
    {
    public:
        /** @brief 뷰 후보 하나입니다. */
        struct Candidate
        {
            uint64  _viewId{ 0 };
            float32 _updateRate{ 0.0f }; ///< 초당 그리기(0 = 매 프레임)
            uint8   _bVisible{ SW_TRUE };
        };

        RenderViewScheduler();

        /**
         * @brief 시각 @p now(초)에서 후보마다 이번 프레임에 그릴지를 @p pOutRender 에 적습니다(SW_TRUE / SW_FALSE).
         * @param budget 한 프레임에 그릴 수 있는 뷰의 최대 수(0 이면 제한 없음).
         * @return 그리기로 고른 수입니다.
         */
        uint32 schedule( float64 now, const Candidate* pCandidate, uint32 candidateCount, uint32 budget, uint8* pOutRender );
        /** @brief 모든 기록을 잊습니다. */
        void clear() { _listEntry.clear(); }
        /** @brief 기억하는 뷰 수입니다(진단 · 시험). */
        uint32 getTrackedCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        struct Entry
        {
            uint64  _viewId{ 0 };
            float64 _lastRenderTime{ -1.0 }; ///< 음수면 아직 그린 적이 없다
            uint64  _renderCount{ 0 };       ///< 그린 횟수 — 늦은 정도가 같으면 덜 그린 뷰가 먼저다(번호 순으로 고르면 앞 뷰만 이긴다)
            uint8   _bSeen{ SW_FALSE };
        };
        /** @brief 뷰의 기록입니다. 없으면 새로 만들어 "한 번도 안 그림" 으로 둡니다. */
        Entry& findOrAddEntry( uint64 viewId );

        vector<Entry>   _listEntry;
        vector<uint32>  _listScratchDue;         ///< 때가 된 후보 번호
        vector<float64> _listScratchOverdue;     ///< 그 후보의 늦은 정도(처음 보는 뷰는 아주 크다)
        vector<uint64>  _listScratchRenderCount; ///< 그 후보가 지금까지 그려진 횟수
        vector<uint32>  _listScratchOrder;       ///< 예산이 모자랄 때의 정렬 순서
    };
} // namespace sw
