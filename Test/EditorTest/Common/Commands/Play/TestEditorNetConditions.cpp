#include "pch.h"

#include "Core/Network/Transport/NetEmulation.h"

#include "Editor/Common/Commands/Play/EditorNetConditions.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 등록 테스트용 탭 — 그리지 않는다. */
    class TestMultiplayerTabA final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override { (void)context; }
    };

    /** @brief 등록 테스트용 둘째 탭입니다. */
    class TestMultiplayerTabB final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override { (void)context; }
    };

    // 순서 키가 등록 순서와 거꾸로다 — 정렬은 순서 키가 정한다.
    SW_EDITOR_MULTIPLAYER_TAB( TestMultiplayerTabA, "test_tab_late", 900100, "Late" );
    SW_EDITOR_MULTIPLAYER_TAB( TestMultiplayerTabB, "test_tab_early", 900000, "Early" );
} // namespace

/**
 * @brief [EditorNetConditionsTest] 엔진 프리셋 표의 줄마다 단추 이름이 있고, 패널 값(ms · % · KB/s)으로 옮겨도 표의 값이다
 */
SW_TEST_CASE( EditorNetConditionsTest, EveryPresetRowReachesThePanelWithItsValues )
{
    NetEmulationPresetTable table;
    SW_ASSERT_TRUE( table.loadFromResource( ::test::getEngineDefaultAssets()._netEmulationPresets.get() ) );
    for ( const NetEmulationPresetDef& preset : table._listPreset )
    {
        SW_EXPECT_TRUE_MSG( preset._label.empty() == false, preset._name.c_str() );
        const EditorNetLinkSettings settings = EditorNetConditionsUtil::makeLinkSettings( preset.makeConditions() );
        SW_EXPECT_NEAR_EQUAL( settings._latencyMilliseconds, static_cast<float32>( preset._latencyMs ), 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( settings._jitterMilliseconds, static_cast<float32>( preset._jitterMs ), 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( settings._lossPercent, preset._lossPercent, 1.0e-3f );
        SW_EXPECT_EQUAL( settings._bandwidthKilobytesPerSecond, preset._bandwidthKilobytesPerSecond );
    }
}

/**
 * @brief [EditorNetConditionsTest] 패널 값으로 만든 `net.emulation` 명령은 자식 프로세스가 읽는 덮어쓰기 글이고, 읽으면 같은 값이다
 */
SW_TEST_CASE( EditorNetConditionsTest, LinkCommandCarriesThePanelValues )
{
    EditorNetLinkSettings settings;
    settings._latencyMilliseconds         = 120.0f;
    settings._jitterMilliseconds          = 30.0f;
    settings._lossPercent                 = 30.0f;
    settings._bandwidthKilobytesPerSecond = 64;
    const string command                  = EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Up, settings );
    const string prefix                   = "net.emulation up ";
    SW_ASSERT_TRUE( command.rfind( prefix, 0 ) == 0 );

    NetEmulationConditions parsed;
    string                 error;
    SW_ASSERT_TRUE_MSG( NetEmulationConditions::parseOverrides( command.substr( prefix.size() ), parsed, error ), error.c_str() );
    const EditorNetLinkSettings back = EditorNetConditionsUtil::makeLinkSettings( parsed );
    SW_EXPECT_NEAR_EQUAL( back._latencyMilliseconds, 120.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( back._jitterMilliseconds, 30.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( back._lossPercent, 30.0f, 1.0e-3f );
    SW_EXPECT_EQUAL( back._bandwidthKilobytesPerSecond, 64 );

    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makePresetCommand( "badwifi" ), string( "net.emulation both badwifi" ) );
    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makeSpikeCommand( 2.5f ), string( "net.emulation spike 2.5" ) );
    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Down, EditorNetLinkSettings{} ).rfind( "net.emulation down latencyMs=0 ", 0 ),
                     size_t{ 0 } );
}

/**
 * @brief [EditorNetConditionsTest] 명령 대상 글(server · client<N> · clients · all · 번호)을 프로세스 구간으로 읽고, 없는 프로세스는 거절한다
 */
SW_TEST_CASE( EditorNetConditionsTest, TargetTextSelectsProcessRanges )
{
    int32 firstIndex = -1;
    int32 lastIndex  = -1;
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "server", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 0 );
    SW_EXPECT_EQUAL( lastIndex, 0 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "client2", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 2 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "clients", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 1 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "all", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 0 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "1", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 1 );

    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "client3", 3, firstIndex, lastIndex ) );
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "client0", 3, firstIndex, lastIndex ) );
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "clients", 1, firstIndex, lastIndex ) ); // 서버만 있으면 클라이언트 구간이 비었다
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "host", 3, firstIndex, lastIndex ) );
}

/**
 * @brief [MultiplayerTabTest] `SW_EDITOR_MULTIPLAYER_TAB` 한 줄이 탭 레지스트리에 오르고, 등록 순서가 아니라 순서 키로 줄 서고, 등록이 탭을 만든다
 * @details Multiplayer 창은 이 레지스트리만 읽어 탭 막대를 그린다(Session · Conditions 와 확장 모듈의 탭). 실제 에디터의 탭 목록은 `AppSmokeTest.EditorRegistriesKeepTheirOrder` 가 본다.
 */
SW_TEST_CASE( MultiplayerTabTest, RegisteredTabsSortByOrderAndCreateTheirTab )
{
    using Registry = EditorExtensionRegistry<EditorMultiplayerTabRegistration>;
    SW_EXPECT_STREQ( EditorMultiplayerTabRegistration::kKindName, "multiplayerTab" );
    const EditorMultiplayerTabRegistration* pEarly = Registry::find( "test_tab_early" );
    const EditorMultiplayerTabRegistration* pLate  = Registry::find( "test_tab_late" );
    SW_ASSERT_NOT_NULL( pEarly );
    SW_ASSERT_NOT_NULL( pLate );
    SW_EXPECT_STREQ( pEarly->_pLabel, "Early" );

    int32 earlyPosition = -1;
    int32 latePosition  = -1;
    for ( uint32 index = 0; index < Registry::getCount(); ++index )
    {
        const EditorMultiplayerTabRegistration& registration = Registry::getAt( index );
        if ( &registration == pEarly )
            earlyPosition = static_cast<int32>( index );
        if ( &registration == pLate )
            latePosition = static_cast<int32>( index );
    }
    SW_EXPECT_TRUE( 0 <= earlyPosition && earlyPosition < latePosition );

    const unique_ptr<IMultiplayerTab> pTab = pEarly->_pCreate();
    SW_EXPECT_NOT_NULL( pTab.get() );
}
