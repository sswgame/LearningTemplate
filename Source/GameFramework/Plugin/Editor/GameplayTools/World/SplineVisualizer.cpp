/**
 * @file SplineVisualizer.cpp
 * @brief 뷰포트 시각화 gameplay.splines(툴바 "Splines")를 구현합니다. 씬의 스플라인마다 호 길이 표의 곡선을 씬 뷰 선 값으로 모읍니다(그리기는 `EditorViewportVisualizer::drawAll`).
 * @details 조절점 손잡이는 여기 없다 — `SplineComponent::_listControlPoint` 의 선언(`Meta = "Shape=Points"`)이 고른 오브젝트의 점을 세운다(`PropertyShapeTool`).
 *          곡선은 로컬 곡선을 지금 월드 행렬로 옮겨 그린다 — 굳힌 월드 곡선(`getWorldPath`)은 오브젝트를 옮겨도 그 자리에 남아 편집 중에는 어긋난다(Godot Path3D 가 늘 노드의 자세로 그리는 것과 같다).
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

namespace sw::editor
{
    namespace
    {
        struct SplineVisualizerInternal
        {
            static constexpr float32 kCurveThickness = 2.0f;
            /** @brief 시각화 토글의 툴바 순서입니다(하우징 부지 910 곁). */
            static constexpr int32 kVisualizerOrder = 920;

            static void gatherCurves( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
            {
                if ( args._pObjectManager == nullptr )
                    return;
                EditorVisualizerLine line{};
                line._thickness = kCurveThickness;
                line._colorRole = EditorVisualizerColorRole::ThemeToken;
                line._pThemeKey = "Viewport.Marker";
                for ( const SplineComponent* pSpline : args._pObjectManager->getComponentRegistry().getAll<SplineComponent>() )
                {
                    const GameObject* pOwner = pSpline->getOwner();
                    if ( pOwner == nullptr || pOwner->isActiveInHierarchy() == false || pSpline->isActive() == false )
                        continue;
                    const float4x4                        world     = pSpline->getWorldMatrix();
                    const vector<SplinePath::TablePoint>& listPoint = pSpline->getLocalPath().getTablePoints();
                    for ( size_t index = 1; index < listPoint.size(); ++index )
                    {
                        line._from = float3::transform( listPoint[index - 1]._position, world );
                        line._to   = float3::transform( listPoint[index]._position, world );
                        outDrawList.addLine( line );
                    }
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( Splines, "gameplay.splines", SplineVisualizerInternal::kVisualizerOrder, SW_EDITOR_ICON_NAME( "Curve" ), SW_LOCKEYREF( "EdPluginVisualizer", "SplinesLabel" ),
                                 SW_LOCKEYREF( "EdPluginVisualizer", "SplinesTip" ), true, &SplineVisualizerInternal::gatherCurves );
} // namespace sw::editor
