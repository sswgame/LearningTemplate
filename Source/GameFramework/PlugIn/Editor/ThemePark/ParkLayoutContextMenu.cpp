/**
 * @file ParkLayoutContextMenu.cpp
 * @brief ThemePark 확장의 우클릭 메뉴 항목을 등록합니다. 하이어라키의 오브젝트와 씬 뷰포트에서 가장 가까운 배치 놀이기구를 알려 줍니다.
 * @details 레벨 디자이너가 오브젝트를 놀이기구 옆에 둘 때 배치 파일(rides.xml)의 어느 놀이기구와 얼마나 떨어졌는지 본다.
 *          배치 미리보기(`ParkLayoutPreview`)가 키트 로더로 읽은 놀이기구 목록을 쓰고, 읽힌 놀이기구가 없으면 항목은 꺼진다.
 *          확장 모듈이 내려가면 정적 등록자가 항목을 빼므로 핫 리로드 뒤에도 지난 모듈의 함수를 부르지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorContextMenuExtension.h"
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
                const ParkLayoutPreview* pPreview            = getService<ParkLayoutPreview>();
                const bool               bPreviewUnavailable = pPreview == nullptr || pPreview->isLoaded() == false || pPreview->getRides().empty();
                if ( bPreviewUnavailable )
                    return nullptr;
                return pPreview;
            }

            /** @brief 메뉴 대상의 월드 위치를 구합니다. 오브젝트가 있으면 그 주 SceneComponent, 없으면 뷰포트 바닥 교점입니다. */
            static bool findTargetPosition( const EditorContextMenuTarget& target, float3& outPosition )
            {
                if ( target._pGameObject != nullptr )
                {
                    const SceneComponent* pSceneComp = target._pGameObject->getPrimarySceneComponent();
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

            static bool isNearestRideEnabled( const EditorContextMenuTarget& target )
            {
                float3 position{};
                return findLoadedPreview() != nullptr && findTargetPosition( target, position );
            }

            /** @brief 대상 위치에서 가장 가까운 놀이기구(바닥 평면 거리)를 로그로 알립니다. */
            static void logNearestRide( const EditorContextMenuTarget& target )
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

    SW_EDITOR_CONTEXT_MENU_ENTRY( ParkNearestRideObject, "themepark.nearestRide.object", 9100, ::sw::editor::EditorContextMenuKind::GameObject, "Nearest Park Ride",
                                  &ParkLayoutContextMenuInternal::logNearestRide, &ParkLayoutContextMenuInternal::isNearestRideEnabled );
    SW_EDITOR_CONTEXT_MENU_ENTRY( ParkNearestRideViewport, "themepark.nearestRide.viewport", 9100, ::sw::editor::EditorContextMenuKind::Viewport, "Nearest Park Ride",
                                  &ParkLayoutContextMenuInternal::logNearestRide, &ParkLayoutContextMenuInternal::isNearestRideEnabled );
} // namespace sw::editor
