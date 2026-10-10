#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Audio/NullAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestFramework.h"

// 사용자 설정(옵션 메뉴 백엔드) — 스키마 검사 · 적용기 등록부 · 품질 프리셋 · 사용자 파일 · 적용/되돌리기 · 확인 카운트다운 · 키 바인딩 · 언어 · 접근성.

// 설정 대상은 등록된 전역 변수여야 한다 — Shipping 시험에서도 표에 있도록 남긴다(빠진 테스트 변수는 등록되지 않는다).
SW_TEST_GLOBAL_VARIABLE_SHIPPED( float32, gv_userSettingsTestFloat, 1.0f, "UserSettingsTest 전용 실수 대상" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_userSettingsTestInt, 0, "UserSettingsTest 전용 정수 대상" );
SW_TEST_GLOBAL_VARIABLE_SHIPPED( bool, gv_userSettingsTestBool, false, "UserSettingsTest 전용 불리언 대상" );

namespace
{
    struct UserSettingsTestInternal
    {
        /** @brief 시험 스키마 — 카테고리 둘, 전역 변수 대상 셋, 적용기 대상 하나, 품질 묶음 하나, 확인 카운트다운 하나. */
        static constexpr const utf8* kSchemaXml = R"(
<UserSettingsSchema version="1">
    <Category id="video" text="t.video"/>
    <Category id="sound" text="t.sound"/>
    <Setting id="video.mode" category="video" type="enum" default="a" confirmSeconds="10" target="gv:gv_userSettingsTestInt">
        <Option value="a"/>
        <Option value="b"/>
        <Option value="c" targetValue="7"/>
    </Setting>
    <Setting id="video.quality" category="video" type="enum" default="high">
        <Option value="low"/>
        <Option value="high"/>
        <Option value="custom"/>
    </Setting>
    <Setting id="video.scale" category="video" type="float" default="1" min="0.5" max="1" step="0.25" target="gv:gv_userSettingsTestFloat"/>
    <Setting id="video.shadows" category="video" type="bool" default="true" target="gv:gv_userSettingsTestBool" enabledWhen="video.mode!=c"/>
    <Setting id="sound.volume" category="sound" type="float" default="1" min="0" max="1" apply="immediate" target="applier:audio.busVolume" param="voice"/>
    <Setting id="sound.restart" category="sound" type="int" default="2" min="0" max="4" apply="restart"/>
    <Scalability setting="video.quality" custom="custom" autoDetectFallback="low">
        <Preset name="low">
            <Value setting="video.scale" value="0.5"/>
            <Value setting="video.shadows" value="false"/>
        </Preset>
        <Preset name="high">
            <Value setting="video.scale" value="1"/>
            <Value setting="video.shadows" value="true"/>
        </Preset>
        <AutoDetect preset="high" minCores="8" minMemoryMb="8000"/>
    </Scalability>
</UserSettingsSchema>)";

        /** @brief 엔진 대상(전역 변수 표 · 오디오)을 걸고 시험 스키마를 읽은 매니저를 세웁니다. */
        static bool initializeWithSchema( sw::UserSettingsManager& outSettings, sw::IAudioSystem* pAudioSystem = nullptr )
        {
            resetTestVariables();
            sw::UserSettingsTargets targets;
            targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
            targets._pAudioSystem           = pAudioSystem;
            outSettings.initialize( targets );
            if ( outSettings.loadSchemaFromXmlText( kSchemaXml, "test.settings.xml" ) == false )
                return false;
            outSettings.reapplyAll();
            return true;
        }

        static void resetTestVariables()
        {
            gv_userSettingsTestFloat = 1.0f;
            gv_userSettingsTestInt   = 0;
            gv_userSettingsTestBool  = false;
        }

        /** @brief 엔진 전역 변수를 이름으로 읽습니다 — Engine.dll 밖(시험 · 게임 모듈)은 변수를 링크하지 않고 표에서 찾는다. */
        static const sw::GlobalVariableInfo& findVariable( const utf8* pName )
        {
            static const sw::GlobalVariableInfo kMissing{};
            const sw::GlobalVariableInfo*       pVariable = sw::engine::getGlobalVariableManager().findVariable( pName );
            return pVariable != nullptr ? *pVariable : kMissing;
        }

        static sw::string makeTempPath( sw::string_view fileName ) { return sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), fileName ); }

        /** @brief 매니저 하나에 스키마 하나를 읽혀 본 결과입니다(오류는 의도한 것). */
        static bool loadsSchema( const utf8* pXml )
        {
            sw::UserSettingsManager settings;
            sw::UserSettingsTargets targets;
            targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
            settings.initialize( targets );
            SW_TEST_DEFENSIVE_SCOPE( "a broken user settings schema is a load error" );
            return settings.loadSchemaFromXmlText( pXml, "broken.settings.xml" );
        }
    };
} // namespace

/**
 * @brief [UserSettingsTest] 명령줄로 준 전역 변수는 기동 적용(reapplyAll)이 덮지 않는다 — 메뉴 적용은 덮는다
 * @details 실행 한 번의 값(명령줄)이 저장된 값(사용자 설정 · 스키마 기본값)을 이긴다. 화면 설정(`-W` · `-vsync`)과 같은 순서다(언리얼 CVar 도 명령줄이 이긴다).
 */
SW_TEST_CASE( UserSettingsTest, CommandLineGlobalVariableWinsAtStartup )
{
    sw::CommandLineManager commandLine;
    commandLine.initialize();
    utf8* argv[] = {
        const_cast<utf8*>( "TestApp.exe" ),
        const_cast<utf8*>( "-gv_userSettingsTestFloat=0.75" ),
    };
    commandLine.parse( 2, argv );

    UserSettingsTestInternal::resetTestVariables();
    gv_userSettingsTestFloat = 0.75f; // 엔진 기동은 명령줄을 전역 변수에 먼저 넣는다
    sw::UserSettingsManager settings;
    sw::UserSettingsTargets targets;
    targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
    targets._pCommandLineManager    = &commandLine;
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( UserSettingsTestInternal::kSchemaXml, "test.settings.xml" ) );
    settings.reapplyAll();

    // 1) 기동 적용은 명령줄 값을 지킨다(스키마 기본값 1 로 덮지 않는다). 명령줄에 없는 변수는 그대로 적용된다.
    SW_EXPECT_NEAR_EQUAL( 0.75f, gv_userSettingsTestFloat, 0.0001f );
    SW_EXPECT_TRUE( gv_userSettingsTestBool );

    // 2) 메뉴에서 고른 값은 덮는다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.scale", "0.5" ) == sw::UserSettingSetResult::Accepted );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_NEAR_EQUAL( 0.5f, gv_userSettingsTestFloat, 0.0001f );
    UserSettingsTestInternal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 시험 스키마가 검사를 통과하고, 기동 적용이 기본값을 전역 변수 대상에 넣는다
 */
SW_TEST_CASE( UserSettingsTest, SchemaLoadsAndStartupAppliesDefaults )
{
    sw::UserSettingsManager settings;
    SW_ASSERT_TRUE( UserSettingsTestInternal::initializeWithSchema( settings ) );

    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( settings.getCategories().size() ) );
    sw::vector<const sw::UserSettingDef*> listSetting;
    settings.collectSettings( "video", listSetting );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( listSetting.size() ) );
    SW_EXPECT_STREQ( "video.mode", listSetting.front()->_id.c_str() );
    SW_EXPECT_STREQ( "a", settings.getValue( "video.mode" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, gv_userSettingsTestFloat, 0.0001f );
    SW_EXPECT_TRUE( gv_userSettingsTestBool );
    SW_EXPECT_EQUAL( 0, gv_userSettingsTestInt );
    UserSettingsTestInternal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 모르는 이름 · 틀린 값은 스키마 로드 오류다 — 원소 · 속성 · 타입 · 대상 · 의존 · 품질 묶음 · 범위 밖 기본값
 */
SW_TEST_CASE( UserSettingsTest, SchemaRejectsUnknownNamesAndInvalidValues )
{
    using Internal = UserSettingsTestInternal;
    // 비교 기준: 올바른 최소 스키마는 읽힌다.
    SW_EXPECT_TRUE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true"/></UserSettingsSchema>)" ) );

    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true" colour="red"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Widget id="c.x"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="double" default="1"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="nowhere" type="bool" default="true"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="float" default="3" min="0" max="1"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="enum" default="z"><Option value="a"/></Setting></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true" target="applier:no.such"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true" target="gv:gv_noSuchVariable"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true" target="gv:gv_userSettingsTestFloat"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true" enabledWhen="c.y=true"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="bool" default="true"/><Setting id="c.x" category="c" type="bool" default="true"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/>
        <Setting id="c.q" category="c" type="enum" default="low"><Option value="low"/><Option value="high"/><Option value="custom"/></Setting>
        <Scalability setting="c.q" custom="custom"><Preset name="low"/></Scalability></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="keyBinding" default="Key.Space"/></UserSettingsSchema>)" ) );
    SW_EXPECT_FALSE( Internal::loadsSchema( R"(<UserSettingsSchema version="1"><Category id="c"/><Setting id="c.x" category="c" type="enum" default="windowed" target="applier:display.windowMode"><Option value="windowed"/><Option value="exclusive"/></Setting></UserSettingsSchema>)" ) );
}

/**
 * @brief [UserSettingsTest] 적용기는 데이터가 이름으로 고른다 — 게임이 올린 적용기가 값 · 인자를 받고, 내리면 그 이름을 쓰는 스키마는 로드 오류다
 */
SW_TEST_CASE( UserSettingsTest, ApplierRegistryDispatchesByName )
{
    struct Recorder
    {
        sw::string _value;
        sw::string _param;
        uint32     _callCount{ 0 };
        bool       record( const sw::UserSettingApplyContext& context )
        {
            _value = sw::string( context._value );
            _param = sw::string( context._param );
            ++_callCount;
            return true;
        }
    };
    Recorder recorder;

    sw::UserSettingsManager settings;
    settings.initialize( sw::UserSettingsTargets{} );
    settings.getRegistry().registerApplier( "game.record", SW_DELEGATE_METHOD( sw::UserSettingApplierDelegate, &Recorder::record, &recorder ) );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="g"/>
        <Setting id="g.level" category="g" type="int" default="3" min="1" max="9" target="applier:game.record" param="slot2"/></UserSettingsSchema>)",
                                                    "game.settings.xml" ) );

    settings.reapplyAll();
    SW_EXPECT_EQUAL( 1u, recorder._callCount );
    SW_EXPECT_STREQ( "3", recorder._value );
    SW_EXPECT_STREQ( "slot2", recorder._param );

    // 범위 밖은 맞춰 넣고(Clamped), 적용 때 적용기가 받는다.
    SW_EXPECT_TRUE( settings.setPendingIntValue( "g.level", 42 ) == sw::UserSettingSetResult::Clamped );
    SW_EXPECT_EQUAL( 1u, recorder._callCount );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_EQUAL( 2u, recorder._callCount );
    SW_EXPECT_STREQ( "9", recorder._value );

    // 같은 이름이 등록부에 없으면 그 이름을 쓰는 스키마는 읽히지 않는다.
    settings.getRegistry().unregisterApplier( "game.record" );
    SW_TEST_DEFENSIVE_SCOPE( "an applier name nobody registered" );
    SW_EXPECT_FALSE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="h"/>
        <Setting id="h.x" category="h" type="bool" default="true" target="applier:game.record"/></UserSettingsSchema>)",
                                                     "game2.settings.xml" ) );
}

/**
 * @brief [UserSettingsTest] 대상이 거절한 적용은 결과의 수로 세고 어느 설정인지 경고로 남긴다
 * @details 메뉴 · 에디터는 `_failedCount` 수만 받는다 — 어느 설정이 왜 지금 실행에 닿지 않았는지는 로그가 알려야 한다.
 */
SW_TEST_CASE( UserSettingsTest, RejectedApplyIsCountedAndReported )
{
    struct Refuser
    {
        bool refuse( const sw::UserSettingApplyContext& /*context*/ ) { return false; }
    };
    Refuser refuser;

    sw::UserSettingsManager settings;
    settings.initialize( sw::UserSettingsTargets{} );
    settings.getRegistry().registerApplier( "game.refuse", SW_DELEGATE_METHOD( sw::UserSettingApplierDelegate, &Refuser::refuse, &refuser ) );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="g"/>
        <Setting id="g.refused" category="g" type="int" default="3" min="1" max="9" target="applier:game.refuse"/></UserSettingsSchema>)",
                                                    "refuse.settings.xml" ) );
    SW_EXPECT_TRUE( settings.setPendingIntValue( "g.refused", 5 ) == sw::UserSettingSetResult::Accepted );

    test::ScopedLogCollector    collector;
    sw::UserSettingsApplyResult result;
    {
        SW_TEST_DEFENSIVE_SCOPE( "an applier that refuses the value" );
        result = settings.applyPending();
    }
    SW_EXPECT_EQUAL( 1u, result._failedCount );
    SW_EXPECT_EQUAL( 0u, result._appliedCount );
    SW_EXPECT_TRUE_MSG( collector.countContaining( "g.refused" ) > 0, collector.joined().c_str() );
    settings.getRegistry().unregisterApplier( "game.refuse" );
}

/**
 * @brief [UserSettingsTest] 품질 프리셋을 고르면 묶인 값이 따라가고, 묶인 값 하나를 바꾸면 Custom, 되돌리면 다시 그 프리셋이다 — 자동 선택은 사양 규칙대로
 */
SW_TEST_CASE( UserSettingsTest, ScalabilityPresetAndCustomDetection )
{
    sw::UserSettingsManager settings;
    SW_ASSERT_TRUE( UserSettingsTestInternal::initializeWithSchema( settings ) );

    SW_EXPECT_TRUE( settings.setPendingValue( "video.quality", "low" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_STREQ( "0.5", settings.getValue( "video.scale" ) );
    SW_EXPECT_STREQ( "false", settings.getValue( "video.shadows" ) );

    // 묶인 값 하나가 프리셋과 다르면 묶음은 Custom 이다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.scale", "0.75" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_STREQ( "custom", settings.getValue( "video.quality" ) );
    // 프리셋 값으로 되돌리면 다시 그 프리셋 이름이다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.scale", "0.5" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_STREQ( "low", settings.getValue( "video.quality" ) );

    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_NEAR_EQUAL( 0.5f, gv_userSettingsTestFloat, 0.0001f );
    SW_EXPECT_FALSE( gv_userSettingsTestBool );

    // 자동 선택: 규칙을 넘으면 그 프리셋, 아니면 fallback.
    sw::HardwareProbeResult strong;
    strong._logicalCoreCount = 16;
    strong._systemMemoryMb   = 32000;
    sw::HardwareProbeResult weak;
    weak._logicalCoreCount = 2;
    weak._systemMemoryMb   = 4000;
    SW_EXPECT_STREQ( "high", settings.detectScalabilityPreset( strong ).c_str() );
    SW_EXPECT_STREQ( "low", settings.detectScalabilityPreset( weak ).c_str() );
    SW_EXPECT_TRUE( settings.applyAutoDetectedPreset( strong ) );
    SW_EXPECT_STREQ( "high", settings.getAppliedValue( "video.quality" ) );
    SW_EXPECT_STREQ( "1", settings.getAppliedValue( "video.scale" ) );
    UserSettingsTestInternal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 사용자 파일 왕복 — 기본값과 다른 값만 쓰고, 새 매니저가 읽으면 같은 값이다
 */
SW_TEST_CASE( UserSettingsTest, UserFileRoundTripKeepsOnlyChangedValues )
{
    using Internal        = UserSettingsTestInternal;
    const sw::string path = Internal::makeTempPath( "sw_usersettingstest_roundtrip.json" );
    {
        sw::UserSettingsManager settings;
        SW_ASSERT_TRUE( Internal::initializeWithSchema( settings ) );
        settings.setUserFilePath( path );
        SW_EXPECT_TRUE( settings.setPendingValue( "video.mode", "b" ) == sw::UserSettingSetResult::Accepted );
        SW_EXPECT_TRUE( settings.setPendingValue( "video.scale", "0.75" ) == sw::UserSettingSetResult::Accepted );
        const sw::UserSettingsApplyResult result = settings.applyPending();
        SW_EXPECT_TRUE( result._bSaved );
        settings.confirmChanges();

        const sw::string json = settings.makeUserJson();
        SW_EXPECT_TRUE( json.find( "video.mode" ) != sw::string::npos );
        SW_EXPECT_TRUE( json.find( "video.scale" ) != sw::string::npos );
        // 기본값 그대로인 설정은 파일에 없다 — 다음 판에서 기본값이 바뀌면 그대로 따라간다.
        SW_EXPECT_TRUE( json.find( "sound.volume" ) == sw::string::npos );
    }
    {
        sw::UserSettingsManager settings;
        SW_ASSERT_TRUE( Internal::initializeWithSchema( settings ) );
        SW_ASSERT_TRUE( settings.loadUserFile( path ) );
        SW_EXPECT_TRUE( settings.hasLoadedUserFile() );
        SW_EXPECT_STREQ( "b", settings.getAppliedValue( "video.mode" ) );
        SW_EXPECT_STREQ( "0.75", settings.getAppliedValue( "video.scale" ) );
        SW_EXPECT_STREQ( "custom", settings.getAppliedValue( "video.quality" ) );
        settings.reapplyAll();
        SW_EXPECT_NEAR_EQUAL( 0.75f, gv_userSettingsTestFloat, 0.0001f );
        SW_EXPECT_EQUAL( 1, gv_userSettingsTestInt );
    }
    SW_EXPECT_TRUE( sw::FileUtil::removeFile( path ) );
    // 파일이 없으면 첫 실행이다 — 오류가 아니라 false.
    sw::UserSettingsManager fresh;
    SW_ASSERT_TRUE( Internal::initializeWithSchema( fresh ) );
    SW_EXPECT_FALSE( fresh.loadUserFile( path ) );
    Internal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 옛 판 파일은 버전 단계로 올리고, 모르는 키는 경고와 함께 버리고, 범위 밖 값은 맞춰 넣는다
 */
SW_TEST_CASE( UserSettingsTest, UserFileUpgradeDropsUnknownAndClamps )
{
    sw::UserSettingsManager settings;
    settings.initialize( sw::UserSettingsTargets{} );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="3"><Category id="a"/>
        <Setting id="a.volume" category="a" type="float" default="1" min="0" max="1"/>
        <Setting id="a.mode" category="a" type="enum" default="windowed"><Option value="windowed"/><Option value="borderless"/></Setting>
        <Setting id="a.gamma" category="a" type="float" default="1" min="0.5" max="2"/>
        <Upgrade version="1" op="rename" key="a.masterVolume" to="a.volume"/>
        <Upgrade version="1" op="scale" key="a.volume" scale="0.01"/>
        <Upgrade version="2" op="mapValue" key="a.mode" valueFrom="fullscreen" valueTo="borderless"/>
        <Upgrade version="2" op="remove" key="a.legacy"/></UserSettingsSchema>)",
                                                    "upgrade.settings.xml" ) );

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a version 1 player file with unknown and out-of-range values" );
        SW_ASSERT_TRUE( settings.loadUserJson( R"({ "version": 1, "values": { "a.masterVolume": 50, "a.mode": "fullscreen", "a.legacy": true,
                                                    "a.removedLongAgo": 3, "a.gamma": 9 } })",
                                               "old.json" ) );
    }
    SW_EXPECT_STREQ( "0.5", settings.getAppliedValue( "a.volume" ) );
    SW_EXPECT_STREQ( "borderless", settings.getAppliedValue( "a.mode" ) );
    SW_EXPECT_STREQ( "2", settings.getAppliedValue( "a.gamma" ) );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "dropped unknown key 'a.removedLongAgo'" ) );
    SW_EXPECT_EQUAL( 0u, logs.countContaining( "a.legacy" ) );
    SW_EXPECT_EQUAL( 1u, logs.countContaining( "'a.gamma' = '9' is out of range" ) );

    // 쓰는 파일은 지금 판이다.
    SW_EXPECT_TRUE( settings.makeUserJson().find( "\"version\": 3" ) != sw::string::npos );
}

/**
 * @brief [UserSettingsTest] 보류 · 적용 · 되돌리기 · 카테고리 기본값 — Immediate 는 보류 즉시 닿고 되돌리면 돌아오며, 나머지는 적용 때만 닿는다
 */
SW_TEST_CASE( UserSettingsTest, ApplyRevertAndResetCategory )
{
    sw::NullAudioSystem     audio;
    sw::UserSettingsManager settings;
    SW_ASSERT_TRUE( UserSettingsTestInternal::initializeWithSchema( settings, &audio ) );

    // Immediate: 볼륨 슬라이더는 움직이는 즉시 들린다.
    SW_EXPECT_TRUE( settings.setPendingFloatValue( "sound.volume", 0.25f ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_NEAR_EQUAL( 0.25f, audio.getBusVolume( "voice" ), 0.0001f );
    SW_EXPECT_TRUE( settings.isPending( "sound.volume" ) );
    settings.revertPending();
    SW_EXPECT_NEAR_EQUAL( 1.0f, audio.getBusVolume( "voice" ), 0.0001f );
    SW_EXPECT_FALSE( settings.hasPendingChanges() );

    // OnConfirm: 적용 전에는 대상이 그대로다.
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "video.shadows", false ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( gv_userSettingsTestBool );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_FALSE( gv_userSettingsTestBool );
    SW_EXPECT_STREQ( "custom", settings.getAppliedValue( "video.quality" ) );

    // 의존 조건: video.mode 가 c 이면 shadows 는 꺼진다(회색) — 바꿀 수 없다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.mode", "c" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_FALSE( settings.isSettingEnabled( "video.shadows" ) );
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "video.shadows", true ) == sw::UserSettingSetResult::Disabled );
    settings.revertPending();
    SW_EXPECT_TRUE( settings.isSettingEnabled( "video.shadows" ) );

    // 카테고리 기본값: 보류로 들어가고 적용해야 닿는다.
    settings.resetCategoryToDefaults( "video" );
    SW_EXPECT_STREQ( "true", settings.getValue( "video.shadows" ) );
    SW_EXPECT_STREQ( "high", settings.getValue( "video.quality" ) );
    SW_EXPECT_FALSE( gv_userSettingsTestBool );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_TRUE( gv_userSettingsTestBool );

    // 다음 실행에 닿는 값은 적용해도 대상에 가지 않고 "다시 시작 필요" 다.
    SW_EXPECT_FALSE( settings.isRestartRequired() );
    SW_EXPECT_TRUE( settings.setPendingIntValue( "sound.restart", 4 ) == sw::UserSettingSetResult::Accepted );
    const sw::UserSettingsApplyResult result = settings.applyPending();
    SW_EXPECT_TRUE( result._bNeedsRestart );
    SW_EXPECT_TRUE( settings.isRestartRequired() );
    UserSettingsTestInternal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 화면 방식처럼 확인이 필요한 값은 시간 안에 확인하지 않으면 되돌린다 — 시계는 `update` 의 델타다
 */
SW_TEST_CASE( UserSettingsTest, ConfirmCountdownRevertsWithoutConfirmation )
{
    sw::UserSettingsManager settings;
    SW_ASSERT_TRUE( UserSettingsTestInternal::initializeWithSchema( settings ) );
    sw::vector<sw::UserSettingEventKind> listEvent;
    struct EventSink
    {
        sw::vector<sw::UserSettingEventKind>* _pListEvent;
        void                                  onEvent( const sw::UserSettingEvent& event ) { _pListEvent->push_back( event._kind ); }
    };
    EventSink                sink{ &listEvent };
    const sw::DelegateHandle handle = settings.registerEventListener( SW_DELEGATE_METHOD( sw::UserSettingEventListener, &EventSink::onEvent, &sink ) );

    // 1) 확인하지 않으면 10 초 뒤 되돌린다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.mode", "c" ) == sw::UserSettingSetResult::Accepted );
    const sw::UserSettingsApplyResult result = settings.applyPending();
    SW_EXPECT_TRUE( result._bAwaitingConfirm );
    SW_EXPECT_EQUAL( 7, gv_userSettingsTestInt ); // 선택지의 targetValue
    // 확인 대기 중에 쓰는 파일은 옛 값이다 — 확인 전에 꺼지면 다음 실행은 확인된 화면으로 뜬다.
    SW_EXPECT_TRUE( settings.makeUserJson().find( "video.mode" ) == sw::string::npos );
    settings.update( 6.0f );
    SW_EXPECT_TRUE( settings.isAwaitingConfirm() );
    SW_EXPECT_NEAR_EQUAL( 4.0f, settings.getConfirmSecondsLeft(), 0.001f );
    settings.update( 5.0f );
    SW_EXPECT_FALSE( settings.isAwaitingConfirm() );
    SW_EXPECT_STREQ( "a", settings.getAppliedValue( "video.mode" ) );
    SW_EXPECT_EQUAL( 0, gv_userSettingsTestInt );

    // 2) 확인하면 남는다.
    SW_EXPECT_TRUE( settings.setPendingValue( "video.mode", "b" ) == sw::UserSettingSetResult::Accepted );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    settings.confirmChanges();
    settings.update( 100.0f );
    SW_EXPECT_STREQ( "b", settings.getAppliedValue( "video.mode" ) );
    SW_EXPECT_EQUAL( 1, gv_userSettingsTestInt );

    bool bSawTimeout   = false;
    bool bSawConfirmed = false;
    for ( const sw::UserSettingEventKind kind : listEvent )
    {
        bSawTimeout   = bSawTimeout || kind == sw::UserSettingEventKind::ConfirmTimedOut;
        bSawConfirmed = bSawConfirmed || kind == sw::UserSettingEventKind::Confirmed;
    }
    SW_EXPECT_TRUE( bSawTimeout );
    SW_EXPECT_TRUE( bSawConfirmed );
    settings.unregisterEventListener( handle );
    UserSettingsTestInternal::resetTestVariables();
}

/**
 * @brief [UserSettingsTest] 같은 범위에서 같은 키를 두 번 쓰면 겹침이다 — 거절하거나 맞바꾸고, 적용하면 입력 맵이 따라가며, 글리프는 보류 키를 보인다
 */
SW_TEST_CASE( UserSettingsTest, KeyBindingConflictDetectionAndSwap )
{
    sw::InputMap inputMap;
    inputMap.registerLayer( "Gameplay", 0, true, false, false );
    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Pressed, "Gameplay" );
    inputMap.bind( "Fire", sw::MouseButton::Left, sw::ActionTrigger::Pressed, "Gameplay" );
    inputMap.bind( "Crouch", sw::Key::C, sw::ActionTrigger::Pressed, "Gameplay" );

    sw::UserSettingsManager settings;
    sw::UserSettingsTargets targets;
    targets._pInputMap = &inputMap;
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="controls"/>
        <Setting id="controls.jump" category="controls" type="keyBinding" action="Jump" default=""/>
        <Setting id="controls.fire" category="controls" type="keyBinding" action="Fire" default=""/></UserSettingsSchema>)",
                                                    "bindings.settings.xml" ) );
    settings.reapplyAll();

    sw::UserSettingBindingConflict conflict;
    SW_EXPECT_TRUE( settings.findBindingConflict( "controls.jump", "Mouse.Left", conflict ) );
    SW_EXPECT_STREQ( "controls.fire", conflict._settingID.c_str() );
    SW_EXPECT_FALSE( settings.findBindingConflict( "controls.jump", "Key.F", conflict ) );

    // 거절 — 메뉴가 "이미 Fire 에 쓰입니다" 를 묻는다.
    SW_EXPECT_TRUE( settings.setPendingBinding( "controls.jump", "Mouse.Left", sw::UserSettingBindingPolicy::Reject ) == sw::UserSettingSetResult::Conflict );
    // 맞바꾸기 — Fire 가 Jump 의 지금 키(Space)를 받는다.
    SW_EXPECT_TRUE( settings.setPendingBinding( "controls.jump", "Mouse.Left", sw::UserSettingBindingPolicy::Swap ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_STREQ( "Mouse.Left", settings.getValue( "controls.jump" ) );
    SW_EXPECT_STREQ( "Key.Space", settings.getValue( "controls.fire" ) );
    SW_EXPECT_STREQ( "[ Space ]", settings.getBindingGlyph( "controls.fire", sw::InputGlyphStyle::KeyboardMouse ) );

    // 스키마 밖 액션(Crouch)과 겹치면 맞바꿀 상대가 없다.
    SW_EXPECT_TRUE( settings.findBindingConflict( "controls.jump", "Key.C", conflict ) );
    SW_EXPECT_TRUE( conflict._settingID.empty() );
    SW_EXPECT_STREQ( "Crouch", conflict._action.c_str() );
    SW_EXPECT_TRUE( settings.setPendingBinding( "controls.jump", "Key.C", sw::UserSettingBindingPolicy::Swap ) == sw::UserSettingSetResult::Conflict );

    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    sw::InputSlot slot;
    SW_ASSERT_TRUE( inputMap.findRebindSlot( "Jump", 0, slot ) );
    SW_EXPECT_TRUE( slot == sw::InputSlot::fromMouseButton( sw::MouseButton::Left ) );
    SW_ASSERT_TRUE( inputMap.findRebindSlot( "Fire", 0, slot ) );
    SW_EXPECT_TRUE( slot == sw::InputSlot::fromKey( sw::Key::Space ) );

    // 카테고리 기본값 = 입력 맵의 기본 바인딩.
    settings.resetCategoryToDefaults( "controls" );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_ASSERT_TRUE( inputMap.findRebindSlot( "Jump", 0, slot ) );
    SW_EXPECT_TRUE( slot == sw::InputSlot::fromKey( sw::Key::Space ) );
}

/**
 * @brief [UserSettingsTest] 입력 슬롯 글 표기는 왕복한다 — 키 · 마우스 · 패드 번호 · 모르는 이름
 */
SW_TEST_CASE( UserSettingsTest, InputSlotTextRoundTrips )
{
    sw::InputSlot slot;
    SW_ASSERT_TRUE( sw::InputSlotUtil::tryParse( "key.space", slot ) );
    SW_EXPECT_STREQ( "Key.Space", sw::InputSlotUtil::toText( slot ) );
    SW_ASSERT_TRUE( sw::InputSlotUtil::tryParse( "Gamepad2.A", slot ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( slot._deviceIndex ) );
    SW_EXPECT_STREQ( "Gamepad2.A", sw::InputSlotUtil::toText( slot ) );
    SW_ASSERT_TRUE( sw::InputSlotUtil::tryParse( "Mouse.Right", slot ) );
    SW_EXPECT_STREQ( "Mouse.Right", sw::InputSlotUtil::toText( slot ) );
    SW_EXPECT_FALSE( sw::InputSlotUtil::tryParse( "Key.NoSuchKey", slot ) );
    SW_EXPECT_FALSE( sw::InputSlotUtil::tryParse( "Gamepad9.A", slot ) );
    SW_EXPECT_FALSE( sw::InputSlotUtil::tryParse( "Space", slot ) );
}

/**
 * @brief [UserSettingsTest] 언어 설정을 바꾸면 로컬라이즈된 글이 바뀐다 — 선택지는 읽힌 언어 목록(공급자)이다
 */
SW_TEST_CASE( UserSettingsTest, LanguageSwitchChangesLocalizedString )
{
    sw::LocalizationManager localization;
    const sw::hashed_string kKey{ "MENU_START" };
    localization.setString( "en_us", kKey, "Start" );
    localization.setString( "ko_kr", kKey, "시작" );
    SW_ASSERT_TRUE( localization.setCurrentLanguage( "en_us" ) );

    sw::UserSettingsManager settings;
    sw::UserSettingsTargets targets;
    targets._pLocalizationManager = &localization;
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="language"/>
        <Setting id="language.text" category="language" type="enum" default="" optionsFrom="localization.languages" apply="immediate"
                 target="applier:localization.language"/></UserSettingsSchema>)",
                                                    "language.settings.xml" ) );
    settings.reapplyAll();
    SW_EXPECT_STREQ( "Start", localization.getString( kKey ) );

    sw::vector<sw::UserSettingOption> listOption;
    settings.collectOptions( "language.text", listOption );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listOption.size() ) );

    SW_EXPECT_TRUE( settings.setPendingValue( "language.text", "ko_kr" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_STREQ( "시작", localization.getString( kKey ) );
    // 읽히지 않은 언어는 선택지가 아니다.
    SW_EXPECT_TRUE( settings.setPendingValue( "language.text", "fr_fr" ) == sw::UserSettingSetResult::Rejected );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_STREQ( "ko_kr", settings.getAppliedValue( "language.text" ) );
}

/**
 * @brief [UserSettingsTest] 의사 문화권은 언어 설정의 선택지이고, 고르면 화면의 글이 바로 의사 글이 된다(Dev 빌드) — 거울 방식은 오른쪽→왼쪽이다
 * @details 잘림 · 하드코딩 글을 찾는 사람이 명령줄 없이 옵션 메뉴에서 켤 수 있어야 한다(언리얼 `-culture=` · 유니티 Pseudo-Locale 선택과 같은 자리).
 */
SW_TEST_CASE( UserSettingsTest, PseudoLocaleIsSelectableThroughTheLanguageSetting )
{
    const sw::string folder      = test::makeTempDirectory( "settings_pseudo" );
    const sw::string projectPath = sw::FileUtil::joinPath( folder, "p.locproject.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( projectPath, R"({ "name": "p", "sourceCulture": "en", "stringTables": [ "p.strings.json" ] })" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "p.strings.json" ), R"({ "culture": "en", "entries": { "MENU_START": { "source": "Start" } } })" ) );

    sw::LocalizationManager localization;
    SW_ASSERT_TRUE( localization.loadCultureTable( "engine/localization/engine.cultures.json" ) );
    SW_ASSERT_TRUE( localization.mountProject( projectPath, sw::LocalizationScope::Game ) );

    sw::UserSettingsManager settings;
    sw::UserSettingsTargets targets;
    targets._pLocalizationManager = &localization;
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="language"/>
        <Setting id="language.text" category="language" type="enum" default="" optionsFrom="localization.languages" apply="immediate"
                 target="applier:localization.language"/></UserSettingsSchema>)",
                                                    "language.settings.xml" ) );
    settings.reapplyAll();
    sw::vector<sw::UserSettingOption> listOption;
    settings.collectOptions( "language.text", listOption );
#if !defined( SW_SHIPPING )
    bool bListed{ false };
    for ( const sw::UserSettingOption& option : listOption )
    {
        bListed = bListed || option._value == "qps_ploc";
    }
    SW_EXPECT_TRUE( bListed );
    SW_EXPECT_TRUE( settings.setPendingValue( "language.text", "qps_ploc" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( sw::PseudoLocalizer::isPseudoText( localization.getString( sw::hashed_string( "MENU_START" ) ) ) );
    SW_EXPECT_TRUE( settings.setPendingValue( "language.text", "qps_plocm" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( localization.isRightToLeft() );
#else
    SW_EXPECT_EQUAL( size_t( 1 ), listOption.size() ); // 배포본은 원문 문화권뿐이다
#endif
}

/**
 * @brief [UserSettingsTest] 엔진 스키마의 접근성 · 게임플레이 설정이 전역 변수 대상에 닿는다(색각 · 번쩍임 · 자막 · UI 배율 · 흔들림 · 시야각)
 */
SW_TEST_CASE( UserSettingsTest, AccessibilitySettingsReachGlobalVariables )
{
    using Internal                       = UserSettingsTestInternal;
    sw::GlobalVariableManager& variables = sw::engine::getGlobalVariableManager();
    sw::UserSettingsManager    settings;
    sw::UserSettingsTargets    targets;
    targets._pGlobalVariableManager = &variables;
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchema( "engine/settings/engine.settings.xml" ) );
    settings.reapplyAll();

    SW_EXPECT_TRUE( settings.setPendingValue( "accessibility.colorVision", "deuteranopia" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "accessibility.reduceFlashing", true ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingFloatValue( "accessibility.uiScale", 1.25f ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingValue( "accessibility.subtitleSize", "large" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingFloatValue( "gameplay.cameraShake", 0.0f ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingFloatValue( "gameplay.fieldOfView", 95.0f ) == sw::UserSettingSetResult::Accepted );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다

    SW_EXPECT_EQUAL( 2, Internal::findVariable( "gv_colorVisionMode" ).getValueAsInt() );
    SW_EXPECT_TRUE( Internal::findVariable( "gv_reduceFlashing" ).getValueAsBool() );
    SW_EXPECT_NEAR_EQUAL( 1.25f, Internal::findVariable( "gv_uiScale" ).getValueAsFloat(), 0.0001f );
    SW_EXPECT_EQUAL( 2, Internal::findVariable( "gv_subtitleSize" ).getValueAsInt() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, Internal::findVariable( "gv_cameraShakeScale" ).getValueAsFloat(), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 95.0f, Internal::findVariable( "gv_cameraFieldOfView" ).getValueAsFloat(), 0.0001f );

    // 자막을 끄면 자막 크기는 바꿀 수 없다(enabledWhen).
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "accessibility.subtitles", false ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_FALSE( settings.isSettingEnabled( "accessibility.subtitleSize" ) );
    settings.revertPending();

    // 다른 시험이 기본값을 보도록 되돌린다.
    settings.resetCategoryToDefaults( "accessibility" );
    settings.resetCategoryToDefaults( "gameplay" );
    (void)settings.applyPending(); // 적용 결과는 아래 단언이 대상 값으로 확인한다
    SW_EXPECT_EQUAL( 0, Internal::findVariable( "gv_colorVisionMode" ).getValueAsInt() );
    SW_EXPECT_FALSE( Internal::findVariable( "gv_reduceFlashing" ).getValueAsBool() );
}

/**
 * @brief [UserSettingsTest] 화면 설정은 창을 직접 만지지 않고 요청으로 쌓인다 — 기동 적용은 요청을 내지 않고, 플레이어가 고른 값만 "있음" 이다
 */
SW_TEST_CASE( UserSettingsTest, DisplaySettingsQueueARequestForTheHost )
{
    sw::UserSettingsManager settings;
    settings.initialize( sw::UserSettingsTargets{} );
    SW_ASSERT_TRUE( settings.loadSchema( "engine/settings/engine.settings.xml" ) );
    settings.reapplyAll();

    sw::DisplaySettingsRequest request;
    SW_EXPECT_FALSE( settings.consumeDisplayRequest( request ) );
    SW_EXPECT_FALSE( settings.getDisplayRequest()._bHasResolution );

    SW_EXPECT_TRUE( settings.setPendingValue( "display.resolution", "1920x1080" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "display.vsync", true ) == sw::UserSettingSetResult::Accepted );
    const sw::UserSettingsApplyResult result = settings.applyPending();
    SW_EXPECT_TRUE( result._bAwaitingConfirm );
    SW_ASSERT_TRUE( settings.consumeDisplayRequest( request ) );
    SW_EXPECT_EQUAL( 1920u, request._width );
    SW_EXPECT_EQUAL( 1080u, request._height );
    SW_EXPECT_TRUE( request._bVSync );
    SW_EXPECT_TRUE( request._bHasResolution );
    SW_EXPECT_FALSE( settings.consumeDisplayRequest( request ) );

    // 창 방식이 borderless 면 해상도는 고를 수 없다.
    SW_EXPECT_TRUE( settings.setPendingValue( "display.windowMode", "borderless" ) == sw::UserSettingSetResult::Accepted );
    SW_EXPECT_FALSE( settings.isSettingEnabled( "display.resolution" ) );
}

/**
 * @brief [UserSettingsTest] 게임 스키마 덧붙이기와 게임 프리셋 기본값 — 난이도처럼 게임이 정한 열거형, 바꾸지 않은 값만 기본값을 따라간다
 */
SW_TEST_CASE( UserSettingsTest, GameOverlayAndGameDefaults )
{
    sw::UserSettingsManager settings;
    sw::UserSettingsTargets targets;
    targets._pGlobalVariableManager = &sw::engine::getGlobalVariableManager();
    settings.initialize( targets );
    SW_ASSERT_TRUE( settings.loadSchema( "engine/settings/engine.settings.xml" ) );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1">
        <Setting id="gameplay.difficulty" category="gameplay" type="enum" default="normal">
            <Option value="easy"/><Option value="normal"/><Option value="hard"/>
        </Setting></UserSettingsSchema>)",
                                                    "game.settings.xml" ) );
    SW_EXPECT_TRUE( settings.setGameDefault( "gameplay.difficulty", "hard" ) );
    SW_EXPECT_TRUE( settings.setGameDefault( "gameplay.fieldOfView", "90" ) );
    SW_EXPECT_STREQ( "hard", settings.getValue( "gameplay.difficulty" ) );
    SW_EXPECT_STREQ( "90", settings.getDefaultValue( "gameplay.fieldOfView" ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a game default for a setting that does not exist" );
        SW_EXPECT_FALSE( settings.setGameDefault( "gameplay.noSuchSetting", "1" ) );
        SW_EXPECT_FALSE( settings.setGameDefault( "gameplay.difficulty", "nightmare" ) );
    }
    // 품질 프리셋 기본값을 바꾸면 묶인 값의 기본값도 그 프리셋이다.
    SW_EXPECT_TRUE( settings.setGameDefault( "graphics.quality", "low" ) );
    SW_EXPECT_STREQ( "0.75", settings.getValue( "graphics.renderScale" ) );
}
