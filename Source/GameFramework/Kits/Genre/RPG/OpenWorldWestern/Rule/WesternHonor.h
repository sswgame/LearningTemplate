/**
 * @file WesternHonor.h
 * @brief 명예를 다룹니다. 빌려 온 기반 `ReputationState` 의 세력 하나("western.honor")로 값과 단계를 관리하고, 행동과 범죄마다 값을 올리거나 내리며, 단계에 따라 상점 할인과 대사 플래그를 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Progression/Reputation.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct SessionStateRefs;
    struct WesternHonorTierDef;

    class GameFlags;
    class ReputationState;
    class WesternCatalog;

    /**
     * @class WesternHonor
     * @brief 플레이어 한 명의 명예입니다. 평판은 빌려 씁니다(`SessionStateRefs::_pReputation`). 그 평판의 카탈로그에 `WesternCatalog::getHonorReputation()` 의 세력이 있어야 단계가 정해집니다.
     *        단계가 바뀌면 빌려 온 평판에 `ReputationEvent` 가 쌓입니다. 이벤트를 꺼내는 것은 게임 화면이며, 키트는 꺼내지 않습니다.
     */
    class SW_KIT_API WesternHonor
    {
    public:
        WesternHonor();

        void initialize( const WesternCatalog* pCatalog, const SessionStateRefs& refs );
        /** @brief 이름이 붙은 행동(낯선 이 돕기, 낙인, 강도)을 반영하고, 실제로 바뀐 값을 반환합니다. 모르는 행동이면 0 을 반환합니다. */
        int32 applyAction( const hashed_string& actionID );
        /** @brief 범죄의 명예 변화를 반영합니다(목격 여부와 상관없이 적용합니다. 양심의 문제이기 때문입니다). */
        int32 applyCrime( const hashed_string& crimeID );
        int32 changeValue( int32 delta );

        int32         getValue() const;
        hashed_string getTierName() const;
        /** @brief 지금 단계의 효과를 반환합니다. 단계가 없으면 nullptr 을 반환합니다. */
        const WesternHonorTierDef* findTier() const;
        /** @brief 상점 값에 곱할 배율을 반환합니다(1 − 할인). */
        float32 computePriceScale() const;
        /**
         * @brief 단계마다의 대사 플래그를 @p inoutFlags 에 둡니다. 지금 단계의 플래그는 1로 켜고 다른 단계의 플래그는 지웁니다(대화 조건식이 읽습니다).
         */
        void applyDialogueFlags( GameFlags& inoutFlags ) const;

    private:
        ReputationState*      _pReputation; ///< 빌려 온 평판
        const WesternCatalog* _pCatalog;
    };
} // namespace sw
