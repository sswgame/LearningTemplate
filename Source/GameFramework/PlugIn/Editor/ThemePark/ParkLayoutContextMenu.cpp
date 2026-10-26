/**
 * @file ParkLayoutContextMenu.cpp
 * @brief ThemePark 확장의 맥락 메뉴 명령을 등록합니다. 계층 창 · 씬 뷰의 오브젝트와 씬 뷰 빈 곳에서 가장 가까운 배치 놀이기구를 알려 줍니다.
 * @details 레벨 디자이너가 오브젝트를 놀이기구 옆에 둘 때 배치 파일(rides.xml)의 어느 놀이기구와 얼마나 떨어졌는지 본다.
 *          배치 미리보기(`ParkLayoutPreview`)가 키트 로더로 읽은 놀이기구 목록을 쓰고, 읽힌 놀이기구가 없으면 항목은 꺼진다.
 *          코어 명령과 같은 등록 줄(`SW_EDITOR_CONTEXT_COMMAND`)이다 — 확장 모듈이 내려가면 정적 등록자가 명령을 빼므로 핫 리로드 뒤에도 지난 모듈의 함수를 부르지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPreview.h"

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutContextMenuInternal
        {
            /** @brief 놀이기구가 읽혀 있는 미리보기입니다. 없거나 비어 있으면 nullptr 입니다. */
            static const ParkLayoutPreview* findLoadedPreview()
            {
                const ParkLayoutPreview* pPreview            = findSubsystem<ParkLayoutPreview>();
                const bool               bPreviewUnavailable = pPreview == nullptr || pPreview->isLoaded() == false || pPreview->getRides().empty();
                if ( bPreviewUnavailable )
                    return nullptr;
                return pPreview;
            }

            /** @brief 메뉴 대상의 월드 위치를 구합니다. 오브젝트 메뉴면 누른 오브젝트(없으면 주 선택)의 주 SceneComponent, 뷰포트 메뉴면 바닥 교점입니다. */
            static bool findTargetPosition( const EditorCommandTarget& target, float3& outPosition )
            {
                if ( target._kind == EditorCommandTargetKind::GameObject )
                {
                    const GameObject*     pObject    = EditorSceneObjectCommands::findTargetObject( target );
                    const SceneComponent* pSceneComp = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
                    if ( pSceneComp == nullptr )
                        return false;
                    outPosition = pSceneComp->getWorldPosition();
                    return true;
                }
                if ( target._bHasWorldPosition == false )
                    return false;
                outPosition = target._worldPosition;
                return true;
            }

            static bool isNearestRideEnabled( const EditorCommandTarget& target )
            {
                float3 position{};
                return findLoadedPreview() != nullptr && findTargetPosition( target, position );
            }

            /** @brief 대상 위치에서 가장 가까운 놀이기구(바닥 평면 거리)를 로그로 알립니다. */
            static void logNearestRide( const EditorCommandTarget& target )
            {
                const ParkLayoutPreview* pPreview = findLoadedPreview();
                float3                   position{};
                if ( pPreview == nullptr || findTargetPosition( target, position ) == false )
                    return;

                const ParkRidePreview* pNearest      = nullptr;
                float32                nearestDistSq = 0.0f;
                for ( const ParkRidePreview& ride : pPreview->getRides() )
                {
                    const float32 dx     = ride._position._x - position._x;
                    const float32 dz     = ride._position._z - position._z;
                    const float32 distSq = dx * dx + dz * dz;
                    if ( pNearest == nullptr || distSq < nearestDistSq )
                    {
                        pNearest      = &ride;
                        nearestDistSq = distSq;
                    }
                }
                SW_LOG_INFO( "Nearest park ride to (%#, %#, %#): '%#' at %# m (cost %#, capacity %#)", position._x, position._y, position._z, pNearest->_name.c_str(),
                             MathUtil::sqrt( nearestDistSq ), pNearest->_buildCost, pNearest->_capacity );
            }
        };
    } // namespace

    // 오브젝트 메뉴(계층 창 · 씬 뷰의 오브젝트 위)와 뷰포트 메뉴(씬 뷰 빈 곳). 순서 9100 — 코어 항목 아래 구분선 뒤다.
    SW_EDITOR_CONTEXT_COMMAND( ParkNearestRideObject, "themepark.nearestRide.object", "Nearest Park Ride", nullptr, "ThemePark", GameObject, 9100, nullptr,
                               &ParkLayoutContextMenuInternal::logNearestRide, &ParkLayoutContextMenuInternal::isNearestRideEnabled, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( ParkNearestRideViewport, "themepark.nearestRide.viewport", "Nearest Park Ride", nullptr, "ThemePark", Viewport, 9100, nullptr,
                               &ParkLayoutContextMenuInternal::logNearestRide, &ParkLayoutContextMenuInternal::isNearestRideEnabled, NoScene, nullptr );
} // namespace sw::editor
