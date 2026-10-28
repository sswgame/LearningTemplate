/**
 * @file PhysicsShapeVisualizer.cpp
 * @brief 강체(RigidBody · RigidBody2D)의 충돌 셰이프를 뷰포트에 와이어프레임으로 그립니다 — 편집 중에도, 플레이 중에도 같은 셰이프 목록에서 그립니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Viewport/EditorColliderHandleTool.h"
#include "Editor/Viewport/EditorProjectedLineRenderer.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Component/ScenePhysics.h"
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
                EditorShapeVisualizerStats& stats = args._pStats->_shape;
                stats._physicsSegmentCount        = 0;
                if ( args._pObjectManager == nullptr || args._pViewProj == nullptr )
                    return;
                EditorProjectedLineRenderer renderer{ args._pDrawList, *args._pViewProj, args._canvasPos, args._canvasSize };
                // 씬 전체를 훑지 않고 물리 레지스트리의 바디 단계만 본다(빌보드 · 카메라 시각화와 같은 방식).
                const TypeInfo*     pBody3DType = findStaticType<RigidBodyComponent>();
                const TypeInfo*     pBody2DType = findStaticType<RigidBody2DComponent>();
                const ScenePhysics& physics     = args._pObjectManager->getSubsystem<ScenePhysics>();
                for ( PhysicsComponent* pComponent : physics.getComponents( PhysicsComponentPhase::Body ) )
                {
                    const GameObject* pObject     = pComponent->getOwner();
                    const bool        bSkipObject = pComponent->isActive() == false || pComponent->isPendingDestroy() || pObject == nullptr ||
                                             pObject->isActiveInHierarchy() == false || pObject->isHiddenInEditor();
                    if ( bSkipObject )
                        continue;
                    if ( const RigidBodyComponent* pBody3D = castTo<RigidBodyComponent>( pComponent, pBody3DType ); pBody3D != nullptr )
                        drawBody3D( renderer, *pBody3D );
                    else if ( const RigidBody2DComponent* pBody2D = castTo<RigidBody2DComponent>( pComponent, pBody2DType ); pBody2D != nullptr )
                        drawBody2D( renderer, *pBody2D );
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
