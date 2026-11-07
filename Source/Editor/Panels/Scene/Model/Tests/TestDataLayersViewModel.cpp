#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Panels/Scene/Model/DataLayersViewModel.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneSettings.h"

// DataLayersViewModelTest — 데이터 레이어 창의 ViewModel 명령. 결함: "On at load" 칸이 씬 설정을 되돌리기 · dirty 없이 바로 고쳤다.

namespace
{
    struct DataLayersViewModelTestInternal
    {
        static constexpr const utf8* kProbeLayer = "DataLayersViewModelProbe";

        /** @brief 활성 씬 설정에서 시험 레이어의 처음 상태입니다. 레이어가 없으면 false 입니다. */
        static bool isProbeInitiallyActive( const sw::Scene& scene )
        {
            const sw::SceneDataLayerDef* pLayer = scene.getSceneSettings().findDataLayer( kProbeLayer );
            return pLayer != nullptr && pLayer->_bInitiallyActive;
        }
    };
} // namespace

/**
 * @brief [DataLayersViewModelTest] 처음 상태 명령은 되돌리기 한 단계다 — 되돌리면 꺼지고 다시 하면 켜진다. 같은 값이면 기록이 늘지 않는다
 * @details 되돌리기 단계는 활성 씬을 찾으므로 에디터의 활성 씬에 시험 레이어를 잠시 선언하고 끝에 설정 · dirty 를 돌려 둔다. 메인 스택 대신 지역 스택을 범위에 건다.
 */
SW_TEST_CASE( DataLayersViewModelTest, InitiallyActiveIsOneUndoStep )
{
    using Internal = DataLayersViewModelTestInternal;

    sw::Scene* const pScene = sw::editor::getActiveScene();
    SW_REQUIRE_NOT_NULL( pScene );
    sw::editor::EditorContext* pContext = sw::editor::findSubsystem<sw::editor::EditorContext>();
    SW_REQUIRE_NOT_NULL( pContext );
    sw::editor::EditorWorkspace& workspace = pContext->getSubsystem<sw::editor::EditorWorkspace>();
    const bool                   bWasDirty = workspace.isSceneDirty();
    const sw::SceneSettings      original  = pScene->getSceneSettings();
    sw::SceneSettings            withProbe = original;
    sw::SceneDataLayerDef        probe{};
    probe._name             = Internal::kProbeLayer;
    probe._bInitiallyActive = false;
    withProbe._listDataLayer.push_back( probe );
    pScene->setSceneSettings( withProbe );

    sw::CommandStack   stack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    {
        const sw::editor::ScopedEditorScope scopedEditor( scope );
        sw::editor::DataLayersViewModel     viewModel;

        viewModel.setLayerInitiallyActive( Internal::kProbeLayer, true );
        SW_EXPECT_TRUE( Internal::isProbeInitiallyActive( *pScene ) );
        SW_REQUIRE_EQUAL( stack.getCommandCount(), size_t{ 1 } );
        SW_EXPECT_TRUE( stack.getCommand( 0 )._label == "Set Data Layer On At Load" );
        SW_EXPECT_TRUE( workspace.isSceneDirty() );

        // ViewState 는 바뀐 값을 값으로 담는다
        sw::editor::DataLayersViewState state;
        viewModel.gather( state );
        SW_EXPECT_TRUE( state._content == sw::editor::DataLayersContent::Layers );
        bool bProbeRowOn = false;
        for ( const sw::editor::DataLayerRow& row : state._listRow )
        {
            if ( row._name == Internal::kProbeLayer )
                bProbeRowOn = row._bInitiallyActive != SW_FALSE;
        }
        SW_EXPECT_TRUE( bProbeRowOn );

        // 이미 그 값이면 기록이 늘지 않는다
        viewModel.setLayerInitiallyActive( Internal::kProbeLayer, true );
        SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 1 } );

        stack.undo();
        SW_EXPECT_FALSE( Internal::isProbeInitiallyActive( *pScene ) );
        stack.redo();
        SW_EXPECT_TRUE( Internal::isProbeInitiallyActive( *pScene ) );
    }
    pScene->setSceneSettings( original );
    if ( bWasDirty == false )
        workspace.clearSceneDirty();
}
