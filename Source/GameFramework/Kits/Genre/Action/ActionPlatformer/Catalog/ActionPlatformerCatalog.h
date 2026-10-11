/**
 * @file ActionPlatformerCatalog.h
 * @brief 스테이지형 액션 플랫포머의 데이터입니다. 스테이지(체크포인트, 비밀 수집품, 목숨, 기준 시간), 등급 규칙(시간, 피격, 수집), 이동 모드 수치(활공, 갈고리, 드릴),
 *        근접 콤보(기반 `MoveCatalog` 기술 id 의 연결), 패리 반사, 적 패턴(간단한 상태 기계)을 담습니다.
 * @details 검브렐라, 부시덴, 리플레이스드, 페퍼 그라인더 같은 게임이 같은 테이블을 쓴다. 기술의 프레임 데이터는 기반 `MoveCatalog` XML 이 따로 가진다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 스테이지 하나입니다. */
    struct ActionStageDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listCheckpoint{};  ///< 지나가는 순서(앞쪽 체크포인트로 되돌아가지 않는다)
        vector<hashed_string> _listSecret{};      ///< 숨은 수집품
        float32               _parTime{ 60.0f };  ///< 이 시간(초) 안에 깨면 시간 점수가 만점이다
        int32                 _lives{ 3 };        ///< 처음 목숨(0이 되면 게임 오버이고, 스테이지를 처음부터 다시 시작한다)
        int32                 _hitTolerance{ 5 }; ///< 이만큼 맞으면 피격 점수가 0이다
    };
} // namespace sw

namespace sw
{
    /** @brief 등급 하나입니다. 점수(0..100)가 이 값 이상이면 이 등급입니다. */
    struct ActionGradeDef
    {
        hashed_string _grade{};
        float32       _minScore{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 등급 규칙입니다. 점수는 시간, 피격, 수집 점수의 가중 평균에 100을 곱한 값입니다. */
    struct ActionGradingRules
    {
        vector<ActionGradeDef> _listGrade{}; ///< 높은 등급부터 둔다(읽을 때 정렬한다). 비어 있으면 S 90, A 75, B 55, C 0 을 쓴다
        float32                _timeWeight{ 1.0f };
        float32                _hitWeight{ 1.0f };
        float32                _collectWeight{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 모드 수치입니다(기반 몸 위에 얹는 활공, 갈고리, 드릴). 거리는 월드 단위, 시간은 초입니다. */
    struct ActionBodySettings
    {
        float32 _glideFallSpeed{ 2.0f };            ///< 우산을 펴면 이 속도보다 빨리 떨어지지 않는다
        float32 _glideGravityScale{ 0.35f };        ///< 우산을 펴고 있는 동안 적용하는 중력 배율
        float32 _grappleRange{ 6.0f };              ///< 이 거리 안의 갈고리 지점에 건다
        float32 _grappleMinLength{ 0.75f };         ///< 이보다 가까운 지점에는 걸지 않는다
        float32 _grappleSwingAcceleration{ 14.0f }; ///< 좌우 입력이 진자 접선 방향으로 주는 가속
        float32 _grappleMaxSpeed{ 24.0f };
        float32 _drillSpeed{ 11.0f };     ///< 흙 속에서 굴착하는 속도
        float32 _drillExitSpeed{ 11.0f }; ///< 흙에서 튀어나올 때의 진행 방향 속도
        float32 _drillJumpSpeed{ 16.0f }; ///< 튀어나올 때 점프를 누르고 있으면 적용하는 위쪽 속도(페퍼 그라인더의 도약)
        float32 _drillEntryTime{ 0.2f };  ///< 흙에 닿지 못한 채 이 시간이 지나면 드릴을 그만둔다
    };
} // namespace sw

namespace sw
{
    /** @brief 근접 콤보 하나입니다. 기반 `MoveCatalog` 기술 id 를 순서대로 연결한 것이며, 다음 기술로 가려면 그 기술의 캔슬 창이 열려 있어야 합니다. */
    struct ActionComboDef
    {
        hashed_string         _id{};
        vector<hashed_string> _listMove{};
    };
} // namespace sw

namespace sw
{
    /** @brief 패리 반사와 히트스톱 규칙입니다. 시간 단위는 프레임입니다. */
    struct ActionParryRules
    {
        float32 _radius{ 1.5f };             ///< 패리 창 동안 이 거리 안에 있는 적 탄을 되받아친다
        float32 _reflectSpeedScale{ 1.5f };  ///< 되받아친 탄의 속도 배율
        float32 _reflectDamageScale{ 2.0f }; ///< 되받아친 탄의 피해 배율
        int32   _windowFrames{ 8 };          ///< 패리 버튼을 누른 뒤 패리 창이 열려 있는 길이
        int32   _hitstopFrames{ 6 };         ///< 되받아칠 때 거는 히트스톱
        int32   _attackBufferFrames{ 8 };    ///< 공격 버튼을 이 프레임 수만큼 기억한다(콤보를 미리 누를 수 있다)
    };
} // namespace sw

namespace sw
{
    /** @brief 적 패턴의 상태 하나입니다. */
    struct ActionPatternStateDef
    {
        hashed_string _id{};
        hashed_string _next{};   ///< `_frames` 가 지나면 갈 상태(비어 있으면 이 상태에 머문다)
        hashed_string _onNear{}; ///< 플레이어가 `_nearRange` 안에 있으면 바로 갈 상태
        hashed_string _onHit{};  ///< 맞으면 갈 상태(경직)
        float32       _nearRange{ 0.0f };
        float32       _moveX{ 0.0f };      ///< 이 상태의 좌우 이동(−1..1, 바라보는 쪽 기준)
        float32       _fireSpeed{ 10.0f }; ///< 쏘는 탄의 속도
        int32         _frames{ 60 };
        uint8         _bFire{ SW_FALSE };   ///< 이 상태에 들어서는 프레임에 한 발 쏜다
        uint8         _bAttack{ SW_FALSE }; ///< 이 상태 동안 근접 판정이 켜져 있다
    };
} // namespace sw

namespace sw
{
    /** @brief 적 패턴 하나입니다. 상태 목록과 시작 상태를 가집니다. */
    struct SW_GF_API ActionPatternDef
    {
        hashed_string                 _id{};
        hashed_string                 _start{};
        vector<ActionPatternStateDef> _listState{};

        /** @brief 상태 인덱스를 반환합니다. 없으면 −1 입니다. */
        int32 findStateIndex( const hashed_string& stateID ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionPlatformerCatalog
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<ActionPlatformer><Grading time="1" hits="1" collect="1"><Grade id="S" min="90"/></Grading><Body glideFallSpeed="2" .../>
     *        <Parry window="8" radius="1.5" reflectSpeed="1.5" reflectDamage="2" hitstop="6" buffer="8"/>
     *        <Stage id="1-1" parTime="90" lives="3" hitTolerance="5" checkpoints="cp1,cp2" secrets="gem1,gem2"/>
     *        <Combo id="sword" moves="slash1,slash2,slash3"/>
     *        <Pattern id="gunner" start="patrol"><State id="patrol" frames="90" next="aim" onNear="aim" near="5" moveX="1"/>
     *        <State id="aim" frames="20" next="shoot"/><State id="shoot" frames="1" next="patrol" fire="true" fireSpeed="9"/></Pattern></ActionPlatformer>`
     */
    class SW_GF_API ActionPlatformerCatalog : public XMLCatalog<ActionPlatformerCatalog>
    {
        friend class XMLCatalog<ActionPlatformerCatalog>;

    public:
        ActionPlatformerCatalog();

        const ActionStageDef*     findStage( const hashed_string& id ) const { return _stageCatalog.find( id ); }
        const ActionComboDef*     findCombo( const hashed_string& id ) const { return _comboCatalog.find( id ); }
        const ActionPatternDef*   findPattern( const hashed_string& id ) const { return _patternCatalog.find( id ); }
        const ActionGradingRules& getGrading() const { return _grading; }
        const ActionBodySettings& getBodySettings() const { return _bodySettings; }
        const ActionParryRules&   getParryRules() const { return _parryRules; }
        ActionParryRules&         getParryRules() { return _parryRules; }

    private:
        static constexpr const utf8* kXMLRootName = "ActionPlatformer"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         loadPattern( const XMLNode& node, const utf8* pID, string_view sourceName );

        GameCatalog<ActionStageDef>   _stageCatalog;
        GameCatalog<ActionComboDef>   _comboCatalog;
        GameCatalog<ActionPatternDef> _patternCatalog;
        ActionGradingRules            _grading;
        ActionBodySettings            _bodySettings;
        ActionParryRules              _parryRules;
    };
} // namespace sw
