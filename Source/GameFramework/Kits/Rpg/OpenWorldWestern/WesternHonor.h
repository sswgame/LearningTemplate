/**
 * @file WesternHonor.h
 * @brief 명예 — 기반 `ReputationState` 의 세력 하나("honor")로 값 · 단계를 두고, 행동 · 범죄마다 증감, 단계에 따른 상점 할인과 대사 플래그를 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Progression/Reputation.h"

namespace sw
{
    struct WesternHonorTierDef;

    class GameFlags;
    class WesternCatalog;

    /**
     * @class WesternHonor
     * @brief 플레이어 한 명의 명예입니다. 단계가 바뀌면 `ReputationEvent` 가 나옵니다(기반 평판 알림 그대로).
     */
    class SW_GF_API WesternHonor
    {
    public:
        WesternHonor();

        void initialize( const WesternCatalog* pCatalog );
        /** @brief 이름 붙은 행동(낯선 이 돕기 · 낙인 · 강도)을 반영합니다. 실제로 바뀐 값입니다. 모르는 행동은 0 입니다. */
        int32 applyAction( const hashed_string& actionId );
        /** @brief 범죄의 명예 변화를 반영합니다(목격 여부와 상관없이 — 양심의 문제). */
        int32 applyCrime( const hashed_string& crimeId );
        int32 changeValue( int32 delta );

        int32         getValue() const;
        hashed_string getTierName() const;
        /** @brief 지금 단계의 효과입니다. 단계가 없으면 nullptr 입니다. */
        const WesternHonorTierDef* findTier() const;
        /** @brief 상점 값에 곱할 배율입니다(1 − 할인). */
        float32 computePriceScale() const;
        /**
         * @brief 단계마다의 대사 플래그를 @p inoutFlags 에 둡니다 — 지금 단계의 것은 1, 다른 단계의 것은 지웁니다(대화 조건식이 읽는다).
         */
        void applyDialogueFlags( GameFlags& inoutFlags ) const;
        void drainEvents( vector<ReputationEvent>& outListEvent ) { _reputation.drainEvents( outListEvent ); }

    private:
        ReputationState       _reputation;
        const WesternCatalog* _pCatalog;
    };
} // namespace sw
