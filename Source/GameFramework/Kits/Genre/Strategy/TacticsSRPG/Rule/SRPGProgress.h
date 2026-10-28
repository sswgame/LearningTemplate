/**
 * @file SRPGProgress.h
 * @brief 택틱스 SRPG 의 진행을 담당합니다. 작전의 승패 조건(적 전멸, 지휘관 격파, 지점 도달, 턴 버티기, 턴 제한, 아군 지휘관), 로그라이트 작전 지도(기반 `RunMap`), 명단(레벨과 개발이 이어진다)을 다룹니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/RunMap.h"

namespace sw
{
    class Archive;
    class SRPGBattlefield;
    class SRPGCatalog;

    /** @brief 승리 조건입니다. */
    enum class SRPGObjective : uint8
    {
        DefeatAll = 0,   ///< 적 팀 전멸이다(제3세력은 세지 않는다)
        DefeatCommander, ///< 적 지휘관을 격파한다
        ReachCell,       ///< 아군이 목표 셀 중 하나에 선다
        SurviveTurns     ///< 턴 제한까지 버틴다
    };

    /** @brief 작전의 결과입니다. */
    enum class SRPGOutcome : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat
    };

    /** @brief 작전 규칙입니다. */
    struct SRPGMissionRule
    {
        vector<int2>  _listGoalCell{};
        int32         _turnLimit{ 0 }; ///< 0 이면 제한이 없다. 버티기이면 이 턴을 넘기면 승리하고, 그 밖의 조건이면 이 턴을 넘기면 패배한다
        SRPGObjective _objective{ SRPGObjective::DefeatAll };
        uint8         _bLoseOnCommander{ SW_TRUE }; ///< 아군 지휘관이 격파되면 패배한다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SRPGMission
     * @brief 승패를 판정합니다. 패배를 먼저 봅니다. 아군 전멸, 아군 지휘관 격파, 턴 제한 초과는 같은 순간의 승리보다 앞섭니다.
     */
    struct SW_KIT_API SRPGMission
    {
        static SRPGOutcome evaluate( const SRPGBattlefield& field, const SRPGMissionRule& rule );
    };
} // namespace sw

namespace sw
{
    /** @brief 캠페인 명단의 한 줄입니다. 작전이 끝나면 레벨과 개발 결과가 여기로 돌아옵니다. */
    struct SRPGRosterEntry
    {
        LevelProgress _pilotLevel{};
        LevelProgress _unitLevel{};
        hashed_string _unitID{};
        hashed_string _pilotID{};
        uint8         _bLost{ SW_FALSE }; ///< 격파되어 이번 판에서 빠졌다(로그라이트)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SRPGCampaign
     * @brief 메탈슬러그 택틱스식 로그라이트 캠페인입니다. 기반 `RunMap` 의 작전 지도에서 노드를 고르고, 명단을 전장에 내보내고, 결과를 받아 다음 노드로 갑니다.
     * @details 작전 시드는 판 시드와 노드 번호의 해시이다. 같은 판의 같은 노드이면 같은 전장이 나온다. 패배하면 판이 끝난다.
     */
    class SW_KIT_API SRPGCampaign
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "SRCP" );
        static constexpr uint32 kStateVersion = 1;

        SRPGCampaign();

        /** @brief 캠페인을 새로 엽니다. 지도 설정이 범위 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다. */
        [[nodiscard]] bool initialize( const RunMapSettings& settings, uint32 seed );
        /** @brief 명단에 추가합니다. 모르는 기체나 파일럿이면 -1 을 반환합니다. */
        int32 addRosterEntry( const SRPGCatalog& catalog, const hashed_string& unitID, const hashed_string& pilotID, int32 pilotLevel = 1 );

        void collectChoices( vector<int32>& outListNode ) const { _runMap.collectChoices( outListNode ); }
        /** @brief 지도의 노드로 가서 작전을 엽니다. 갈 수 없거나 판이 끝났으면 false 를 반환합니다. */
        [[nodiscard]] bool beginMission( int32 nodeIndex );
        /** @brief 남은 명단을 @p listCell 순서대로 아군으로 배치하고, 배치한 수를 반환합니다. */
        int32 deployRoster( SRPGBattlefield& field, const vector<int2>& listCell ) const;
        /** @brief 작전 결과를 받습니다. 레벨과 개발 결과를 명단으로 되돌리고, 격파된 유닛은 잃으며, 패배하면 판이 끝납니다. */
        void completeMission( const SRPGBattlefield& field, SRPGOutcome outcome );

        hashed_string                  getMissionKind() const;
        uint32                         getMissionSeed() const;
        const RunMap&                  getRunMap() const { return _runMap; }
        const vector<SRPGRosterEntry>& getRoster() const { return _listRoster; }
        bool                           isInMission() const { return _bInMission == SW_TRUE; }
        bool                           isFailed() const { return _bFailed == SW_TRUE; }
        /** @brief 마지막 노드(보스)의 작전을 이겼는지 반환합니다. */
        bool isCleared() const { return _bFailed == SW_FALSE && _bInMission == SW_FALSE && _runMap.isFinished(); }

        /** @brief 지도(`RunMap`), 명단(레벨 둘, 기체와 파일럿 id, 잃음 여부), 시드, 임무 중 여부, 실패 여부를 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        RunMap                  _runMap;
        vector<SRPGRosterEntry> _listRoster;
        uint32                  _seed;
        uint8                   _bInMission;
        uint8                   _bFailed;
    };
} // namespace sw
