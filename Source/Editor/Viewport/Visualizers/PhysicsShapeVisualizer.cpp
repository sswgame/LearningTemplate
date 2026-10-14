/**
 * @file PhysicsShapeVisualizer.cpp
 * @brief 강체(RigidBody · RigidBody2D)의 충돌 셰이프를 뷰포트에 와이어프레임으로 그립니다 — 편집 중에도, 플레이 중에도 같은 셰이프 목록에서 그립니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Viewport/EditorColliderHandleTool.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/Component/Physics/RigidBody2DComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

namespace sw::editor
{
    namespace
    {
        struct PhysicsShapeVisualizerInternal
        {
            /**
             * @brief 3D 강체의 셰이프를 바디의 월드 자세와 배율로 그립니다.
             * @details 물리 씬은 플레이에서만 바디를 만든다. 그래서 씬의 `drawDebug` 대신 컴포넌트의 셰이프 목록(`getShapes`)에서 그린다 — 월드 배율을 먹이는 규칙
             *          (`PhysicsComponentUtil::makeScaledShape`)이 바디를 만들 때와 같아 보이는 모양이 곧 물리가 판정하는 모양이다.
             */
            static void drawBody3D( IPhysicsDebugRenderer& renderer, const RigidBodyComponent& body )
            {
                float3     position{};
                quaternion rotation{};
                float3     scale{};
                PhysicsComponentUtil::readWorldPose( body, position, rotation, scale );
                const float4 color = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( body.getBodyType() ), body.isSleeping(), body.isTrigger() );
                for ( const PhysicsShapeDesc3D& shape : body.getShapes() )
                {
                    PhysicsDebugDrawUtil::drawShape3D( renderer, PhysicsComponentUtil::makeScaledShape( shape, scale ), position, rotation, color );
                }
            }

            /** @brief 2D 강체의 셰이프를 바디의 월드 자세(XY 와 Z 축 둘레 각)와 배율로 그립니다. */
            static void drawBody2D( IPhysicsDebugRenderer& renderer, const RigidBody2DComponent& body )
            {
                float3     position{};
                quaternion rotation{};
                float3     scale{};
                PhysicsComponentUtil::readWorldPose( body, position, rotation, scale );
                const float4  color = PhysicsDebugDrawUtil::getBodyColor( static_cast<uint8>( body.getBodyType() ), body.isSleeping(), body.isTrigger() );
                const float32 angle = PhysicsComponentUtil::getAngle2D( rotation );
                for ( const PhysicsShapeDesc2D& shape : body.getShapes() )
                {
                    PhysicsDebugDrawUtil::drawShape2D( renderer, PhysicsComponentUtil::makeScaledShape( shape, scale ), float2{ position._x, position._y }, angle,
                                                       color );
                }
            }

            static void draw( const EditorViewportVisualizerArgs& args )
            {
                EditorShapeVisualizerStats& stats = EditorShapeVisualizerStats::get();
                stats._physicsSegmentCount        = 0;
                if ( args._pListObject == nullptr || args._pViewProj == nullptr )
                    return;
                EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
                for ( const GameObject* pObject : *args._pListObject )
                {
                    const bool bSkipObject = pObject == nullptr || pObject->isActiveInHierarchy() == false || pObject->isHiddenInEditor();
                    if ( bSkipObject )
                        continue;
                    pObject->forEachComponentOfType<RigidBodyComponent>( [&renderer]( RigidBodyComponent* pBody )
                    {
                        if ( pBody->isActive() )
                            drawBody3D( renderer, *pBody );
                    } );
                    pObject->forEachComponentOfType<RigidBody2DComponent>( [&renderer]( RigidBody2DComponent* pBody )
                    {
                        if ( pBody->isActive() )
                            drawBody2D( renderer, *pBody );
                    } );
                }
                stats._physicsSegmentCount = renderer.getDrawnSegmentCount();
                // 같은 시각화 켬 아래서 선택한 강체의 크기 손잡이를 그리고 끈다.
                EditorColliderHandleTool::process( args );
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( PhysicsShapes, "physics_shapes", 110, editoricon::kCollider, "Phys",
                          "RigidBody · RigidBody2D 의 충돌 셰이프를 와이어프레임으로 표시합니다(정적 회색 · 키네마틱 파랑 · 동적 초록 · 트리거 노랑)", false,
                          &PhysicsShapeVisualizerInternal::draw );
} // namespace sw::editor
