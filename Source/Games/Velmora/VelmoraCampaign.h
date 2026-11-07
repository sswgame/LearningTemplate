/**
 * @file VelmoraCampaign.h
 * @brief 벨모라 한 판의 진행 규칙입니다 — 지역 진입 · 봉화 · 야영지 휴식 · 전투 클리어를 판 상태(퀘스트 일지 · 깃발 · 지갑)와 플레이어의 몸(`AdventureVitals`)에 적용합니다.
 * @details 엔진 오브젝트를 모르는 보통 클래스다(단위 테스트가 그대로 돌린다). 입력 · 자리 · 전투 룸은 디렉터(`VelmoraGameModeComponent`)가 가진다.
 *          무엇을 했는지는 판 상태의 깃발에 이름으로 남긴다(`velmora.visited.<지역>` · `velmora.beacon.<지역>` · `velmora.cleared.<지역>`) — 확장 팩 지역이 빠진 판을 불러도
 *          모르는 이름은 그대로 남고 다시 켜면 이어진다(세이브 승계). 퀘스트에는 일어난 일만 알린다(`Enter` 지역 · `Light` 봉화 · `Clear` 지역 · `Defeat` 보스 종).
 *          보상은 퀘스트 단계 보상의 값 이름이 정한다 — `gold`(지갑의 기본 통화) · `heart`(하트 그릇). 모르는 이름은 경고한다(데이터 오타가 보상을 조용히 지우지 않게).
 *          장르 이름을 딴 키트로 빼지 않았다: `docs/DesignBacklog.md` "오픈월드 액션 RPG 키트" 1 — 두 번째 사용처에서 같은 계약이 확인될 때 나눈다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"

namespace sw
{
    struct QuestEvent;
    struct VelmoraRegionDef;

    class GameFlags;
    class QuestLog;
    class Wallet;

    /**
     * @class VelmoraCampaign
     * @brief 한 판의 진행 규칙과 플레이어의 몸 · 일어설 지역입니다.
     */
    class VelmoraCampaign
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "VLCP" );
        static constexpr uint32 kStateVersion = 1;

        static constexpr const utf8* kRewardGold  = "gold";  ///< 퀘스트 보상 값 이름 — 지갑의 기본 통화
        static constexpr const utf8* kRewardHeart = "heart"; ///< 퀘스트 보상 값 이름 — 하트 그릇 수

        VelmoraCampaign();

        /** @brief 몸을 설정으로 새로 세우고 일어설 지역을 @p startRegion 으로 둡니다. 설정이 범위 밖이면 오류를 남기고 false 입니다(`AdventureVitals::initialize`). */
        [[nodiscard]] bool initialize( const AdventureVitalsSettings& settings, const hashed_string& startRegion );

        /**
         * @brief 지역에 들어섰습니다. 퀘스트에 `Enter` 를 알리고 지역의 받을 퀘스트를 받아 봅니다(조건이 안 맞으면 다음 진입에 다시 본다).
         * @return 처음 들어선 지역이면 true 입니다(방문 깃발을 세운다).
         */
        bool enterRegion( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const;
        /** @brief 지역의 봉화를 켭니다. 봉화가 없거나 이미 켰으면 false 이고, 켜면 퀘스트에 `Light` 를 알립니다. */
        bool lightBeacon( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const;
        /** @brief 지역의 야영지에서 쉽니다. 체력을 채우고 일어설 지역을 이 지역으로 둡니다. 야영지가 없으면 false 입니다. */
        [[nodiscard]] bool restAtCamp( const VelmoraRegionDef& region );
        /** @brief 지역의 전투를 깼습니다. 클리어 깃발을 세우고 퀘스트에 `Clear` 지역과 보스 종마다 `Defeat` 를 알립니다. */
        void clearEncounter( const VelmoraRegionDef& region, QuestLog& quests, GameFlags& flags ) const;

        /** @brief 4분의 1 하트 단위 피해입니다. 이번에 쓰러졌으면 true 입니다. */
        [[nodiscard]] bool takeDamage( int32 quarters ) { return _vitals.applyDamage( quarters ); }
        /** @brief 쓰러진 뒤 일어섭니다(체력을 채운다). 일어설 자리는 `getRespawnRegion` 지역의 일어설 자리다. */
        [[nodiscard]] bool respawn() { return _vitals.revive(); }
        /** @brief 쌓인 체력 알림을 @p outListEvent 뒤에 붙이고 비웁니다(프레임마다 — 부르지 않으면 끝없이 쌓인다). */
        void pollHealthEvents( vector<HealthEvent>& outListEvent ) { _vitals.pollHealthEvents( outListEvent ); }
        /**
         * @brief 퀘스트 일지 알림을 보상으로 바꿉니다 — 단계에 들어설 때의 보상 값(`gold` · `heart`)을 지갑 · 몸에 준다.
         * @return 이번 알림에서 끝난 퀘스트 수입니다.
         */
        int32 applyQuestEvents( const vector<QuestEvent>& listEvent, Wallet& wallet );

        static bool isBeaconLit( const VelmoraRegionDef& region, const GameFlags& flags );
        static bool isEncounterCleared( const VelmoraRegionDef& region, const GameFlags& flags );
        static bool isVisited( const VelmoraRegionDef& region, const GameFlags& flags );

        const AdventureVitals& getVitals() const { return _vitals; }
        const hashed_string&   getRespawnRegion() const { return _respawnRegion; }
        /** @brief 일어설 지역을 바꿉니다(불러온 판에서 그 지역이 표에 없을 때 디렉터가 시작 지역으로 되돌린다). */
        void setRespawnRegion( const hashed_string& region ) { _respawnRegion = region; }

        /** @brief 몸과 일어설 지역을 씁니다. 몸의 설정은 `initialize` 에서 받은 것이라 싣지 않는다. */
        SW_DECLARE_STATE_IO();

    private:
        AdventureVitals _vitals;
        hashed_string   _respawnRegion;
    };
} // namespace sw
