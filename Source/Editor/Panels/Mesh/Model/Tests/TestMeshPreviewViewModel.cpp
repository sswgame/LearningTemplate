// MeshPreviewViewModelTest — 미리보기 씬의 메시 오브젝트 세우기 · 찾기, 메시를 읽기 전의 LOD 표와 상태 줄(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Panels/Mesh/Model/MeshPreviewViewModel.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/MeshComponent.h"

using namespace sw;
using namespace sw::editor;

/** @brief [MeshPreviewViewModelTest] 씬이 없으면 대상이 없고 카메라를 맞추지 않으며 표는 지난 값을 비운다 */
SW_TEST_CASE( MeshPreviewViewModelTest, NoSceneHasNoSubject )
{
    MeshPreviewViewModel model;
    float3               center{};
    float32              radius = 0.0f;
    SW_EXPECT_NULL( MeshPreviewViewModel::findSubject( nullptr ) );
    SW_EXPECT_FALSE( model.syncSubject( nullptr, "common/models/a.mesh", center, radius ) );
    model.applyLODChoice( nullptr ); // 씬이 없으면 아무것도 하지 않는다
    MeshPreviewLODState state;
    state._vertexCount = 99;
    MeshPreviewViewModel::gatherLOD( nullptr, state );
    SW_EXPECT_TRUE( state._bHasSubject == SW_FALSE );
    SW_EXPECT_EQUAL( 0u, state._vertexCount );
    const int32 autoChoice = MeshPreviewViewModel::kAutoLOD;
    SW_EXPECT_EQUAL( autoChoice, model.editLODChoice() );
}

/** @brief [MeshPreviewViewModelTest] 세운 대상은 찾히고, 메시가 아직 없으면 표는 대상만 있다고 한다 — 경로가 빈 문서는 읽지 못했다고 적는다 */
SW_TEST_CASE( MeshPreviewViewModelTest, PopulatedSubjectWithoutAMesh )
{
    Scene                scene( "MeshPreviewTest" );
    MeshPreviewViewModel model;
    model.populateScene( scene );
    SW_REQUIRE_NOT_NULL( MeshPreviewViewModel::findSubject( &scene ) );
    MeshPreviewLODState state;
    MeshPreviewViewModel::gatherLOD( &scene, state );
    SW_EXPECT_TRUE( state._bHasSubject == SW_TRUE );
    SW_EXPECT_TRUE( state._bMeshRead == SW_FALSE );
    SW_EXPECT_TRUE( state._listLOD.empty() );

    model.resetForDocument();
    SW_EXPECT_EQUAL( string( "Loaded" ), model.getStatus() );
    float3  center{};
    float32 radius = 0.0f;
    SW_EXPECT_FALSE( model.syncSubject( &scene, "", center, radius ) );
    SW_EXPECT_FALSE( model.getStatus() == "Loaded" );
}
