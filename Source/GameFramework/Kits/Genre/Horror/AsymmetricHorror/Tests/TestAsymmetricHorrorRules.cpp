#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorSnapshot.h"
#include "GameFramework/Runtime/Character/AI/AIPerception.h"

#include <cstring>

// 비대칭 공포 키트의 규칙: 카탈로그, 공격과 빈사, 들기와 갈고리, 치료, 발전기와 스킬 체크, 판자, 창틀, 사물함, 탈출구와 해치, 결과, 상태 왕복과 꾸민 바이트 거절. 키트 GF_AsymmetricHorror(nogpu).
// 꾸민 바이트는 `writeState` 의 정상 바이트에서 칸 하나만 바꿔 넣는다. 칸의 자리는 `AsymmetricFixtureBytes` 가 형식대로 세고, 정상 값을 먼저 읽어 자리가 맞는지 본다.

using namespace sw;

namespace
{
    constexpr const utf8* kAsymmetricXML = R"(<AsymmetricHorrorRules survivorSpeed="4" crawlSpeed="1" hitHasteScale="1.5" hitHasteTime="1" bleedoutTime="10" interactRange="2" generatorsRequired="2">
  <Generator time="1" scales="1,1.5" skillCheckInterval="0" noiseRadius="60" kickPenalty="0.1" kickRegression="0.1" maxRepairers="2"/>
  <Heal time="1" scales="1,2" skillCheckInterval="0" maxHealers="2"/>
  <SkillCheck leadTime="0.1" failPenalty="0.1"><Window grade="Great" early="0.05" late="0.05" score="300" bonus="1"/></SkillCheck>
  <Hook stageTime="1" struggleGrace="0.5" wiggleTime="1" wiggleStunTime="2" stages="3"/>
  <Pallet stunTime="2" stunRange="2" breakTime="1"/>
  <Window fastTime="0.5" mediumTime="1" killerTime="1" fastSpeedRatio="0.9" noiseRadius="40" blockTime="2" blockCount="2"/>
  <Locker searchTime="1"/>
  <Endgame gateTime="1" collapseTime="2" slowScale="0.5" hatchRemaining="1"/>
  <Killer id="trapper" speedRatio="1.25" lungeRange="3" lungeAngle="1" hitCooldown="1" missCooldown="1" cooldownSpeedScale="0.5" terrorRadius="32" abilityCooldown="5" carrySpeedScale="0.5"/>
  <Killer id="nurse"/>
  <Category id="Objectives" cap="100"/>
  <Score action="Repair" category="Objectives" points="500"/>
  <Score action="Hit" category="Brutality" points="10"/>
</AsymmetricHorrorRules>)";

    constexpr const utf8* kAsymmetricSkillCheckXML = R"(<AsymmetricHorrorRules interactRange="2" generatorsRequired="2">
  <Generator time="100" skillCheckInterval="0.2" noiseRadius="60" maxRepairers="2"/>
  <Heal time="100" skillCheckInterval="0.2" maxHealers="2"/>
  <SkillCheck leadTime="0.1" failPenalty="0.1"><Window grade="Great" early="0.05" late="0.05" score="300" bonus="1"/></SkillCheck>
  <Killer id="trapper" lungeRange="3" hitCooldown="1"/>
</AsymmetricHorrorRules>)";

    constexpr float32 kAsymmetricStep      = 0.1f;
    constexpr float32 kAsymmetricTolerance = 0.02f;

    /** @brief 판 설정과 시간 흘리기입니다. */
    struct AsymmetricHorrorTestUtil
    {
        static AsymmetricHorrorMatchSettings makeSettings( const hashed_string& killerID = "trapper" )
        {
            AsymmetricHorrorMatchSettings settings;
            settings._killerID  = killerID;
            settings._fixedStep = kAsymmetricStep;
            settings._seed      = 7u;
            return settings;
        }

        static float3 at( float32 x, float32 z ) { return float3{ x, 0.0f, z }; }

        static void advance( AsymmetricHorrorMatch& inoutMatch, float32 seconds )
        {
            const int32 stepCount = static_cast<int32>( seconds / kAsymmetricStep + 0.5f );
            for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            {
                inoutMatch.update( kAsymmetricStep );
            }
        }
    };

    /** @brief 정상 상태 바이트입니다. 칸 하나를 바꿔 읽어 봅니다. */
    struct AsymmetricFixtureBytes
    {
        // 살인마 칸의 뒤에서부터의 자리이다(점수가 비어 있을 때, 칸 전체 69 바이트).
        static constexpr size_t kKillerBytes = 69;
        vector<uint8>           _listByte{};
        size_t                  _stageBase{ 0 }; ///< 판 다음의 완료 수 자리이다

        explicit AsymmetricFixtureBytes( const AsymmetricHorrorMatch& match )
        {
            Archive written;
            match.writeState( written );
            _listByte.assign( written.getData(), written.getData() + written.getSize() );
            Archive name;
            StateIO::writeName( name, AsymmetricHorrorTestUtil::makeSettings()._killerID );
            Archive matchBytes;
            match.getMatch().writeState( matchBytes );
            _stageBase = static_cast<size_t>( name.getSize() ) + 1 + 1 + 4 + 4 + static_cast<size_t>( matchBytes.getSize() ) + 4 + 4; // 살인마 id, 시작, 알림, 걸음, 시간, 판, 팀 둘
        }

        size_t getSize() const { return _listByte.size(); }
        /** @brief 마지막 생존자의 칸 자리입니다. @p fromEnd 는 그 생존자 칸 끝에서 센 바이트입니다. */
        size_t lastSurvivor( size_t fromEnd ) const { return getSize() - kKillerBytes - fromEnd; }

        template <typename TValue>
        TValue readAt( size_t offset ) const
        {
            TValue value{};
            std::memcpy( &value, _listByte.data() + offset, sizeof( TValue ) );
            return value;
        }

        /** @brief @p offset 의 칸을 @p value 로 바꾼 바이트를 새 판으로 읽습니다. */
        template <typename TValue>
        [[nodiscard]] bool readPatched( const AsymmetricHorrorRulesCatalog& catalog, size_t offset, TValue value ) const
        {
            vector<uint8> listByte = _listByte;
            std::memcpy( listByte.data() + offset, &value, sizeof( TValue ) );
            AsymmetricHorrorMatch restored;
            SW_EXPECT_TRUE( restored.initialize( AsymmetricHorrorTestUtil::makeSettings(), &catalog ) );
            Archive reading( listByte.data(), listByte.size() );
            return restored.readState( reading );
        }
    };

    /** @brief 한 판입니다. 카탈로그를 읽어 살인마를 정해 둡니다. 무대는 케이스마다 놓습니다. */
    struct AsymmetricHorrorRulesTest
    {
        AsymmetricHorrorRulesCatalog  _catalog{};
        AsymmetricHorrorMatch         _match{};
        vector<AsymmetricHorrorEvent> _listEvent{};

        AsymmetricHorrorRulesTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kAsymmetricXML, "AsymmetricHorrorRulesTest" ) );
            SW_REQUIRE_TRUE( _match.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
        }

        void advance( float32 seconds ) { AsymmetricHorrorTestUtil::advance( _match, seconds ); }

        /** @brief 쌓인 이벤트를 `_listEvent` 뒤로 모으고 @p kind 의 수를 셉니다. */
        int32 countEvents( AsymmetricHorrorEvent::Kind kind )
        {
            _match.pollEvents( _listEvent );
            return VectorUtil::countBy<&AsymmetricHorrorEvent::_kind>( _listEvent, kind );
        }

        const AsymmetricHorrorSurvivor& survivor( int32 index ) const { return *_match.findSurvivor( index ); }

        /** @brief 원점의 생존자를 쓰러뜨립니다. 살인마는 2 m 뒤에서 +Z 를 봅니다. */
        void downAtOrigin()
        {
            _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
            SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
            advance( 1.2f );
            SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Downed );
            advance( 1.2f );
        }

        /** @brief 쓰러진 @p index 를 들어 0 번 갈고리에 겁니다. */
        void hangOnHook( int32 index )
        {
            SW_EXPECT_TRUE( _match.pickUp( index ) );
            SW_EXPECT_TRUE( _match.hookCarried( 0 ) );
        }

        /** @brief 발전기 0, 1 번을 원점 근처의 @p index 가 차례로 고쳐 전원을 넣습니다. */
        void powerGates( int32 index )
        {
            for ( int32 generator = 0; generator < 2; ++generator )
            {
                SW_EXPECT_TRUE( _match.startRepair( index, generator ) );
                advance( 1.2f );
            }
            SW_EXPECT_TRUE( _match.areGatesPowered() );
        }

        /** @brief 왕복한 판이 같은 바이트이고, 같은 시간을 흘려도 같은 판인지 봅니다. */
        void expectRoundTrip()
        {
            AsymmetricHorrorMatch restored;
            SW_REQUIRE_TRUE( restored.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
            SW_REQUIRE_TRUE( StateBytesTestUtil::roundTrips( _match, restored ) );
            vector<AsymmetricHorrorEvent> listEvent;
            restored.pollEvents( listEvent );
            SW_EXPECT_TRUE( listEvent.empty() );
            advance( 0.5f );
            AsymmetricHorrorTestUtil::advance( restored, 0.5f );
            SW_EXPECT_TRUE( StateBytesTestUtil::hasSameState( _match, restored ) );
        }
    };

    /** @brief 스킬 체크가 뜨는 판입니다. 발전기와 치료가 오래 걸리고, 맞히면 보너스로 바로 끝납니다. */
    struct AsymmetricHorrorTest
    {
        AsymmetricHorrorRulesCatalog  _catalog{};
        AsymmetricHorrorMatch         _match{};
        vector<AsymmetricHorrorEvent> _listEvent{};

        AsymmetricHorrorTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kAsymmetricSkillCheckXML, "AsymmetricHorrorTest" ) );
            SW_REQUIRE_TRUE( _match.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
        }

        int32 countEvents( AsymmetricHorrorEvent::Kind kind )
        {
            _match.pollEvents( _listEvent );
            return VectorUtil::countBy<&AsymmetricHorrorEvent::_kind>( _listEvent, kind );
        }

        /** @brief @p index 에게 스킬 체크가 뜰 때까지 걸음을 돌립니다. */
        bool waitForSkillCheck( int32 index )
        {
            for ( int32 stepIndex = 0; stepIndex < 30; ++stepIndex )
            {
                _match.update( kAsymmetricStep );
                const InteractionProgress* pProgress = _match.findActivityProgress( index );
                if ( pProgress != nullptr && pProgress->hasPendingSkillCheck() )
                    return true;
            }
            return false;
        }
    };
} // namespace

// ── 카탈로그 ──────────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 루트 속성과 하위 원소, 살인마, 점수를 읽는다. 같은 속성 이름도 원소마다 다른 칸이다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, CatalogReadsEveryElement )
{
    const AsymmetricHorrorRules& rules = _catalog.getRules();
    SW_EXPECT_EQUAL( 2, rules._generatorsRequired );
    SW_EXPECT_NEAR( 2.0f, rules._interactRange, kAsymmetricTolerance );
    SW_EXPECT_NEAR( 1.0f, rules._generator._time, kAsymmetricTolerance );
    SW_REQUIRE_EQUAL( size_t{ 2 }, rules._generator._listScale.size() );
    SW_EXPECT_NEAR( 1.5f, rules._generator._listScale[1], kAsymmetricTolerance );
    SW_EXPECT_NEAR( 40.0f, rules._window._noiseRadius, kAsymmetricTolerance );
    SW_EXPECT_EQUAL( 3, rules._hook._stages );

    SW_EXPECT_EQUAL( size_t{ 2 }, _catalog.getKillers().size() );
    SW_REQUIRE_TRUE( _catalog.findKiller( "trapper" ) != nullptr );
    SW_EXPECT_NEAR( 1.25f, _catalog.findKiller( "trapper" )->_speedRatio, kAsymmetricTolerance );
    SW_EXPECT_TRUE( _catalog.findKiller( "nothing" ) == nullptr );

    SW_REQUIRE_TRUE( _catalog.findScoreRule( "Repair" ) != nullptr );
    SW_EXPECT_NEAR( 500.0f, _catalog.findScoreRule( "Repair" )->_points, kAsymmetricTolerance );
    SW_EXPECT_TRUE( _catalog.findScoreRule( "Vault" ) == nullptr );
    SW_EXPECT_NEAR( 100.0f, _catalog.getCategoryCap( "Objectives" ), kAsymmetricTolerance );
    SW_EXPECT_NEAR( 0.0f, _catalog.getCategoryCap( "Brutality" ), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 헤더의 예대로 창에 `bonus` 를 적으면 판정기가 모르는 속성으로 거절해 `<SkillCheck>` 가 통째로 빠졌다. 판정기가 부르는 쪽의 속성을 받는다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, SkillCheckWindowAcceptsBonus )
{
    SW_EXPECT_NEAR( 1.0f, _catalog.getRules()._skillCheck._bonus.getValue( "Great" ), kAsymmetricTolerance );
    SW_EXPECT_NEAR( 0.1f, _catalog.getRules()._skillCheck._failPenalty, kAsymmetricTolerance );
    SW_REQUIRE_EQUAL( size_t{ 1 }, _catalog.getJudge().getWindows().size() );
    SW_EXPECT_EQUAL( 300, _catalog.getJudge().getWindows()[0]._score );
}

/**
 * @brief [AsymmetricHorrorRulesTest] `bonus` 밖의 모르는 창 속성은 그대로 오타로 거절한다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, SkillCheckWindowRefusesUnknownField )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><SkillCheck><Window grade="Great" bonuss="1"/></SkillCheck><Killer id="a"/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
    SW_EXPECT_TRUE( catalog.getJudge().getWindows().empty() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 살인마는 `add` 로 앞 정의를 말없이 덮어썼다(주석은 "뒤의 것을 버린다"). 겹친 id 를 거절한다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogRefusesDuplicateKiller )
{
    SW_EXPECT_LOG( "GameDataXML.DuplicateID" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><Killer id="a" lungeRange="1"/><Killer id="a" lungeRange="2"/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
    SW_EXPECT_NEAR( 1.0f, catalog.findKiller( "a" )->_lungeRange, kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 겹친 범주는 앞 상한을 말없이 바꿨다 — 거절한다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogRefusesDuplicateCategory )
{
    SW_EXPECT_LOG( "AsymmetricHorrorRulesCatalog.DuplicateCategory" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><Killer id="a"/><Category id="c" cap="1"/><Category id="c" cap="2"/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
    SW_EXPECT_NEAR( 1.0f, catalog.getCategoryCap( "c" ), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 겹친 점수는 앞 규칙에 가려 말없이 쓰이지 않았다 — 거절한다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogRefusesDuplicateScore )
{
    SW_EXPECT_LOG( "AsymmetricHorrorRulesCatalog.DuplicateScoreRule" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><Killer id="a"/><Score action="Hit" category="c" points="1"/><Score action="Hit" category="c" points="2"/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
    SW_EXPECT_NEAR( 1.0f, catalog.findScoreRule( "Hit" )->_points, kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 살인마가 없으면 판을 열 수 없으므로 실패이다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogNeedsKiller )
{
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( "<AsymmetricHorrorRules/>", "AsymmetricHorrorRulesTest" ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 모르는 원소는 오류이다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogRefusesUnknownElement )
{
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><Killer id="a"/><Campfire/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 범위 밖 설정은 그 원소를 바꾸지 않는다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, CatalogRefusesOutOfRangeSetting )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    AsymmetricHorrorRulesCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AsymmetricHorrorRules><Killer id="a"/><Hook stages="0"/></AsymmetricHorrorRules>)", "AsymmetricHorrorRulesTest" ) );
    SW_EXPECT_EQUAL( AsymmetricHorrorHookDef{}._stages, catalog.getRules()._hook._stages );
}

// ── 판 열기 ───────────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 모르는 살인마는 거절한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, InitializeRefusesUnknownKiller )
{
    SW_EXPECT_LOG( "AsymmetricHorrorMatch.UnknownKiller" );
    AsymmetricHorrorMatch match;
    SW_EXPECT_FALSE( match.initialize( AsymmetricHorrorTestUtil::makeSettings( "nobody" ), &_catalog ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 범위 밖 걸음은 거절한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, InitializeRefusesBadStep )
{
    SW_EXPECT_LOG( "AsymmetricHorrorMatch.AsymmetricHorrorMatchRefusedSettings" );
    AsymmetricHorrorMatchSettings settings = AsymmetricHorrorTestUtil::makeSettings();
    settings._fixedStep                    = 0.0f;
    AsymmetricHorrorMatch match;
    SW_EXPECT_FALSE( match.initialize( settings, &_catalog ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 카탈로그가 없으면 판을 열지 않고 무대도 받지 않는다
 */
SW_TEST_CASE( AsymmetricHorrorRulesTest, InitializeRefusesNoCatalog )
{
    AsymmetricHorrorMatch match;
    SW_EXPECT_FALSE( match.initialize( AsymmetricHorrorTestUtil::makeSettings(), nullptr ) );
    SW_EXPECT_FALSE( match.addSurvivor( float3{} ).has_value() );
    SW_EXPECT_FALSE( match.addGenerator( float3{} ).has_value() );
    SW_EXPECT_FALSE( match.addGate( float3{} ).has_value() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 시작 전에는 시간이 흐르지 않는다. 시작 뒤에는 생존자를 더하지 못하고, 걸음은 고정이다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, StartAndFixedStep )
{
    SW_EXPECT_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.update( 1.0f );
    SW_EXPECT_EQUAL( 0u, _match.getTick() );
    SW_EXPECT_TRUE( _match.getMatch().getPhase() == MatchPhase::Waiting );

    _match.start();
    SW_EXPECT_TRUE( _match.getMatch().getPhase() == MatchPhase::InProgress );
    SW_EXPECT_FALSE( _match.addSurvivor( float3{} ).has_value() );
    _match.update( 0.05f );
    SW_EXPECT_EQUAL( 0u, _match.getTick() ); // 한 걸음이 차지 않았다
    _match.update( 0.05f );
    SW_EXPECT_EQUAL( 1u, _match.getTick() );
    SW_EXPECT_EQUAL( 1, _match.countStandingSurvivors() );
    SW_EXPECT_TRUE( _match.findSurvivor( 1 ) == nullptr );
    SW_EXPECT_TRUE( _match.findSurvivor( -1 ) == nullptr );
}

// ── 이동, 공격 ───────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 이동: XZ 평면에서 길이 1 로 자르고, 하던 일을 그만둔다. 없는 번호는 아무 일도 없다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, SurvivorMoves )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{ 0.0f, 9.0f, 0.0f } ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    SW_EXPECT_NEAR( 0.0f, survivor( 0 )._position._y, kAsymmetricTolerance );
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );

    _match.moveSurvivor( 9, float3{ 1.0f, 0.0f, 0.0f }, 1.0f );
    _match.moveSurvivor( 0, float3{}, 1.0f );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::Repairing ); // 멈춰 있으면 그대로이다

    _match.moveSurvivor( 0, float3{ 5.0f, 9.0f, 0.0f }, 0.5f );
    SW_EXPECT_NEAR( 2.0f, survivor( 0 )._position._x, kAsymmetricTolerance );
    SW_EXPECT_NEAR( 0.0f, survivor( 0 )._position._y, kAsymmetricTolerance );
    SW_EXPECT_NEAR( 4.0f, survivor( 0 )._lastMoveSpeed, kAsymmetricTolerance );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );
    SW_EXPECT_EQUAL( 0, _match.findGenerator( 0 )->_progress.getParticipantCount() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 공격: 건강 → 부상(달아나는 속도) → 빈사(기어감). 쿨다운 중에는 못 하고 그때 느려진다. 점수를 준다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, AttackHurtsThenDowns )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_NEAR( 5.0f, _match.computeKillerSpeed(), kAsymmetricTolerance );

    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
    SW_EXPECT_NEAR( 6.0f, _match.computeSurvivorSpeed( 0 ), kAsymmetricTolerance );
    SW_EXPECT_NEAR( 2.5f, _match.computeKillerSpeed(), kAsymmetricTolerance );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::NotReady );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Hit ) );
    SW_EXPECT_NEAR( 10.0f, _match.getKiller()._score.getValue( "Brutality" ), kAsymmetricTolerance );

    advance( 1.2f );
    SW_EXPECT_NEAR( 4.0f, _match.computeSurvivorSpeed( 0 ), kAsymmetricTolerance );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured ); // 다음 걸음이 건강으로 되돌리지 않는다

    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Downed );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Dying );
    SW_EXPECT_NEAR( 1.0f, _match.computeSurvivorSpeed( 0 ), kAsymmetricTolerance );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Downed ) );
    SW_EXPECT_FALSE( _match.startRepair( 0, 0 ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 헛방: 거리 밖, 등 뒤, 빈사인 대상은 맞지 않고 헛방 쿨다운이 걸린다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, AttackMisses )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -9.0f ) );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Missed );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Missed ) );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::NotReady );

    advance( 1.2f );
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, 2.0f ) ); // 대상은 등 뒤에 있다
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Missed );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Healthy );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 심장 소리는 위협 반경으로, 자극은 숨지 않은 생존자로 정한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, HeartbeatAndStimuli )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addLocker( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 16.0f, 0.0f ) );
    SW_EXPECT_NEAR( 0.5f, _match.computeHeartbeat( 0 ), kAsymmetricTolerance );
    SW_EXPECT_NEAR( 0.0f, _match.computeHeartbeat( 9 ), kAsymmetricTolerance );
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 99.0f, 0.0f ) );
    SW_EXPECT_NEAR( 0.0f, _match.computeHeartbeat( 0 ), kAsymmetricTolerance );

    vector<AIStimulus> listStimulus;
    _match.collectKillerStimuli( listStimulus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_EQUAL( uint64{ 1 }, listStimulus[0]._id );
    SW_REQUIRE_TRUE( _match.enterLocker( 0, 0 ) );
    _match.collectKillerStimuli( listStimulus );
    SW_EXPECT_TRUE( listStimulus.empty() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 능력: 쿨다운이 지나야 다시 쓴다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, AbilityCooldown )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 90.0f, 0.0f ) ) == 0 ); // 아무도 없으면 첫 걸음에 판이 끝난다
    _match.start();
    SW_EXPECT_TRUE( _match.useKillerAbility() );
    SW_EXPECT_FALSE( _match.useKillerAbility() );
    advance( 5.2f );
    SW_EXPECT_TRUE( _match.useKillerAbility() );
}

// ── 들기, 갈고리 ─────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 들기: 빈사인 대상만, 거리 안에서, 한 번에 하나이다. 든 생존자는 살인마를 따라가고 살인마는 느려지며 공격하지 못한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, PickUpCarries )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 1 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_FALSE( _match.pickUp( 0 ) ); // 아직 건강하다
    SW_EXPECT_FALSE( _match.pickUp( 9 ) );
    downAtOrigin();
    SW_EXPECT_TRUE( _match.pickUp( 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Carried );
    SW_EXPECT_EQUAL( 0, _match.getKiller()._carrying );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::PickedUp ) );
    SW_EXPECT_FALSE( _match.pickUp( 1 ) );
    SW_EXPECT_NEAR( 2.5f, _match.computeKillerSpeed(), kAsymmetricTolerance );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::NotReady );

    _match.moveKiller( float3{ 1.0f, 0.0f, 0.0f }, 1.0f );
    SW_EXPECT_NEAR( 2.5f, survivor( 0 )._position._x, kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 갈고리: 시간이 지나면 다음 단계이고, 몸부림 단계에서 멈추면 희생된다. 한 명이면 판이 끝나고 살인마가 이긴다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, HookStagesLeadToSacrifice )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    _match.start();
    downAtOrigin();
    SW_EXPECT_FALSE( _match.hookCarried( 0 ) ); // 아직 들지 않았다
    SW_EXPECT_TRUE( _match.pickUp( 0 ) );
    SW_EXPECT_FALSE( _match.hookCarried( 9 ) );
    SW_EXPECT_TRUE( _match.hookCarried( 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Hooked );
    SW_EXPECT_EQUAL( 1, survivor( 0 )._hookStage );
    SW_EXPECT_EQUAL( -1, _match.getKiller()._carrying );

    _match.setStruggling( 0, true );
    advance( 1.2f );
    SW_EXPECT_EQUAL( 2, survivor( 0 )._hookStage );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::HookStageAdvanced ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Hooked );

    _match.setStruggling( 0, false );
    advance( 0.7f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Sacrificed );
    SW_EXPECT_EQUAL( SW_FALSE, survivor( 0 )._bBledOut );
    SW_EXPECT_TRUE( _match.isEnded() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::MatchEnded ) );
    SW_EXPECT_EQUAL( _match.getMatch().findParticipant( _match.getKiller()._participant )->_team, _match.getMatch().getWinningTeam() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 구출하면 부상으로 내려온다. 자기 자신은 구하지 못한다. 걸린 횟수가 단계 수에 닿으면 바로 희생된다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RescueAndLastHook )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    _match.start();
    downAtOrigin();
    hangOnHook( 0 );
    SW_EXPECT_FALSE( _match.rescue( 0, 0 ) );
    SW_EXPECT_TRUE( _match.rescue( 1, 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Rescued ) );
    SW_EXPECT_FALSE( _match.rescue( 1, 0 ) ); // 이제 걸려 있지 않다

    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Downed );
    hangOnHook( 0 );
    SW_EXPECT_EQUAL( 2, survivor( 0 )._hookStage );
    SW_EXPECT_TRUE( _match.rescue( 1, 0 ) );

    advance( 1.2f );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::Downed );
    hangOnHook( 0 );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Sacrificed );
    SW_EXPECT_EQUAL( -1, _match.getKiller()._carrying );
    SW_EXPECT_FALSE( _match.isEnded() ); // 한 명이 남았다
}

/**
 * @brief [AsymmetricHorrorRulesTest] 몸부림을 다 채우면 빠져나와 부상이 되고, 살인마는 기절한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, WiggleFreeStunsKiller )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.start();
    downAtOrigin();
    SW_REQUIRE_TRUE( _match.pickUp( 0 ) );
    _match.setWiggling( 0, true );
    advance( 1.2f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
    SW_EXPECT_EQUAL( -1, _match.getKiller()._carrying );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::WiggledFree ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::KillerStunned ) );
    SW_EXPECT_FALSE( _match.getKiller().canAct() );
    SW_EXPECT_TRUE( _match.killerAttack() == KillerAttackResult::NotReady );
    SW_EXPECT_NEAR( 0.0f, _match.computeKillerSpeed(), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 빈사를 내버려 두면 출혈로 죽는다. 희생과 구별한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, BleedingOutSacrifices )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.start();
    downAtOrigin();
    advance( 10.5f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Sacrificed );
    SW_EXPECT_EQUAL( SW_TRUE, survivor( 0 )._bBledOut );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Sacrificed ) );
}

// ── 치료 ─────────────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 치료: 다친 다른 사람만, 거리 안에서이다. 끝나면 한 단계 낫는다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, HealingInjuredRestoresHealth )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 2 );
    _match.start();
    SW_EXPECT_FALSE( _match.startHeal( 1, 0 ) ); // 아직 다치지 않았다
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
    SW_EXPECT_FALSE( _match.startHeal( 0, 0 ) );
    SW_EXPECT_FALSE( _match.startHeal( 2, 0 ) ); // 멀다
    SW_EXPECT_TRUE( _match.startHeal( 1, 0 ) );
    SW_EXPECT_TRUE( _match.startHeal( 1, 0 ) ); // 이미 하고 있다
    SW_EXPECT_TRUE( _match.findActivityProgress( 1 ) == &survivor( 0 )._healing );

    advance( 1.2f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Healthy );
    SW_EXPECT_TRUE( survivor( 1 )._activity == SurvivorActivity::None );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Healed ) );
    SW_EXPECT_NEAR( 0.0f, survivor( 0 )._healing.getProgress(), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 빈사 치료: 받는 동안 출혈이 멈추고, 그만두면 다시 흐르며, 끝나면 부상으로 돌아온다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, HealingDyingStopsBleeding )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 1 );
    _match.start();
    downAtOrigin();
    const float32 bleedBefore = survivor( 0 )._healthMachine.getDownedHealth();
    SW_REQUIRE_TRUE( _match.startHeal( 1, 0 ) );
    advance( 0.5f );
    SW_EXPECT_NEAR( bleedBefore, survivor( 0 )._healthMachine.getDownedHealth(), kAsymmetricTolerance );

    _match.stopActivity( 1 );
    advance( 0.5f );
    SW_EXPECT_NEAR( bleedBefore - 0.5f, survivor( 0 )._healthMachine.getDownedHealth(), kAsymmetricTolerance );

    SW_REQUIRE_TRUE( _match.startHeal( 1, 0 ) );
    advance( 0.7f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
    SW_EXPECT_TRUE( survivor( 0 )._healthMachine.isAlive() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 환자가 움직여도 치료는 거리를 보지 않아, 멀리 달아난 환자를 그대로 고쳤다. 거리를 벗어나면 치료자를 뗀다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, MovingPatientOutOfRangeStopsHealing )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.0f ) ) == 1 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
    SW_REQUIRE_TRUE( _match.startHeal( 1, 0 ) );

    _match.moveSurvivor( 0, float3{ 0.0f, 0.0f, 1.0f }, 0.1f ); // 가까이에서는 이어 간다
    SW_EXPECT_TRUE( survivor( 1 )._activity == SurvivorActivity::Healing );

    _match.moveSurvivor( 0, float3{ 1.0f, 0.0f, 0.0f }, 1.0f );
    SW_EXPECT_TRUE( survivor( 1 )._activity == SurvivorActivity::None );
    SW_EXPECT_EQUAL( 0, survivor( 0 )._healing.getParticipantCount() );
    advance( 1.2f );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
}

// ── 발전기 ───────────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 수리: 거리 안에서 고치고, 둘이면 배율만큼 빠르며, 끝나면 손을 떼고 점수는 범주 상한에서 멈춘다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RepairingGenerator )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 2 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    SW_EXPECT_FALSE( _match.startRepair( 2, 0 ) ); // 멀다
    SW_EXPECT_FALSE( _match.startRepair( 0, 9 ) );
    SW_EXPECT_TRUE( _match.startRepair( 0, 0 ) );
    SW_EXPECT_TRUE( _match.startRepair( 0, 0 ) ); // 이미 고치고 있다
    SW_EXPECT_TRUE( _match.startRepair( 1, 0 ) );
    SW_EXPECT_EQUAL( 2, _match.findGenerator( 0 )->_progress.getParticipantCount() );
    advance( 0.4f );
    SW_EXPECT_NEAR( 0.6f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );

    advance( 0.4f );
    SW_EXPECT_TRUE( _match.findGenerator( 0 )->_progress.isCompleted() );
    SW_EXPECT_EQUAL( 1, _match.getCompletedGeneratorCount() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::GeneratorCompleted ) );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );
    SW_EXPECT_FALSE( _match.areGatesPowered() );
    SW_EXPECT_FALSE( _match.startRepair( 0, 0 ) );
    SW_EXPECT_NEAR( 100.0f, survivor( 0 )._score.getValue( "Objectives" ), kAsymmetricTolerance ); // 몫 250 이 상한에서 멈춘다
}

/**
 * @brief [AsymmetricHorrorRulesTest] 필요한 수를 고치면 전원이 들어오고, 남은 발전기는 막히며 고치던 사람은 손을 뗀다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, GatesPowerBlocksRest )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 1 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 2 );
    _match.start();
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    advance( 1.2f );
    SW_REQUIRE_TRUE( _match.startRepair( 0, 1 ) );
    advance( 0.5f );
    SW_REQUIRE_TRUE( _match.startRepair( 1, 2 ) );
    advance( 0.7f );

    SW_EXPECT_TRUE( _match.areGatesPowered() );
    SW_EXPECT_EQUAL( 2, _match.getCompletedGeneratorCount() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::GatesPowered ) );
    SW_EXPECT_EQUAL( SW_TRUE, _match.findGenerator( 2 )->_bBlocked );
    SW_EXPECT_FALSE( _match.findGenerator( 2 )->_progress.isCompleted() );
    SW_EXPECT_TRUE( survivor( 1 )._activity == SurvivorActivity::None );
    SW_EXPECT_FALSE( _match.startRepair( 1, 2 ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 걷어차기: 아무도 없고 진행이 있을 때만, 깎고 줄어든다. 누가 다시 붙으면 멈춘다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, KickingGeneratorRegresses )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_FALSE( _match.kickGenerator( 0 ) ); // 진행이 없다
    SW_EXPECT_FALSE( _match.kickGenerator( 9 ) );
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    advance( 0.5f );
    SW_EXPECT_FALSE( _match.kickGenerator( 0 ) ); // 고치고 있다
    _match.stopActivity( 0 );
    advance( 1.0f );
    SW_EXPECT_NEAR( 0.5f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance ); // 걷어차지 않으면 줄지 않는다

    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) );
    SW_EXPECT_FALSE( _match.kickGenerator( 0 ) ); // 멀다
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_TRUE( _match.kickGenerator( 0 ) );
    SW_EXPECT_NEAR( 0.4f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );
    SW_EXPECT_EQUAL( SW_TRUE, _match.findGenerator( 0 )->_bKicked );
    SW_EXPECT_FALSE( _match.kickGenerator( 0 ) ); // 이미 걷어찼다
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::GeneratorKicked ) );
    advance( 1.0f );
    SW_EXPECT_NEAR( 0.3f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );

    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    SW_EXPECT_EQUAL( SW_FALSE, _match.findGenerator( 0 )->_bKicked );
    _match.stopActivity( 0 );
    advance( 1.0f );
    SW_EXPECT_NEAR( 0.3f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 걷어차인 발전기가 0 까지 줄면 표시만 내리고 퇴행 규칙은 그대로였다. 그 뒤로 고치다 손을 떼면 걷어차지 않아도 줄었다 — 규칙도 내린다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RegressionEndsAtZero )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    advance( 0.3f );
    _match.stopActivity( 0 );
    SW_REQUIRE_TRUE( _match.kickGenerator( 0 ) );
    advance( 3.0f );
    SW_EXPECT_NEAR( 0.0f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );
    SW_EXPECT_EQUAL( SW_FALSE, _match.findGenerator( 0 )->_bKicked );

    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    advance( 0.5f );
    _match.stopActivity( 0 );
    advance( 1.0f );
    SW_EXPECT_NEAR( 0.5f, _match.findGenerator( 0 )->_progress.getProgress(), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorTest] 결함: 스킬 체크 보너스로 끝난 발전기는 다음 걸음이 "끝난 것" 으로 건너뛰어 완료 수가 오르지 않았고, 고치던 사람은 끝난 발전기에 붙은 채였다. 전원이 영영 들어오지 않는다 — 응답에서 바로 처리한다
 */
SW_TEST_CASE_F( AsymmetricHorrorTest, SkillCheckBonusCompletesGenerator )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    SW_EXPECT_FALSE( _match.respondSkillCheck( 0, 0.0f ) ); // 하는 일이 없다
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    SW_REQUIRE_TRUE( waitForSkillCheck( 0 ) );
    const float32 targetTime = _match.findActivityProgress( 0 )->getSkillCheckTargetTime();
    SW_EXPECT_TRUE( _match.respondSkillCheck( 0, targetTime ) );

    SW_EXPECT_EQUAL( 1, _match.getCompletedGeneratorCount() );
    SW_EXPECT_TRUE( _match.findSurvivor( 0 )->_activity == SurvivorActivity::None );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::GeneratorCompleted ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::SkillCheckResult ) );
    _match.update( kAsymmetricStep );
    SW_EXPECT_EQUAL( 1, _match.getCompletedGeneratorCount() ); // 두 번 세지 않는다

    // 응답 직후의 바이트도 완료 수와 맞아 다시 읽힌다.
    AsymmetricHorrorMatch restored;
    SW_REQUIRE_TRUE( restored.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
    SW_EXPECT_TRUE( StateBytesTestUtil::roundTrips( _match, restored ) );
}

/**
 * @brief [AsymmetricHorrorTest] 치료도 스킬 체크 보너스로 그 자리에서 끝난다
 */
SW_TEST_CASE_F( AsymmetricHorrorTest, SkillCheckBonusCompletesHeal )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.0f ) ) == 1 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
    SW_REQUIRE_TRUE( _match.startHeal( 1, 0 ) );
    SW_REQUIRE_TRUE( waitForSkillCheck( 1 ) );
    const float32 targetTime = _match.findActivityProgress( 1 )->getSkillCheckTargetTime();
    SW_EXPECT_TRUE( _match.respondSkillCheck( 1, targetTime ) );
    SW_EXPECT_TRUE( _match.findSurvivor( 0 )->_state == SurvivorState::Healthy );
    SW_EXPECT_TRUE( _match.findSurvivor( 1 )->_activity == SurvivorActivity::None );
}

/**
 * @brief [AsymmetricHorrorTest] 스킬 체크를 놓치면 실패로 처리하고 소음이 난다
 */
SW_TEST_CASE_F( AsymmetricHorrorTest, MissedSkillCheckMakesNoise )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    SW_REQUIRE_TRUE( waitForSkillCheck( 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::SkillCheckStarted ) );
    AsymmetricHorrorTestUtil::advance( _match, 0.3f );
    SW_EXPECT_TRUE( countEvents( AsymmetricHorrorEvent::Kind::SkillCheckResult ) >= 1 );
    SW_EXPECT_TRUE( countEvents( AsymmetricHorrorEvent::Kind::Noise ) >= 1 );
    SW_EXPECT_EQUAL( 0, _match.getCompletedGeneratorCount() );
}

// ── 판자, 창틀, 사물함 ───────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 판자: 가까운 살인마를 기절시키고 든 사람을 떨어뜨린다. 쓰러진 판자는 다시 못 내리고, 살인마가 부순다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, PalletStunsAndBreaks )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addPallet( float3{} ) == 0 );
    _match.start();
    downAtOrigin();
    SW_REQUIRE_TRUE( _match.pickUp( 0 ) );
    SW_EXPECT_FALSE( _match.breakPallet( 0 ) ); // 아직 서 있다
    SW_EXPECT_FALSE( _match.dropPallet( 1, 9 ) );
    SW_EXPECT_TRUE( _match.dropPallet( 1, 0 ) );
    SW_EXPECT_TRUE( _match.getPalletState( 0 ) == PalletState::Dropped );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::KillerStunned ) );
    SW_EXPECT_EQUAL( -1, _match.getKiller()._carrying );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Injured );
    SW_EXPECT_FALSE( _match.dropPallet( 1, 0 ) );
    SW_EXPECT_FALSE( _match.breakPallet( 0 ) ); // 기절 중이다

    advance( 2.2f );
    SW_EXPECT_TRUE( _match.breakPallet( 0 ) );
    SW_EXPECT_TRUE( _match.getPalletState( 0 ) == PalletState::Dropped );
    advance( 1.2f );
    SW_EXPECT_TRUE( _match.getPalletState( 0 ) == PalletState::Broken );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::PalletBroken ) );
    SW_EXPECT_EQUAL( -1, _match.getKiller()._breakingPallet );
    SW_EXPECT_TRUE( _match.getPalletState( 9 ) == PalletState::Broken );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 멀리 있는 살인마는 판자에 기절하지 않는다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, PalletMissesFarKiller )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addPallet( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) );
    SW_EXPECT_TRUE( _match.dropPallet( 0, 0 ) );
    SW_EXPECT_EQUAL( 0, countEvents( AsymmetricHorrorEvent::Kind::KillerStunned ) );
    SW_EXPECT_TRUE( _match.getKiller().canAct() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 창틀: 서서 넘으면 느리고 조용하며, 끝나면 건너편에 선다. 넘는 동안은 다른 일을 못 한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, VaultingWindow )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( -1.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 99.0f, 0.0f ) ); // 위협 반경 밖이다
    SW_EXPECT_FALSE( _match.vault( 0, 9 ) );
    SW_EXPECT_TRUE( _match.vault( 0, 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::Vaulting );
    SW_EXPECT_FALSE( _match.vault( 0, 0 ) );
    SW_EXPECT_EQUAL( 0, countEvents( AsymmetricHorrorEvent::Kind::Noise ) );
    advance( 0.7f );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::Vaulting ); // 느린 넘기는 1 초이다
    advance( 0.5f );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );
    SW_EXPECT_NEAR( 1.0f, survivor( 0 )._position._x, kAsymmetricTolerance );
    SW_EXPECT_EQUAL( 0, _match.findWindow( 0 )->_chaseVaultCount );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 달려 들어와 넘으면 빠르고 시끄럽다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, FastVaultMakesNoise )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( -1.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 99.0f, 0.0f ) );
    _match.moveSurvivor( 0, float3{ 1.0f, 0.0f, 0.0f }, 0.1f );
    SW_EXPECT_TRUE( _match.vault( 0, 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Noise ) );
    SW_EXPECT_NEAR( 40.0f, survivor( 0 )._noiseRadius, kAsymmetricTolerance );
    vector<AIStimulus> listStimulus;
    _match.collectKillerStimuli( listStimulus );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_NEAR( 40.0f, listStimulus[0]._noiseRadius, kAsymmetricTolerance );
    advance( 0.7f );
    SW_EXPECT_NEAR( 0.6f, survivor( 0 )._position._x, kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 막힌 창틀의 남은 시간을 아무도 줄이지 않아, 추격 중 몇 번 넘은 창틀은 판이 끝날 때까지 막혔다. 걸음마다 줄인다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, BlockedWindowReopens )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( -1.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 10.0f, 0.0f ) ); // 위협 반경 안이다
    SW_REQUIRE_TRUE( _match.vault( 0, 0 ) );
    SW_EXPECT_EQUAL( 1, _match.findWindow( 0 )->_chaseVaultCount );
    advance( 1.2f );
    SW_REQUIRE_TRUE( _match.vault( 0, 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::WindowBlocked ) );
    SW_EXPECT_EQUAL( 0, _match.findWindow( 0 )->_chaseVaultCount );
    advance( 1.2f );
    SW_EXPECT_FALSE( _match.vault( 0, 0 ) );
    SW_EXPECT_TRUE( _match.findWindow( 0 )->_blockedRemaining > 0.0f );
    advance( 1.0f );
    SW_EXPECT_NEAR( 0.0f, _match.findWindow( 0 )->_blockedRemaining, kAsymmetricTolerance );
    SW_EXPECT_TRUE( _match.vault( 0, 0 ) );
    SW_EXPECT_TRUE( _match.findWindow( 9 ) == nullptr );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 살인마도 창틀을 넘고, 넘는 동안은 다른 일을 못 한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, KillerVaults )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 90.0f, 0.0f ) ) == 0 ); // 아무도 없으면 첫 걸음에 판이 끝난다
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) );
    SW_EXPECT_FALSE( _match.killerVault( 0 ) ); // 멀다
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( -1.0f, 0.0f ) );
    SW_EXPECT_FALSE( _match.killerVault( 9 ) );
    SW_EXPECT_TRUE( _match.killerVault( 0 ) );
    SW_EXPECT_FALSE( _match.getKiller().canAct() );
    SW_EXPECT_FALSE( _match.killerVault( 0 ) );
    advance( 1.2f );
    SW_EXPECT_TRUE( _match.getKiller().canAct() );
    SW_EXPECT_NEAR( 1.0f, _match.getKiller()._position._x, kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 사물함: 한 명만 들어가고, 숨은 사람은 맞지 않으며 움직이지 않는다. 빈 사물함을 뒤지면 시간만 들고, 찾으면 체력과 상관없이 바로 들린다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, LockersHideAndGrab )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addLocker( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_FALSE( _match.searchLocker( 0 ) ); // 비었다
    SW_EXPECT_FALSE( _match.getKiller().canAct() );
    advance( 1.2f );

    SW_EXPECT_FALSE( _match.enterLocker( 0, 9 ) );
    SW_EXPECT_TRUE( _match.enterLocker( 0, 0 ) );
    SW_EXPECT_FALSE( _match.enterLocker( 1, 0 ) );
    _match.moveSurvivor( 0, float3{ 1.0f, 0.0f, 0.0f }, 1.0f );
    SW_EXPECT_NEAR( 0.0f, survivor( 0 )._position._x, kAsymmetricTolerance );
    SW_EXPECT_NEAR( 0.0f, _match.computeSurvivorSpeed( 0 ), kAsymmetricTolerance );
    _match.exitLocker( 0 );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );
    SW_EXPECT_TRUE( _match.enterLocker( 0, 0 ) );

    SW_EXPECT_TRUE( _match.searchLocker( 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Carried );
    SW_EXPECT_TRUE( survivor( 0 )._healthMachine.isDowned() );
    SW_EXPECT_EQUAL( 0, _match.getKiller()._carrying );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::LockerGrab ) );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );
    SW_EXPECT_TRUE( _match.enterLocker( 1, 0 ) ); // 비었다
}

// ── 탈출, 해치, 결과 ─────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 탈출구: 전원 뒤에 열고, 열리면 붕괴가 시작되며, 열린 탈출구 옆에서 나간다. 붕괴가 끝나면 남은 사람은 희생된다. 반반이면 비긴다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, GateEscapeAndCollapse )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 1 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    _match.start();
    SW_EXPECT_FALSE( _match.startOpenGate( 0, 0 ) ); // 전원이 없다
    SW_EXPECT_FALSE( _match.escape( 0 ) );
    powerGates( 0 );
    SW_EXPECT_FALSE( _match.startOpenGate( 0, 9 ) );
    SW_EXPECT_TRUE( _match.startOpenGate( 0, 0 ) );
    SW_EXPECT_FALSE( _match.isCollapseStarted() );
    SW_EXPECT_FALSE( _match.escape( 1 ) ); // 아직 닫혔다
    advance( 1.2f );
    SW_EXPECT_TRUE( _match.findGate( 0 )->_progress.isCompleted() );
    SW_EXPECT_TRUE( _match.isCollapseStarted() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::GateOpened ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::CollapseStarted ) );
    SW_EXPECT_TRUE( survivor( 0 )._activity == SurvivorActivity::None );

    SW_EXPECT_TRUE( _match.escape( 0 ) );
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Escaped );
    SW_EXPECT_EQUAL( 1, _match.countStandingSurvivors() );
    SW_EXPECT_FALSE( _match.isEnded() );

    advance( 2.2f );
    SW_EXPECT_TRUE( survivor( 1 )._state == SurvivorState::Sacrificed );
    SW_EXPECT_TRUE( _match.isEnded() );
    SW_EXPECT_EQUAL( -1, _match.getMatch().getWinningTeam() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::MatchEnded ) );

    SW_EXPECT_FALSE( _match.escape( 1 ) ); // 끝난 판은 아무것도 받지 않는다
    SW_EXPECT_FALSE( _match.startRepair( 1, 0 ) );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 모두 나가면 생존자 팀이 이긴다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, EveryoneEscapingWins )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 1 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    _match.start();
    powerGates( 0 );
    SW_REQUIRE_TRUE( _match.startOpenGate( 0, 0 ) );
    advance( 1.2f );
    SW_EXPECT_TRUE( _match.escape( 0 ) );
    SW_EXPECT_TRUE( _match.isEnded() );
    SW_EXPECT_EQUAL( _match.getMatch().findParticipant( survivor( 0 )._participant )->_team, _match.getMatch().getWinningTeam() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 누가 빈사이면 붕괴가 배율만큼 느리게 흐른다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, CollapseSlowsWhenSomeoneIsHelpless )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addGenerator( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addGate( AsymmetricHorrorTestUtil::at( 0.0f, 1.5f ) ) == 0 );
    _match.start();
    downAtOrigin();
    powerGates( 1 );
    SW_REQUIRE_TRUE( _match.startOpenGate( 1, 0 ) );
    advance( 1.2f );
    const float32 remaining = _match.getCollapseRemaining();
    SW_EXPECT_TRUE( remaining > 0.0f );
    advance( 1.0f );
    SW_EXPECT_NEAR( remaining - 0.5f, _match.getCollapseRemaining(), kAsymmetricTolerance );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 해치: 남은 사람이 적으면 열리고 그리로 나간다. 둘이 남았으면 닫혀 있다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, HatchOpensForLastSurvivor )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    _match.setHatchPosition( AsymmetricHorrorTestUtil::at( 1.0f, 0.0f ) );
    _match.start();
    advance( 0.2f );
    SW_EXPECT_FALSE( _match.isHatchOpen() );
    SW_EXPECT_FALSE( _match.escape( 1 ) );

    downAtOrigin();
    hangOnHook( 0 );
    _match.setStruggling( 0, true );
    advance( 2.2f ); // 시간이 다 되어 희생된다
    SW_EXPECT_TRUE( survivor( 0 )._state == SurvivorState::Sacrificed );
    SW_EXPECT_TRUE( _match.isHatchOpen() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::HatchOpened ) );

    SW_EXPECT_TRUE( _match.escape( 1 ) );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::Escaped ) );
    SW_EXPECT_TRUE( survivor( 1 )._state == SurvivorState::Escaped );
    SW_EXPECT_TRUE( _match.isEnded() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 살인마가 해치를 닫으면 전원이 들어오고 붕괴가 시작되며, 다시 열리지 않는다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, ClosingHatchStartsCollapse )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_EXPECT_FALSE( _match.closeHatch() ); // 놓지 않았다
    _match.setHatchPosition( float3{} );
    advance( 0.2f );
    SW_EXPECT_TRUE( _match.isHatchOpen() );

    SW_EXPECT_TRUE( _match.closeHatch() );
    SW_EXPECT_FALSE( _match.isHatchOpen() );
    SW_EXPECT_TRUE( _match.areGatesPowered() );
    SW_EXPECT_TRUE( _match.isCollapseStarted() );
    SW_EXPECT_EQUAL( 1, countEvents( AsymmetricHorrorEvent::Kind::HatchClosed ) );
    advance( 0.5f );
    SW_EXPECT_FALSE( _match.isHatchOpen() );
    SW_EXPECT_FALSE( _match.closeHatch() );
}

// ── 스냅숏, 왕복 ─────────────────────────────────────────────────────

/**
 * @brief [AsymmetricHorrorRulesTest] 스냅숏은 판의 보이는 상태를 그대로 옮긴다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, SnapshotCopiesVisibleState )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addPallet( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    _match.start();
    SW_REQUIRE_TRUE( _match.startRepair( 0, 0 ) );
    advance( 0.5f );

    AsymmetricHorrorSnapshot snapshot;
    _match.makeSnapshot( snapshot );
    SW_EXPECT_EQUAL( 5u, snapshot._tick );
    SW_EXPECT_TRUE( snapshot._phase == MatchPhase::InProgress );
    SW_REQUIRE_EQUAL( size_t{ 1 }, snapshot._listSurvivor.size() );
    SW_EXPECT_TRUE( snapshot._listSurvivor[0]._activity == SurvivorActivity::Repairing );
    SW_REQUIRE_EQUAL( size_t{ 1 }, snapshot._listGenerator.size() );
    SW_EXPECT_NEAR( 0.5f, snapshot._listGenerator[0]._progress, kAsymmetricTolerance );
    SW_EXPECT_EQUAL( size_t{ 1 }, snapshot._listGateProgress.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, snapshot._listPalletState.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, snapshot._listWindowBlocked.size() );
    SW_EXPECT_EQUAL( -1, snapshot._killerCarrying );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 시작 전의 판도 왕복한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RoundTripBeforeStart )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    AsymmetricHorrorMatch restored;
    SW_REQUIRE_TRUE( restored.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
    SW_EXPECT_TRUE( StateBytesTestUtil::roundTrips( _match, restored ) );
    SW_EXPECT_EQUAL( 1, restored.getSurvivorCount() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 왕복: 갈고리, 들린 사람, 사물함, 수리 중인 판을 잇는다. 같은 시간을 흘리면 같은 판이다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RoundTripMidMatch )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 1 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 2 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 50.0f, 1.0f ) ) == 3 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addPallet( AsymmetricHorrorTestUtil::at( 70.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( AsymmetricHorrorTestUtil::at( 70.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addLocker( AsymmetricHorrorTestUtil::at( 50.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( AsymmetricHorrorTestUtil::at( 50.0f, 1.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addGate( AsymmetricHorrorTestUtil::at( 70.0f, 0.0f ) ) == 0 );
    _match.setHatchPosition( AsymmetricHorrorTestUtil::at( 70.0f, 0.0f ) );
    _match.start();
    downAtOrigin();
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Hit ); // 앞사람이 쓰러져 다음 사람이다
    advance( 1.2f );
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Downed );
    hangOnHook( 0 );
    SW_REQUIRE_TRUE( _match.pickUp( 1 ) );
    SW_REQUIRE_TRUE( _match.enterLocker( 2, 0 ) );
    SW_REQUIRE_TRUE( _match.startRepair( 3, 0 ) );
    advance( 0.3f );
    SW_REQUIRE_TRUE( survivor( 0 )._state == SurvivorState::Hooked );
    expectRoundTrip();
}

/**
 * @brief [AsymmetricHorrorRulesTest] 왕복: 치료 중이고 걷어차인 발전기가 있는 판을 잇는다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RoundTripWhileHealingAndRegressing )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 0.0f, 1.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    _match.start();
    _match.setKillerPosition( AsymmetricHorrorTestUtil::at( 0.0f, -2.0f ) );
    SW_REQUIRE_TRUE( _match.startRepair( 1, 0 ) );
    advance( 0.5f );
    _match.stopActivity( 1 );
    SW_REQUIRE_TRUE( _match.kickGenerator( 0 ) );
    SW_REQUIRE_TRUE( _match.killerAttack() == KillerAttackResult::Hit );
    SW_REQUIRE_TRUE( _match.startHeal( 1, 0 ) );
    advance( 0.2f );
    expectRoundTrip();
}

/**
 * @brief [AsymmetricHorrorRulesTest] 끝난 판도 왕복한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, RoundTripAfterEnd )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 1 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    _match.start();
    powerGates( 0 );
    SW_REQUIRE_TRUE( _match.startOpenGate( 0, 0 ) );
    advance( 1.2f );
    SW_REQUIRE_TRUE( _match.escape( 0 ) );
    expectRoundTrip();
}

/**
 * @brief [AsymmetricHorrorRulesTest] 시작하지 않았거나 살인마가 다른 판, 잘린 바이트는 읽지 않고 상태는 그대로이다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, ReadRefusesWrongHost )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( float3{} ) == 0 );
    _match.start();
    Archive written;
    _match.writeState( written );

    AsymmetricHorrorMatch uninitialized;
    Archive               reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( uninitialized.readState( reading ) );

    AsymmetricHorrorMatch other;
    SW_REQUIRE_TRUE( other.initialize( AsymmetricHorrorTestUtil::makeSettings( "nurse" ), &_catalog ) );
    Archive readingOther( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( other.readState( readingOther ) );
    SW_EXPECT_EQUAL( 0, other.getSurvivorCount() );

    AsymmetricHorrorMatch truncated;
    SW_REQUIRE_TRUE( truncated.initialize( AsymmetricHorrorTestUtil::makeSettings(), &_catalog ) );
    Archive readingTruncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated.readState( readingTruncated ) );
    SW_EXPECT_EQUAL( 0, truncated.getSurvivorCount() );
}

/**
 * @brief [AsymmetricHorrorRulesTest] 결함: 칸마다 범위만 보거나 아예 보지 않아, 맞지 않는 짝을 받았다. 들린 상태인데 살인마가 들지 않으면 내려오지 못하고, 없는 참가자는 판 결과에서 빠지며, 완료 수가 틀리면 전원이 어긋난다 — 모두 거절한다
 */
SW_TEST_CASE_F( AsymmetricHorrorRulesTest, ReadRefusesCraftedBytes )
{
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 90.0f, 0.0f ) ) == 0 );
    SW_REQUIRE_TRUE( _match.addSurvivor( AsymmetricHorrorTestUtil::at( 90.0f, 0.0f ) ) == 1 );
    SW_REQUIRE_TRUE( _match.addHook( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addPallet( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addLocker( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addWindow( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGenerator( float3{} ) == 0 );
    SW_REQUIRE_TRUE( _match.addGate( float3{} ) == 0 );
    _match.start();

    const AsymmetricFixtureBytes fixture( _match );
    const size_t                 size = fixture.getSize();
    const size_t                 base = fixture._stageBase;

    // 자리가 맞는지 정상 값으로 본다.
    SW_REQUIRE_EQUAL( 0, fixture.readAt<int32>( base ) );
    SW_REQUIRE_EQUAL( 1u, fixture.readAt<uint32>( base + 25 ) );                 // 갈고리 수
    SW_REQUIRE_EQUAL( 1u, fixture.readAt<uint32>( base + 41 ) );                 // 판자 수
    SW_REQUIRE_EQUAL( 1u, fixture.readAt<uint32>( base + 58 ) );                 // 사물함 수
    SW_REQUIRE_EQUAL( -1, fixture.readAt<int32>( base + 74 ) );                  // 사물함에 든 사람
    SW_REQUIRE_EQUAL( 1u, fixture.readAt<uint32>( base + 78 ) );                 // 창틀 수
    SW_REQUIRE_EQUAL( 1u, fixture.readAt<uint32>( base + 102 ) );                // 발전기 수
    SW_REQUIRE_EQUAL( -1, fixture.readAt<int32>( size - 13 ) );                  // 든 사람
    SW_REQUIRE_EQUAL( -1, fixture.readAt<int32>( size - 9 ) );                   // 부수는 판자
    SW_REQUIRE_EQUAL( 2, fixture.readAt<int32>( size - 5 ) );                    // 살인마 참가자
    SW_REQUIRE_EQUAL( 1, fixture.readAt<int32>( fixture.lastSurvivor( 9 ) ) );   // 생존자 참가자
    SW_REQUIRE_EQUAL( -1, fixture.readAt<int32>( fixture.lastSurvivor( 13 ) ) ); // 생존자 하던 일의 대상
    SW_REQUIRE_EQUAL( 0, fixture.readAt<int32>( fixture.lastSurvivor( 17 ) ) );  // 갈고리 단계
    SW_EXPECT_TRUE( fixture.readPatched<int32>( _catalog, base, 0 ) );           // 형식은 맞다

    // 머리와 판.
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base, 1 ) ); // 완료 수가 끝난 발전기 수와 다르다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base, -1 ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, base + 4, 2 ) );       // 전원 깃발
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, base + 6, SW_TRUE ) ); // 놓지 않은 해치가 열렸다
    SW_EXPECT_FALSE( fixture.readPatched<float32>( _catalog, base + 9, 5.0f ) );  // 시작하지 않은 붕괴가 돈다

    // 무대.
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, base + 57, 7 ) ); // 판자 상태
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base + 74, 5 ) ); // 없는 사람이 사물함에 있다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base + 74, 0 ) ); // 숨지 않은 사람이 사물함에 있다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base + 74, -2 ) );
    SW_EXPECT_FALSE( fixture.readPatched<float32>( _catalog, base + 94, -1.0f ) ); // 막힌 시간
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base + 98, -1 ) );      // 추격 넘기 수
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, base + 98, 2 ) );       // 막힘 수에 닿으면 0 으로 돌아간다

    // 생존자(뒤에서부터 플래그 셋, 하던 일, 상태, 참가자, 대상, 갈고리 단계).
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 1 ), 2 ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 4 ), 1 ) ); // 대상 없이 수리한다
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 4 ), 5 ) ); // 대상 없이 숨었다
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 4 ), 9 ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), 9 ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), static_cast<uint8>( SurvivorState::Injured ) ) ); // 체력이 가득한데 부상이다
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), static_cast<uint8>( SurvivorState::Dying ) ) );   // 쓰러지지 않았다
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), static_cast<uint8>( SurvivorState::Carried ) ) ); // 살인마가 들지 않았다
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), static_cast<uint8>( SurvivorState::Hooked ) ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, fixture.lastSurvivor( 5 ), static_cast<uint8>( SurvivorState::Sacrificed ) ) ); // 살아 있다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, fixture.lastSurvivor( 9 ), 99 ) );                                              // 없는 참가자
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, fixture.lastSurvivor( 9 ), 2 ) );                                               // 살인마 팀의 참가자
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, fixture.lastSurvivor( 13 ), 5 ) );                                              // 하는 일이 없는데 대상이 있다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, fixture.lastSurvivor( 17 ), -1 ) );                                             // 갈고리 단계
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, fixture.lastSurvivor( 17 ), 4 ) );
    SW_EXPECT_FALSE( fixture.readPatched<float32>( _catalog, fixture.lastSurvivor( 45 ), 2.0f ) );  // 몸부림 진행
    SW_EXPECT_FALSE( fixture.readPatched<float32>( _catalog, fixture.lastSurvivor( 37 ), -1.0f ) ); // 소음 반경
    SW_EXPECT_FALSE( fixture.readPatched<float32>( _catalog, fixture.lastSurvivor( 53 ), -1.0f ) ); // 갈고리 시간

    // 살인마.
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 13, 1 ) ); // 들린 상태가 아닌 사람을 든다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 13, 9 ) );
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 9, 0 ) ); // 서 있는 판자를 부순다
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 9, 9 ) );
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 5, 0 ) ); // 생존자 팀의 참가자
    SW_EXPECT_FALSE( fixture.readPatched<int32>( _catalog, size - 5, 99 ) );
    SW_EXPECT_FALSE( fixture.readPatched<uint8>( _catalog, size - 1, 2 ) );
}
