#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"
#include "Engine/Physics/Component/PhysicsComponent.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"
#include "GameFramework/Runtime/Gameplay/Zone/JumpEscapeReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ScreenReactionComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneModule.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneOccupantIndex.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneTracker.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

using namespace sw;

// ZoneTest — 지역 효과의 처리 길(진입 · 체류 · 이탈 · 조건 전환 · 경우 목록 · 식 값 · 출처), 읽기 검사(모르는 종류 · 매개변수 · 참조), 배치 덮어쓰기(nogpu, 물리 없음).

namespace
{
    /** @brief 어빌리티 컴포넌트를 가진 대상 하나와 시험 카탈로그입니다. */
    struct ZoneFixture
    {
        GameObjectManager _manager;
        AbilityCatalog    _catalog;
        AbilityComponent* _pHero{ nullptr };

        static inline const hashed_string kHealth{ "Health" };
        static inline const hashed_string kBurn{ "Burn" };
        static inline const hashed_string kFireResist{ "FireResist" };

        ZoneFixture()
        {
            _pHero                        = _manager.createGameObject( hashed_string( "Hero" ) )->addComponent<AbilityComponent>();
            unique_ptr<AttributeSet> pSet = make_unique<AttributeSet>();
            pSet->defineAttribute( kHealth, 100.0f );
            pSet->defineAttribute( hashed_string( "MaxHealth" ), 100.0f );
            pSet->defineAttribute( kBurn, 0.0f );
            pSet->defineAttribute( kFireResist, 0.25f );
            (void)_pHero->addAttributeSet( std::move( pSet ) );
            _pHero->setHealthAttributes( kHealth, hashed_string( "MaxHealth" ) );
            _pHero->setCatalog( &_catalog );

            AbilityEffectDef root;
            root._id             = hashed_string( "GE_Test_Root" );
            root._durationPolicy = EffectDurationPolicy::Infinite;
            root._grantedTags.addTag( "State.MoveBlocked.Rooted"_tag );
            (void)_catalog.addEffect( root );

            AbilityEffectDef burn;
            burn._id             = hashed_string( "GE_Test_Burn" );
            burn._durationPolicy = EffectDurationPolicy::Instant;
            AbilityEffectModifier modifier;
            modifier._attribute       = kBurn;
            modifier._op              = AttributeModOp::Add;
            modifier._magnitudeSource = EffectMagnitudeSource::SetByCaller;
            modifier._setByCallerName = hashed_string( "Data.Damage" );
            burn._listModifier.push_back( modifier );
            (void)_catalog.addEffect( burn );
        }

        GameObject& hero() { return *_pHero->getOwner(); }

        static ZoneModuleDef makeModule( const utf8* pName, const utf8* pKind, std::initializer_list<std::pair<const utf8*, const utf8*>> listParam, const utf8* pCondition = "" )
        {
            ZoneModuleDef module;
            module._name      = hashed_string( pName );
            module._kind      = hashed_string( pKind );
            module._condition = pCondition;
            for ( const auto& param : listParam )
            {
                ZoneParamDef def;
                def._name  = hashed_string( param.first );
                def._value = param.second;
                module._listParam.push_back( def );
            }
            return module;
        }

        static ZoneCaseDef makeCase( const utf8* pName, const utf8* pCondition, const utf8* pDo )
        {
            ZoneCaseDef caseDef;
            caseDef._name      = hashed_string( pName );
            caseDef._condition = pCondition;
            caseDef._do        = pDo;
            return caseDef;
        }

        /** @brief 대상을 한 프레임 안에 두고(위치 값 @p depth01) 처리합니다. */
        void runInside( ZoneTracker& tracker, const ZoneCompiledProfile& profile, const utf8* pSource, float32 depth01, float32 deltaTime )
        {
            ZoneSample sample;
            sample._zoneDepth01 = depth01;
            tracker.beginFrame();
            tracker.touch( hero(), sample );
            tracker.dispatchOverlapEvents( _manager, profile, ActionSource{ hashed_string( pSource ), GameObjectHandle{} }, deltaTime );
        }

        void leave( ZoneTracker& tracker, const ZoneCompiledProfile& profile, const utf8* pSource )
        {
            tracker.beginFrame();
            tracker.dispatchOverlapEvents( _manager, profile, ActionSource{ hashed_string( pSource ), GameObjectHandle{} }, 0.1f );
        }
    };
} // namespace

/** @brief [ZoneTest] 진입 할 일이 이펙트를 걸고(출처 volume:<이름>), 이탈 할 일이 그 출처의 것만 지운다 — 겹친 다른 영역의 효과는 남는다 */
SW_TEST_CASE( ZoneTest, EnterAndExitActionsKeepTheirSource )
{
    ZoneFixture fixture;
    ZoneProfile profile;
    profile._listModule.push_back( ZoneFixture::makeModule( "root", "Action", {
                                                                                  {"on",                                    "Enter"},
                                                                                  {"do", "ability.applyEffect(effect=GE_Test_Root)"}
    } ) );
    profile._listModule.push_back( ZoneFixture::makeModule( "release", "Action", {
                                                                                     {"on",                   "Exit"},
                                                                                     {"do", "ability.removeEffect()"}
    } ) );
    ZoneCompiledProfile compiled;
    SW_REQUIRE_TRUE( compiled.compile( profile, "test", &fixture._catalog ) );

    ZoneTracker swamp;
    ZoneTracker bog;
    fixture.runInside( swamp, compiled, "volume:Swamp", 1.0f, 0.1f );
    fixture.runInside( bog, compiled, "volume:Bog", 1.0f, 0.1f );
    SW_EXPECT_TRUE( MovementConstraint::read( fixture.hero() )._bMoveBlocked == SW_TRUE );
    vector<AbilityEffectLogEntry> listLog;
    fixture._pHero->collectEffectLog( listLog );
    SW_REQUIRE_TRUE( listLog.empty() == false );
    SW_EXPECT_TRUE( listLog[0]._sourceName == hashed_string( "volume:Swamp" ) );

    fixture.runInside( swamp, compiled, "volume:Swamp", 1.0f, 0.1f ); // 남아 있으면 다시 걸지 않는다(진입은 한 번)
    SW_EXPECT_EQUAL( 2u, fixture._pHero->getActiveEffectCount() );
    fixture.leave( swamp, compiled, "volume:Swamp" );
    SW_EXPECT_TRUE( MovementConstraint::read( fixture.hero() )._bMoveBlocked == SW_TRUE ); // 늪(Bog) 것은 남는다
    SW_EXPECT_TRUE( swamp.findOccupancy( fixture.hero().getHandle() ) == nullptr );
    fixture.leave( bog, compiled, "volume:Bog" );
    SW_EXPECT_FALSE( MovementConstraint::read( fixture.hero() )._bMoveBlocked == SW_TRUE );
}

/** @brief [ZoneTest] 조건 전환 — 안쪽으로 넘는 순간(지연 뒤) 한 번, 다시 나오거나 나가는 순간 한 번. 체류 주기는 간격마다 */
SW_TEST_CASE( ZoneTest, ConditionTransitionsFireOnceWithDelay )
{
    ZoneFixture fixture;
    ZoneProfile profile;
    profile._listModule.push_back( ZoneFixture::makeModule( "engulf", "Action", {
                                                                                    {   "on",                                   "Became"},
                                                                                    {"delay",                                        "1"},
                                                                                    {   "do", "ability.applyEffect(effect=GE_Test_Root)"}
    },
                                                            "zoneDepth01 >= 0.5" ) );
    profile._listModule.push_back( ZoneFixture::makeModule( "free", "Action", {
                                                                                  {"on",                                      "Lost"},
                                                                                  {"do", "ability.removeEffect(effect=GE_Test_Root)"}
    },
                                                            "zoneDepth01 >= 0.5" ) );
    profile._listModule.push_back( ZoneFixture::makeModule( "tick", "Action", {
                                                                                  {      "on",                                                                            "Stay"},
                                                                                  {"interval",                                                                             "0.5"},
                                                                                  {  "damage",                                                                               "1"},
                                                                                  {      "do", "ability.applyEffect(effect=GE_Test_Burn, setByCaller=Data.Damage, value=damage)"}
    } ) );
    ZoneCompiledProfile compiled;
    SW_REQUIRE_TRUE( compiled.compile( profile, "test", &fixture._catalog ) );
    ZoneTracker dune;
    const auto  rooted = [&fixture]()
    { return MovementConstraint::read( fixture.hero() )._bMoveBlocked == SW_TRUE; };

    fixture.runInside( dune, compiled, "volume:Dune", 0.2f, 0.25f ); // 바깥: 체류 첫 프레임에 한 번
    SW_EXPECT_FALSE( rooted() );
    SW_EXPECT_NEAR( 1.0f, fixture._pHero->getAttributeValue( ZoneFixture::kBurn ).value_or( -1.0f ), 1.0e-4f );
    fixture.runInside( dune, compiled, "volume:Dune", 0.8f, 0.5f ); // 안쪽 0.5 초 — 지연 1 초 전
    SW_EXPECT_FALSE( rooted() );
    fixture.runInside( dune, compiled, "volume:Dune", 0.8f, 0.5f ); // 1 초 — 한 번
    SW_EXPECT_TRUE( rooted() );
    fixture.runInside( dune, compiled, "volume:Dune", 0.2f, 0.5f ); // 다시 나오는 순간 해제
    SW_EXPECT_FALSE( rooted() );
    fixture.runInside( dune, compiled, "volume:Dune", 0.9f, 1.5f ); // 다시 넘고 지연을 채움
    SW_EXPECT_TRUE( rooted() );
    fixture.leave( dune, compiled, "volume:Dune" ); // 나가는 순간도 해제
    SW_EXPECT_FALSE( rooted() );
    // 체류 주기: 0.25 + 0.5 + 0.5 + 0.5 + 1.5 초를 0.5 초 간격으로 — 프레임마다 많아야 한 번(밀린 주기는 따라잡지 않는다).
    SW_EXPECT_NEAR( 5.0f, fixture._pHero->getAttributeValue( ZoneFixture::kBurn ).value_or( -1.0f ), 1.0e-4f );
}

/** @brief [ZoneTest] 경우 목록은 처음 맞는 하나만 하고, 수치 칸의 식은 대상 속성을 읽는다(저항 0.25 → 20 × 0.75 = 15) */
SW_TEST_CASE( ZoneTest, FirstMatchingCaseAndExpressionValues )
{
    ZoneFixture   fixture;
    ZoneProfile   profile;
    ZoneModuleDef burn = ZoneFixture::makeModule( "burn", "Action", {
                                                                        {    "on",                        "Enter"},
                                                                        {"damage", "20 * (1 - target.FireResist)"}
    } );
    burn._listCase.push_back( ZoneFixture::makeCase( "immune", "tag(State.Fireproof)", "" ) );
    burn._listCase.push_back( ZoneFixture::makeCase( "burn", "", "ability.applyEffect(effect=GE_Test_Burn, setByCaller=Data.Damage, value=damage)" ) );
    profile._listModule.push_back( burn );
    ZoneCompiledProfile compiled;
    SW_REQUIRE_TRUE( compiled.compile( profile, "test", &fixture._catalog ) );

    ZoneTracker lava;
    fixture.runInside( lava, compiled, "surface:Lava", 1.0f, 0.1f );
    SW_EXPECT_NEAR( 15.0f, fixture._pHero->getAttributeValue( ZoneFixture::kBurn ).value_or( -1.0f ), 1.0e-4f );

    fixture.leave( lava, compiled, "surface:Lava" );
    fixture._pHero->getOwnedTagCounts().updateCount( "State.Fireproof"_tag, 1 );
    fixture.runInside( lava, compiled, "surface:Lava", 1.0f, 0.1f ); // 화염 면역 — 첫 경우가 맞아 아무것도 하지 않는다
    SW_EXPECT_NEAR( 15.0f, fixture._pHero->getAttributeValue( ZoneFixture::kBurn ).value_or( -1.0f ), 1.0e-4f );
}

/** @brief [ZoneTest] 즉사는 무적을 존중하고(기본), 칸으로 넘을 수 있다 */
SW_TEST_CASE( ZoneTest, KillRespectsInvulnerabilityUnlessTold )
{
    ZoneFixture fixture;
    ZoneProfile profile;
    profile._listModule.push_back( ZoneFixture::makeModule( "kill", "Action", {
                                                                                  {"on",         "Enter"},
                                                                                  {"do", "health.kill()"}
    } ) );
    ZoneCompiledProfile respectful;
    SW_REQUIRE_TRUE( respectful.compile( profile, "test", &fixture._catalog ) );
    profile._listModule[0] = ZoneFixture::makeModule( "kill", "Action", {
                                                                            {"on",                                "Enter"},
                                                                            {"do", "health.kill(ignoreInvulnerable=true)"}
    } );
    ZoneCompiledProfile ruthless;
    SW_REQUIRE_TRUE( ruthless.compile( profile, "test", &fixture._catalog ) );

    fixture._pHero->getOwnedTagCounts().updateCount( AbilityComponent::getInvulnerableTag(), 1 );
    ZoneTracker pit;
    fixture.runInside( pit, respectful, "volume:Pit", 1.0f, 0.1f );
    SW_EXPECT_NEAR( 100.0f, fixture._pHero->getAttributeValue( ZoneFixture::kHealth ).value_or( -1.0f ), 1.0e-4f );
    ZoneTracker abyss;
    fixture.runInside( abyss, ruthless, "volume:Abyss", 1.0f, 0.1f );
    SW_EXPECT_NEAR( 0.0f, fixture._pHero->getAttributeValue( ZoneFixture::kHealth ).value_or( -1.0f ), 1.0e-4f );
    SW_EXPECT_TRUE( fixture._pHero->getHealthReading()._bDead == SW_TRUE );
}

/** @brief [ZoneTest] 읽기 검사 — 모르는 종류 · 매개변수 · 고르기 값 · 식, 카탈로그에 없는 이펙트는 로드 실패이고 프로필은 비어 있다 */
SW_TEST_CASE( ZoneTest, LoadRejectsWhatItCannotRun )
{
    ZoneFixture         fixture;
    ZoneCompiledProfile compiled;
    const auto          compileOne = [&]( const ZoneModuleDef& module )
    {
        ZoneProfile profile;
        profile._listModule.push_back( module );
        return compiled.compile( profile, "test", &fixture._catalog );
    };

    SW_EXPECT_LOG( "Zone.ModuleKindUnknown" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "a", "NoSuchKind", {} ) ) );
    SW_EXPECT_LOG( "Zone.ParamUnknown" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "b", "PhysicsField", {
                                                                                   { "strenght", "1" }
    } ) ) );
    SW_EXPECT_LOG( "Zone.ChoiceInvalid" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "c", "PhysicsField", {
                                                                                   { "direction", "Sideways" }
    } ) ) );
    SW_EXPECT_LOG( "ConditionExpr.Invalid" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "d", "PhysicsField", {
                                                                                   { "strength", "4 *" }
    } ) ) );
    SW_EXPECT_LOG( "AbilityActions.UnknownEffect" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "e", "Action", {
                                                                             { "do", "ability.applyEffect(effect=GE_Missing)" }
    } ) ) );
    SW_EXPECT_LOG( "Zone.ParamMissing" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "f", "Blackboard", {} ) ) );
    SW_EXPECT_TRUE( compiled.isEmpty() );

    SW_EXPECT_TRUE( compileOne( ZoneFixture::makeModule( "g", "PhysicsField", {
                                                                                  {   "direction",        "ToCenter"},
                                                                                  {    "strength", "6 * zoneDepth01"},
                                                                                  {"gravityScale",               "0"}
    } ) ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), compiled.getModules().size() );
}

/** @brief [ZoneTest] 표면 도장 모듈: 도장 칸(크기 · 깊이 · 메워지는 속도)이 빠지거나 규칙에 어긋나면(메워지지 않음 · 테두리 반쪽 · 모르는 모양 · 자리) 로드 실패, 식 깊이는 받는다 */
SW_TEST_CASE( ZoneTest, SurfaceStampModuleChecksTheStamp )
{
    ZoneFixture         fixture;
    ZoneCompiledProfile compiled;
    const auto          compileOne = [&]( const ZoneModuleDef& module )
    {
        ZoneProfile profile;
        profile._listModule.push_back( module );
        return compiled.compile( profile, "test", &fixture._catalog );
    };

    SW_EXPECT_LOG( "Zone.ParamMissing" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "a", "SurfaceStamp", {
                                                                                   { "size", "1,1,0"},
                                                                                   {"depth",  "0.06"}
    } ) ) );
    SW_EXPECT_LOG( "Surface.StampFillMissing" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "b", "SurfaceStamp", {
                                                                                   {     "size", "1,1,0"},
                                                                                   {    "depth",  "0.06"},
                                                                                   {"fillSpeed",     "0"}
    } ) ) );
    SW_EXPECT_LOG( "Surface.StampRimHalf" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "c", "SurfaceStamp", {
                                                                                   {     "size", "1,1,0"},
                                                                                   {    "depth",  "0.06"},
                                                                                   {"fillSpeed",  "0.02"},
                                                                                   {"rimHeight",  "0.03"}
    } ) ) );
    SW_EXPECT_LOG( "Zone.ChoiceInvalid" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "d", "SurfaceStamp", {
                                                                                   {    "shape", "Square"},
                                                                                   {     "size",  "1,1,0"},
                                                                                   {    "depth",   "0.06"},
                                                                                   {"fillSpeed",   "0.02"}
    } ) ) );
    SW_EXPECT_LOG( "Zone.ChoiceInvalid" );
    SW_EXPECT_FALSE( compileOne( ZoneFixture::makeModule( "e", "SurfaceStamp", {
                                                                                   {       "at",  "Feet"},
                                                                                   {     "size", "1,1,0"},
                                                                                   {    "depth",  "0.06"},
                                                                                   {"fillSpeed",  "0.02"}
    } ) ) );

    // 안쪽일수록 깊은 깔때기 — 깊이는 위치 값의 식이다(대상 없는 검사 문맥에서는 0 이라 빈 도장으로 지난다).
    SW_EXPECT_TRUE( compileOne( ZoneFixture::makeModule( "funnel", "SurfaceStamp", {
                                                                                       {    "shape",             "Funnel"},
                                                                                       {       "at",             "Center"},
                                                                                       {     "size",              "2,0,0"},
                                                                                       {    "depth", "0.12 * zoneDepth01"},
                                                                                       {"fillSpeed",               "0.05"},
                                                                                       {"spinSpeed",                "1.5"},
                                                                                       { "armCount",                  "3"}
    } ) ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), compiled.getModules().size() );
}

/** @brief [ZoneTest] 배치 덮어쓰기 — 이름으로 칸 하나만 고치고, 빼고, 더한다. 없는 이름을 빼면 오류다 */
SW_TEST_CASE( ZoneTest, PlacementOverridesWorkByName )
{
    ZoneProfile preset;
    preset._listModule.push_back( ZoneFixture::makeModule( "sink", "Action", {
                                                                                 {      "on",                "Stay"},
                                                                                 {"interval",                "0.25"},
                                                                                 {      "do", "flag.add(flag=sunk)"}
    } ) );
    preset._listModule.push_back( ZoneFixture::makeModule( "npcAvoid", "Blackboard", {
                                                                                         { "key", "InDanger" }
    } ) );

    vector<ZoneModuleDef> listOverride;
    listOverride.push_back( ZoneFixture::makeModule( "sink", "", {
                                                                     { "interval", "1" }
    } ) ); // 느린 사구: 칸 하나만
    ZoneModuleDef removeAvoid;
    removeAvoid._name = hashed_string( "npcAvoid" );
    removeAvoid._op   = ZoneOverrideOp::Remove;
    listOverride.push_back( removeAvoid );
    listOverride.push_back( ZoneFixture::makeModule( "wet", "SurfaceChannel", {
                                                                                  {"channel", "Wet"},
                                                                                  {   "rate", "0.5"}
    } ) );

    ZoneProfile placed = preset;
    SW_REQUIRE_TRUE( placed.applyOverrides( listOverride, "test" ) );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), placed._listModule.size() );
    const ZoneModuleDef* pSink = placed.findModule( hashed_string( "sink" ) );
    SW_REQUIRE_TRUE( pSink != nullptr );
    SW_EXPECT_TRUE( pSink->_kind == hashed_string( "Action" ) );                               // 적지 않은 칸은 물려받는다
    SW_EXPECT_EQUAL( string( "1" ), pSink->findParam( hashed_string( "interval" ) )->_value ); // 적은 칸만 바뀐다
    SW_EXPECT_EQUAL( string( "Stay" ), pSink->findParam( hashed_string( "on" ) )->_value );
    SW_EXPECT_TRUE( placed.findModule( hashed_string( "npcAvoid" ) ) == nullptr );
    SW_EXPECT_TRUE( placed.findModule( hashed_string( "wet" ) ) != nullptr );

    ZoneProfile           broken = preset;
    vector<ZoneModuleDef> listBad{ removeAvoid, removeAvoid };
    SW_EXPECT_LOG( "Zone.OverrideModuleMissing" );
    SW_EXPECT_FALSE( broken.applyOverrides( listBad, "test" ) ); // 두 번째 빼기는 없는 이름이다
}

/** @brief [ZoneTest] 볼륨은 씬을 돌지 않고 씬 공간 색인의 물리 층을 읽는다 — 첫 볼륨이 층의 사용자가 되면 층을 맞추고, 마지막 볼륨이 떠나면 놓는다 */
SW_TEST_CASE( ZoneTest, OccupantIndexReadsSceneSpatialGrid )
{
    GameObjectManager manager;
    SceneSpatialGrid& grid   = manager.getSubsystem<SceneSpatialGrid>();
    GameObject*       pSwamp = manager.createGameObject( hashed_string( "Swamp" ) );
    (void)pSwamp->addComponent<SceneComponent>();
    pSwamp->addComponent<ZoneVolumeComponent>()->configure( "", ZoneVolumeShape::Box, float3{ 2.0f, 1.0f, 3.0f } );
    GameObject* pGas = manager.createGameObject( hashed_string( "Gas" ) );
    (void)pGas->addComponent<SceneComponent>();
    ZoneVolumeComponent* pGasVolume = pGas->addComponent<ZoneVolumeComponent>();
    pGasVolume->configure( "", ZoneVolumeShape::Sphere, float3{ 5.0f, 1.0f, 1.0f } );
    SW_EXPECT_NEAR( 5.0f, pGasVolume->getHorizontalReach(), 1.0e-5f ); // 구는 반지름 x 다 — z 반 크기(1)를 쓰지 않는다

    vector<Component*>            listKey;
    vector<ZoneOccupantCandidate> listCandidate;
    ZoneOccupantIndex::queryCandidates( manager, 6.0f, -2.0f, -3.0f, 2.0f, 3.0f, listKey, listCandidate );
    SW_EXPECT_TRUE( listCandidate.empty() ); // 물리 개체가 없는 씬
    SW_EXPECT_TRUE( grid.isActive( PhysicsComponent::getCharacterSpatialLayer() ) && grid.isActive( PhysicsComponent::getBodySpatialLayer() ) );

    manager.destroyGameObject( pSwamp );
    manager.destroyGameObject( pGas );
    manager.update( 1.0f / 60.0f ); // 지운 볼륨이 해제되며 사용자를 뺀다
    SW_EXPECT_FALSE( grid.isActive( PhysicsComponent::getCharacterSpatialLayer() ) );
}

/** @brief [ZoneTest] 지역 컴포넌트는 틱하지 않는다 — 첫 지역 컴포넌트가 지역 프레임 시스템을 붙이고, 시스템이 켜진 것을 등록 순서로 한 번에 돈다 */
SW_TEST_CASE( ZoneTest, ZoneSystemRunsRegisteredZoneComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( ZoneSystem::find( manager ) == nullptr );
    GameObject*                  pWalker = manager.createGameObject( hashed_string( "Walker" ) );
    JumpEscapeReactionComponent* pEscape = pWalker->addComponent<JumpEscapeReactionComponent>();
    SW_REQUIRE_TRUE( pEscape != nullptr );
    SW_EXPECT_FALSE( pEscape->canEverTick() ); // 프레임 일은 시스템이 한다
    ZoneSystem* pSystem = ZoneSystem::find( manager );
    SW_REQUIRE_TRUE( pSystem != nullptr );          // 첫 지역 컴포넌트가 등록될 때 붙는다
    pSystem->runFramePass( manager, 1.0f / 60.0f ); // 컨트롤러 · 어빌리티가 없는 몸 — 할 일 없이 지난다
    SW_EXPECT_EQUAL( 0u, pEscape->getEscapeCount() );
}

/** @brief [ZoneTest] 화면 반응: 태그(State.Poisoned)가 있으면 페이드 인 시간(0.5 초) 동안 가중치가 1 로 오르고, 태그가 사라지면 페이드 아웃(1 초) 동안 0 으로 내린다. 시간이 0 이면 그대로다 */
SW_TEST_CASE( ZoneTest, ScreenReactionFadesWithTag )
{
    ZoneFixture              fixture;
    GameObject*              pHero   = fixture._pHero->getOwner();
    ScreenReactionComponent* pScreen = pHero->addComponent<ScreenReactionComponent>();
    SW_REQUIRE_TRUE( pScreen != nullptr );
    pScreen->setTag( "State.Poisoned"_tag ); // 태그는 데이터가 적는 칸이다(기본값 없음)
    ZoneSystem* pSystem = ZoneSystem::find( fixture._manager );
    SW_REQUIRE_TRUE( pSystem != nullptr ); // 화면 반응도 지역 프레임 시스템을 붙인다
    const float32 step = 0.1f;

    pSystem->runFramePass( fixture._manager, step );
    SW_EXPECT_NEAR( 0.0f, pScreen->getWeight(), 1.0e-6f ); // 태그 없음

    fixture._pHero->getOwnedTagCounts().updateCount( "State.Poisoned"_tag, 1 );
    pSystem->runFramePass( fixture._manager, step );
    SW_EXPECT_NEAR( 0.2f, pScreen->getWeight(), 1.0e-5f ); // 0.1 / 0.5
    pSystem->runFramePass( fixture._manager, 0.0f );
    SW_EXPECT_NEAR( 0.2f, pScreen->getWeight(), 1.0e-5f ); // 일시정지 — 옮기지 않는다
    for ( int32 index = 0; index < 10; ++index )
        pSystem->runFramePass( fixture._manager, step );
    SW_EXPECT_NEAR( 1.0f, pScreen->getWeight(), 1.0e-6f );

    fixture._pHero->getOwnedTagCounts().updateCount( "State.Poisoned"_tag, -1 );
    pSystem->runFramePass( fixture._manager, step );
    SW_EXPECT_NEAR( 0.9f, pScreen->getWeight(), 1.0e-5f ); // 0.1 / 1
    for ( int32 index = 0; index < 20; ++index )
        pSystem->runFramePass( fixture._manager, step );
    SW_EXPECT_NEAR( 0.0f, pScreen->getWeight(), 1.0e-6f );
}

// 인스펙터의 매개변수 덮어쓰기 · 되돌리기 — 물려받은 값을 고치면 합치기 항목이 생기고, 그 매개변수를 되돌려 항목이 비면 항목째 사라진다(빈 "바꿈" 줄을 남기지 않는다).
SW_TEST_CASE( ZoneOverrideTest, ParamOverrideAndRevertLeaveNoEmptyEntry )
{
    sw::vector<sw::ZoneModuleDef> listOverride;
    const sw::hashed_string       kModule{ "Burn" };
    const sw::hashed_string       kDamage{ "damage" };
    SW_REQUIRE_TRUE( sw::ZoneModuleDef::setOverrideParam( listOverride, kModule, kDamage, "12" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listOverride.size() );
    SW_EXPECT_TRUE( listOverride[0]._op == sw::ZoneOverrideOp::Merge );
    SW_REQUIRE_NOT_NULL( listOverride[0].findParam( kDamage ) );
    SW_EXPECT_EQUAL( sw::string( "12" ), listOverride[0].findParam( kDamage )->_value );

    SW_REQUIRE_TRUE( sw::ZoneModuleDef::setOverrideParam( listOverride, kModule, kDamage, "20 * immersion" ) ); // 같은 매개변수는 고친다
    SW_EXPECT_EQUAL( size_t( 1 ), listOverride[0]._listParam.size() );

    SW_EXPECT_TRUE( sw::ZoneModuleDef::revertOverrideParam( listOverride, kModule, kDamage ) );
    SW_EXPECT_TRUE( listOverride.empty() );
    SW_EXPECT_FALSE( sw::ZoneModuleDef::revertOverrideParam( listOverride, kModule, kDamage ) ); // 지울 것이 없다
}

// 매개변수를 되돌려도 다른 칸(조건)을 적은 항목은 남고, 통째로 바꾸기 항목에는 매개변수 하나를 얹지 않는다.
SW_TEST_CASE( ZoneOverrideTest, RevertKeepsOtherFieldsAndSkipsReplace )
{
    sw::vector<sw::ZoneModuleDef> listOverride;
    const sw::hashed_string       kModule{ "Sink" };
    SW_REQUIRE_TRUE( sw::ZoneModuleDef::setOverrideParam( listOverride, kModule, sw::hashed_string( "speed" ), "0.5" ) );
    listOverride[0]._condition = "timeInside > 2";
    SW_EXPECT_TRUE( sw::ZoneModuleDef::revertOverrideParam( listOverride, kModule, sw::hashed_string( "speed" ) ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listOverride.size() ); // 조건이 남아 항목은 산다

    listOverride[0]._op = sw::ZoneOverrideOp::Replace;
    SW_EXPECT_FALSE( sw::ZoneModuleDef::setOverrideParam( listOverride, kModule, sw::hashed_string( "speed" ), "1" ) );
    SW_EXPECT_TRUE( sw::ZoneModuleDef::revertOverrideModule( listOverride, kModule ) );
    SW_EXPECT_TRUE( listOverride.empty() );
}
