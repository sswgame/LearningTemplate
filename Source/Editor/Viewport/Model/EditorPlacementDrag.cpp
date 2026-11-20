#include "pch.h"

#include "Editor/Viewport/Model/EditorPlacementDrag.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Workspace/Asset/EditorPlacementPose.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Viewport/Model/EditorAssetPlacement.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    EditorPlacementDrag::EditorPlacementDrag()
        : _pDragPhysics{}
        , _pDragManager{ nullptr }
        , _bDragPrepared{ false }
    {
    }

    EditorPlacementDrag::~EditorPlacementDrag() = default;

    void EditorPlacementDrag::end()
    {
        _pDragPhysics.reset();
        _pDragManager  = nullptr;
        _bDragPrepared = false;
    }

    void EditorPlacementDrag::prepare( GameObjectManager* pManager, bool b2DMode )
    {
        if ( _bDragPrepared && _pDragManager == pManager )
            return;
        _bDragPrepared = true;
        _pDragManager  = pManager;
        _pDragPhysics.reset();
        if ( b2DMode || pManager == nullptr )
            return;
        // 편집 중에는 플레이 물리 씬이 없다 — 정적 바디만 담은 사본을 끌기 동안 하나 둔다(라이트 프로브 베이크와 같은 길). 물리 서비스가 없으면 비어 경계 · 바닥만 본다.
        _pDragPhysics = pManager->getSubsystem<ScenePhysics>().createStaticScene3D();
    }

    bool EditorPlacementDrag::query( const string& assetPath, const Ray* pRay, bool b2DMode, float32 snapStep, EditorPlacementGhost& outGhost )
    {
        outGhost                    = EditorPlacementGhost{};
        Scene*             pScene   = editor::getActiveScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        outGhost._bPlaceable = EditorAssetCommands::canPlaceInViewport( assetPath );
        prepare( pManager, b2DMode );
        if ( outGhost._bPlaceable && pRay != nullptr )
        {
            EditorPlacementQuery query;
            query._pPhysics    = _pDragPhysics.get();
            query._pManager    = pManager;
            query._maxDistance = getPreferences<EditorViewportPreferences>()._placementMaxDistance;
            query._snapStep    = snapStep;
            query._b2DMode     = b2DMode ? SW_TRUE : SW_FALSE;
            outGhost._bFound   = EditorAssetPlacement::findSurface( query, *pRay, outGhost._hit );
        }
        outGhost._pSurfaceName = EditorAssetPlacement::getSurfaceName( outGhost._hit._surface );
        return true;
    }

    EditorPlacementDropResult EditorPlacementDrag::drop( const string& assetPath, const EditorPlacementGhost& ghost, float32 yaw, bool b2DMode )
    {
        if ( ghost._bPlaceable == false )
        {
            (void)EditorAssetCommands::openPath( assetPath ); // 놓을 수 없는 종류는 연다 — 고스트가 놓기 전에 그렇다고 알렸다. 실패는 openPath 가 알린다
            return EditorPlacementDropResult::Opened;
        }
        if ( EditorPlaySession::refuseSceneEdit( "Viewport asset drop" ) )
            return EditorPlacementDropResult::Blocked; // 거절 이유가 경고 로그로 남는다
        if ( ghost._bFound == false )
        {
            SW_LOG_WARNING_ID( "EditorViewportAssetPlacement.NoSurface", "'%#' was not placed - no surface under the cursor within %# m (aim at an object, a collider or the ground plane)",
                               assetPath.c_str(), getPreferences<EditorViewportPreferences>()._placementMaxDistance );
            return EditorPlacementDropResult::Refused;
        }
        Scene*             pScene   = editor::getActiveScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return EditorPlacementDropResult::Blocked;
        const EditorPlacementHit& hit = ghost._hit;
        SW_LOG_TRACE_ID( "EditorViewportAssetPlacement.Placed", "Placed '%#' on the %# at (%#, %#, %#), turned %# deg", assetPath.c_str(), ghost._pSurfaceName, hit._position._x, hit._position._y,
                         hit._position._z, MathUtil::toDegree( yaw ) );
        EditorPlacementPose pose{};
        pose._position = hit._position;
        pose._yaw      = yaw;
        pose._b2DMode  = b2DMode;
        EditorAssetCommands::placeInViewport( pManager, assetPath.c_str(), pose );
        return EditorPlacementDropResult::Placed;
    }

    void EditorPlacementDrag::recordDrop( EditorPlacementDropResult result, const EditorPlacementGhost& ghost, EditorPlacementStats& inoutStats )
    {
        switch ( result )
        {
            case EditorPlacementDropResult::Opened:
                ++inoutStats._openedCount;
                break;
            case EditorPlacementDropResult::Refused:
                inoutStats._surface = ghost._hit._surface;
                ++inoutStats._refusedCount;
                break;
            case EditorPlacementDropResult::Placed:
                inoutStats._surface      = ghost._hit._surface;
                inoutStats._lastPosition = ghost._hit._position;
                inoutStats._lastYaw      = inoutStats._yaw;
                ++inoutStats._placedCount;
                break;
            case EditorPlacementDropResult::Blocked:
                break;
        }
    }

    float32 EditorPlacementDrag::stepYaw( float32 yaw, float32 step, int32 direction )
    {
        if ( step <= 0.0f )
            return yaw;
        // 걸음의 배수로 센 뒤 감는다 — 실수 덧셈을 쌓으면 네 번 돌린 값이 2π 언저리(6.2831 · 1e-7)에 남는다.
        const float32 stepIndex = MathUtil::round( yaw / step ) + static_cast<float32>( direction );
        float32       wrapped   = MathUtil::fmod( stepIndex * step, MathUtil::kTwoPi );
        if ( wrapped < 0.0f )
            wrapped += MathUtil::kTwoPi;
        if ( MathUtil::kTwoPi - wrapped < step * 0.5f )
            wrapped = 0.0f; // 한 바퀴를 다 돌았다
        return wrapped;
    }
} // namespace sw::editor
