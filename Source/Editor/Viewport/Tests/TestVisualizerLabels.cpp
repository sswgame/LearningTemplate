// VisualizerLabelTest — 등록된 뷰포트 시각화의 라벨 · 툴팁 키가 모듈 프로젝트를 맞춘 뒤 모두 풀리는지(nogpu).
// 시각화 등록은 키(`SW_LOCKEYREF`)만 들고 툴바가 그릴 때 푼다. 키가 어느 프로젝트에도 없거나 프로젝트가 붙지 않으면 그릴 때마다 MissingKey 경고가 난다.
#include "pch.h"

#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Localization/LocalizationManager.h"

/**
 * @brief [VisualizerLabelTest] 엔진 · 에디터 프로젝트와 올라온 모듈의 현지화 프로젝트를 맞추면 등록된 모든 시각화의 라벨과 툴팁이 풀린다
 * @details 음성 대조: 모듈 프로젝트를 맞추지 않으면(`syncModuleProjects` 를 빼면) 확장 모듈이 제 표에 둔 키가 풀리지 않아 이 검사가 진다.
 */
SW_TEST_CASE( VisualizerLabelTest, EveryRegisteredLabelResolvesAfterModuleProjectsSync )
{
    sw::LocalizationManager loc;
    SW_REQUIRE_TRUE( loc.mountProject( sw::AssetID{ "engine/localization/engine.locproject.xml" }, sw::LocalizationScope::Engine ) );
    SW_REQUIRE_TRUE( loc.mountProject( sw::AssetID{ "editor/localization/editor.locproject.xml" }, sw::LocalizationScope::Editor ) );
    SW_REQUIRE_TRUE( loc.syncModuleProjects() );

    const uint32_t count = sw::editor::EditorViewportVisualizer::getCount();
    SW_REQUIRE_TRUE( count > 0 );
    for ( uint32_t index = 0; index < count; ++index )
    {
        const sw::editor::EditorVisualizerRegistration& visualizer = sw::editor::EditorViewportVisualizer::getAt( index );
        SW_EXPECT_NOT_NULL( loc.findString( sw::hashed_string( visualizer._pToggleLabel ) ) );
        SW_EXPECT_NOT_NULL( loc.findString( sw::hashed_string( visualizer._pTooltip ) ) );
    }
}
