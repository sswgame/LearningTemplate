// SplineVisualizerTest — 뷰포트 시각화 gameplay.splines 의 모으기: 로컬 곡선을 지금 월드 행렬로 옮긴 선분을 내고, 끈 오브젝트는 내지 않는다(그리기 없음 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    const EditorVisualizerRegistration* findVisualizer( string_view id )
    {
        for ( uint32 index = 0; index < EditorViewportVisualizer::getCount(); ++index )
        {
            const EditorVisualizerRegistration& registration = EditorViewportVisualizer::getAt( index );
            if ( string_view{ registration._pID } == id )
                return &registration;
        }
        return nullptr;
    }
} // namespace

/** @brief [SplineVisualizerTest] 선분은 조절점의 첫 자리에서 끝 자리까지 이어지고 오브젝트를 옮기면 함께 옮겨지며, 매니저가 없거나 오브젝트를 끄면 없다 */
SW_TEST_CASE( SplineVisualizerTest, GathersTheCurveInWorldSpace )
{
    const EditorVisualizerRegistration* pVisualizer = findVisualizer( "gameplay.splines" );
    SW_REQUIRE_NOT_NULL( pVisualizer );
    SW_REQUIRE_TRUE( pVisualizer->_pGather != nullptr );

    EditorViewportVisualizerArgs args;
    EditorVisualizerDrawList     drawList;
    pVisualizer->_pGather( args, drawList );
    SW_EXPECT_TRUE( drawList._listLine.empty() );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Rail" ) );
    SplineComponent*  pSpline = pObject->addComponent<SplineComponent>();
    SW_REQUIRE_NOT_NULL( pSpline );
    pSpline->setControlPoints( {
                                   float3{0.0f, 0.0f, 0.0f},
                                   float3{4.0f, 0.0f, 0.0f},
                                   float3{4.0f, 0.0f, 4.0f}
    },
                               SplineKind::Linear, false );
    pSpline->setLocalPosition( float3{ 10.0f, 1.0f, 0.0f } );
    manager.beginPlay();
    args._pObjectManager = &manager;
    pVisualizer->_pGather( args, drawList );
    SW_REQUIRE_FALSE( drawList._listLine.empty() );
    const EditorVisualizerLine& first = drawList._listLine.front();
    const EditorVisualizerLine& last  = drawList._listLine.back();
    SW_EXPECT_NEAR( 10.0f, first._from._x, 1.0e-3f );
    SW_EXPECT_NEAR( 1.0f, first._from._y, 1.0e-3f );
    SW_EXPECT_NEAR( 14.0f, last._to._x, 1.0e-3f );
    SW_EXPECT_NEAR( 4.0f, last._to._z, 1.0e-3f );

    drawList.clear();
    pObject->setActive( false );
    pVisualizer->_pGather( args, drawList );
    SW_EXPECT_TRUE( drawList._listLine.empty() );
}
