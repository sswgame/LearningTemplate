/**
 * @file SrpgProgress.h
 * @brief 택틱스 SRPG 의 진행 — 작전 승패 조건(적 전멸 · 지휘관 격파 · 지점 도달 · 턴 버티기 · 턴 제한 · 아군 지휘관), 로그라이트 작전 지도(기반 `RunMap`)와 명단(레벨 · 개발이 이어진다)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Progression/RunMap.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class SrpgBattlefield;
    class SrpgCatalog;

    /** @brief 승리 조건입니다. */
    enum class SrpgObjective : uint8
    {
        DefeatAll = 0,   ///< 적 팀 전멸(제3세력은 세지 않는다)
        DefeatCommander, ///< 적 지휘관 격파
        ReachCell,       ///< 아군이 목표 칸 중 하나에 선다
        SurviveTurns     ///< 턴 제한까지 버틴다
    };

    /** @brief 작전 결과입니다. */
    enum class SrpgOutcome : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat
    };

    /** @brief 작전 규칙입니다. */
    struct SrpgMissionRule
    {
        vector<int2>  _listGoalCell{};
        int32         _turnLimit{ 0 }; ///< 0 이면 없음. 버티기면 이 턴을 넘기면 승리, 아니면 이 턴을 넘기면 패배
        SrpgObjective _objective{ SrpgObjective::DefeatAll };
        uint8         _bLoseOnCommander{ SW_TRUE }; ///< 아군 지휘관이 격파되면 패배
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SrpgMission
     * @brief 패배를 먼저 봅니다 — 아군 전멸 · 아군 지휘관 격파 · 턴 제한 초과가 같은 순간의 승리보다 앞섭니다.
     */
    struct SW_GF_API SrpgMission
    {
        static SrpgOutcome evaluate( const SrpgBattlefield& field, const SrpgMissionRule& rule );
    };
} // namespace sw

namespace sw
{
    /** @brief 캠페인 명단의 한 줄 — 작전이 끝나면 레벨 · 개발이 여기로 돌아옵니다. */
    struct SrpgRosterEntry
    {
        LevelProgress _pilotLevel{};
        LevelProgress _unitLevel{};
        hashed_string _unitId{};
        hashed_string _pilotId{};
        uint8         _bLost{ SW_FALSE }; ///< 격파되어 이번 판에서 빠졌다(로그라이트)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SrpgCampaign
     * @brief 메탈슬러그 택틱스식 로그라이트 — 기반 `RunMap` 의 작전 지도에서 칸을 고르고, 명단을 전장에 내보내고, 결과를 받아 다음 칸으로 갑니다.
     * @details 작전 씨앗은 판 씨앗과 칸 번호의 해시라 같은 판 · 같은 칸이면 같은 전장입니다. 패배하면 판이 끝납니다.
     */
    class SW_GF_API SrpgCampaign
    {
    public:
        static constexpr uint32 kStateTag     = 0x50435253u; ///< 'SRCP'
        static constexpr uint32 kStateVersion = 1;

        SrpgCampaign();

        void initialize( const RunMapSettings& settings, uint32 seed );
        /** @brief 명단에 더합니다. 모르는 기체 · 파일럿이면 −1 입니다. */
        int32 addRosterEntry( const SrpgCatalog& catalog, const hashed_string& unitId, const hashed_string& pilotId, int32 pilotLevel = 1 );

        void collectChoices( vector<int32>& outListNode ) const { _runMap.collectChoices( outListNode ); }
        /** @brief 지도의 칸으로 가서 작전을 엽니다. 갈 수 없거나 판이 끝났으면 false 입니다. */
        [[nodiscard]] bool beginMission( int32 nodeIndex );
        /** @brief 남은 명단을 @p listCell 순서대로 아군으로 놓습니다. 놓은 수입니다. */
        int32 deployRoster( SrpgBattlefield& field, const vector<int2>& listCell ) const;
        /** @brief 작전 결과를 받습니다 — 레벨 · 개발을 명단으로 되돌리고, 격파된 유닛은 잃고, 패배면 판이 끝납니다. */
        void completeMission( const SrpgBattlefield& field, SrpgOutcome outcome );

        hashed_string                  getMissionKind() const;
        uint32                         getMissionSeed() const;
        const RunMap&                  getRunMap() const { return _runMap; }
        const vector<SrpgRosterEntry>& getRoster() const { return _listRoster; }
        bool                           isInMission() const { return _bInMission == SW_TRUE; }
        bool                           isFailed() const { return _bFailed == SW_TRUE; }
        /** @brief 끝 칸(보스)의 작전을 이겼는가입니다. */
        bool isCleared() const { return _bFailed == SW_FALSE && _bInMission == SW_FALSE && _runMap.isFinished(); }

        /** @brief 지도(`RunMap`) · 명단(레벨 둘 · 기체 · 파일럿 id · 잃음) · 씨앗 · 임무 중 · 실패를 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        RunMap                  _runMap;
        vector<SrpgRosterEntry> _listRoster;
        uint32                  _seed;
        uint8                   _bInMission;
        uint8                   _bFailed;
    };
} // namespace sw
