#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorSceneSettingsCommands.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSettings.h"

namespace
{
    struct EditorSceneSettingsCommandsTestInternal
    {
        static sw::SceneDataLayerDef makeLayer( const utf8* pName, bool bInitiallyActive )
        {
            sw::SceneDataLayerDef layer{};
            layer._name             = pName;
            layer._bInitiallyActive = bInitiallyActive;
            return layer;
        }
    };
} // namespace

// EditorSceneSettingsCommandsTest — 씬 설정 단계의 XML 왕복과 쓸 수 없는 설정의 거절. 활성 씬을 건드리지 않게 지역 씬으로 본다(되돌리기 실행은 DataLayersViewModelTest).

/**
 * @brief [EditorSceneSettingsCommandsTest] 단계에 담은 XML 을 다시 읽으면 같은 설정이다 — 데이터 레이어의 처음 상태까지
 */
SW_TEST_CASE( EditorSceneSettingsCommandsTest, SettingsXmlRoundTrips )
{
    using Commands = sw::editor::EditorSceneSettingsCommands;
    using Internal = EditorSceneSettingsCommandsTestInternal;

    sw::Scene         scene( "SceneSettingsProbe" );
    sw::SceneSettings settings{};
    settings._listDataLayer.push_back( Internal::makeLayer( "Winter", true ) );
    settings._listDataLayer.push_back( Internal::makeLayer( "Night", false ) );
    const sw::string xml = Commands::writeSettingsXml( settings );

    SW_REQUIRE_TRUE( Commands::restoreSettingsXml( scene, xml ) );
    const sw::SceneDataLayerDef* pWinter = scene.getSceneSettings().findDataLayer( "Winter" );
    const sw::SceneDataLayerDef* pNight  = scene.getSceneSettings().findDataLayer( "Night" );
    SW_REQUIRE_NOT_NULL( pWinter );
    SW_REQUIRE_NOT_NULL( pNight );
    SW_EXPECT_TRUE( pWinter->_bInitiallyActive );
    SW_EXPECT_FALSE( pNight->_bInitiallyActive );
    SW_EXPECT_TRUE( Commands::writeSettingsXml( scene.getSceneSettings() ) == xml );
}

/**
 * @brief [EditorSceneSettingsCommandsTest] 쓸 수 없는 설정(겹친 레이어 이름)은 바꾸지 않고 기록도 남기지 않는다
 * @details 메인 스택을 더럽히지 않게 지역 스택을 범위에 건다. 거절은 `SceneSettings::validate` 의 오류 로그를 남긴다(겹친 이름마다).
 */
SW_TEST_CASE( EditorSceneSettingsCommandsTest, InvalidSettingsAreRejectedWithoutRecord )
{
    using Commands = sw::editor::EditorSceneSettingsCommands;
    using Internal = EditorSceneSettingsCommandsTestInternal;

    sw::CommandStack   stack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    sw::Scene         scene( "SceneSettingsProbe" );
    sw::SceneSettings duplicated{};
    duplicated._listDataLayer.push_back( Internal::makeLayer( "Winter", true ) );
    duplicated._listDataLayer.push_back( Internal::makeLayer( "Winter", false ) );

    SW_EXPECT_LOG( "SceneSettings.DataLayerNameInvalid" ); // 거절의 까닭을 남긴다
    SW_EXPECT_FALSE( Commands::apply( scene, duplicated, "Probe" ) );
    SW_EXPECT_TRUE( scene.getSceneSettings()._listDataLayer.empty() );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 0 } );

    // 같은 설정은 아무것도 하지 않는다
    SW_EXPECT_FALSE( Commands::apply( scene, scene.getSceneSettings(), "Probe" ) );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 0 } );
}
