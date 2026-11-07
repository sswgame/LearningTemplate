/**
 * @file RTSVision.h
 * @brief RTS 한 판의 팀 시야(전장의 안개)입니다. 팀마다 셀의 안개 상태를 들고, 간격마다 다시 칠합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Utility/Grid/GridTopology.h"

namespace sw
{
    class Archive;

    /** @brief 안개의 상태입니다. */
    enum class RTSVisibility : uint8
    {
        Unexplored = 0,
        Explored, ///< 본 적이 있다(지형과 마지막으로 본 건물)
        Visible
    };

    /**
     * @class RTSVision
     * @brief 팀마다 셀의 `RTSVisibility` 를 담습니다. 칠하기는 `beginRepaint`(보이던 셀을 본 적 있음으로) 뒤 시야원마다 `reveal` 입니다.
     * @details 무엇이 시야를 내는지(유닛 · 건물 · 주인)는 월드가 정한다 — 이 클래스는 셀 상태와 다시 칠할 때(`_repaintTimer`)만 든다.
     *          `RTSWorld` 에서 뗐다(시야 상태 묶음이 유닛 · 명령과 따로 저장 · 복원된다). 팀 수는 플레이어를 더할 때 늘고 줄지 않는다.
     */
    class SW_KIT_API RTSVision
    {
    public:
        /** @brief 격자 크기로 비웁니다. 팀은 없습니다. */
        void reset( int32 width, int32 height );
        /** @brief 팀 수를 적어도 @p teamCount 로 늘립니다. 새 팀은 모두 `Unexplored` 입니다. */
        void  ensureTeamCount( int32 teamCount );
        int32 getTeamCount() const { return static_cast<int32>( _listTeamCell.size() ); }

        /** @brief 시간을 흘리고, 다시 칠할 때가 되었으면 다음 간격 @p interval 을 걸고 true 를 돌려줍니다. */
        [[nodiscard]] bool updateRepaintTimer( float32 deltaTime, float32 interval );
        /** @brief 모든 팀의 `Visible` 셀을 `Explored` 로 내립니다. 이어서 `reveal` 로 지금 시야를 칠합니다. */
        void beginRepaint();
        /** @brief 팀 @p team 에게 @p center 셀 둘레 반경 @p sight(셀) 원을 `Visible` 로 칠합니다. 격자 밖은 자릅니다. */
        void reveal( int32 team, const int2& center, float32 sight );

        /** @brief 셀의 상태입니다. 팀이나 셀이 범위 밖이면 `Unexplored` 입니다. */
        RTSVisibility get( int32 team, const int2& cell ) const;

        /**
         * @brief 팀마다의 셀 상태와 다시 칠하기 타이머를 씁니다.
         *        `writeState` 가 쓴 바이트를 읽습니다. 셀 수가 지금 격자와 다르거나, 셀 값이 범위 밖이거나 데이터가 깨졌으면 false 이고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        vector<vector<uint8>> _listTeamCell; ///< 팀마다 셀의 RTSVisibility
        GridTopology          _topology{};
        Countdown             _repaintTimer{};
    };
} // namespace sw
