#include "pch.h"

#include "Editor/Viewport/EditorViewportAssetPlacement.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorDragPayload.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorPlacementPose.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportAssetPlacementInternal
        {
            /** @brief 고스트 고리의 반지름(m)과 점 수입니다. 놓일 자리를 보이는 표시일 뿐 오브젝트 크기가 아니다. */
            static constexpr float32 kGhostRingRadius   = 0.5f;
            static constexpr uint32  kGhostRingSegments = 24;
            /** @brief 고스트 법선 선의 길이(m)입니다. */
            static constexpr float32 kGhostNormalLength = 0.75f;

            /** @brief @p normal 과 수직인 두 축입니다(고리를 그 면에 눕힌다). */
            static void makeTangentBasis( const float3& normal, float3& outTangent, float3& outBitangent )
            {
                const float3 reference = MathUtil::abs( normal._y ) < 0.99f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                outTangent             = reference.cross( normal ).normalize();
                outBitangent           = normal.cross( outTangent );
            }
        };
    } // namespace

    EditorViewportAssetPlacement::EditorViewportAssetPlacement()
        : _pDragPhysics{}
        , _pDragManager{ nullptr }
        , _stats{}
        , _bDragPrepared{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    EditorViewportAssetPlacement::~EditorViewportAssetPlacement() = default;

    void EditorViewportAssetPlacement::prepareDragPhysics( GameObjectManager* pManager, bool b2DMode )
    {
        if ( _bDragPrepared == SW_TRUE && _pDragManager == pManager )
            return;
        _bDragPrepared = SW_TRUE;
        _pDragManager  = pManager;
        _pDragPhysics.reset();
        if ( b2DMode || pManager == nullptr )
            return;
        // 편집 중에는 플레이 물리 씬이 없다 — 정적 바디만 담은 사본을 끌기 동안 하나 둔다(라이트 프로브 베이크와 같은 길). 물리 서비스가 없으면 비어 경계 · 바닥만 본다.
        _pDragPhysics = pManager->getSubsystem<ScenePhysics>().createStaticScene3D();
    }

    void EditorViewportAssetPlacement::process( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const ViewportToolbarSettings& settings )
    {
        if ( EditorInputGUI::isPayloadInFlight( EditorPayloadKindSet{ EditorPayloadKind::Asset } ) == false )
        {
            // 끌기가 끝났다 — 사본을 버린다(다음 끌기는 그 사이의 편집을 담아 새로 만든다)
            _pDragPhysics.reset();
            _pDragManager  = nullptr;
            _bDragPrepared = SW_FALSE;
            _stats._yaw    = 0.0f; // 다음 끌기는 회전 없이 시작한다
            return;
        }

        string       assetPath;
        bool         bDelivered = false;
        const float2 canvasMax{ canvasPos._x + canvasSize._x, canvasPos._y + canvasSize._y };
        // 캔버스 사각형 전체가 타깃이다 — 캔버스 위에 그린 오버레이 · 기즈모 · HUD 아이템이 "마지막 아이템" 이 되어 놓은 것을 가로채지 않는다.
        if ( EditorWidgets::previewAssetDropInRect( canvasPos, canvasMax, "##ViewportAssetDrop", assetPath, bDelivered ) == false )
            return;

        Scene*             pScene   = editor::getActiveScene();
        GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;

        const bool b2DMode    = settings._bIs2DMode;
        const bool bPlaceable = EditorAssetCommands::canPlaceInViewport( assetPath );
        prepareDragPhysics( pManager, b2DMode );
        if ( bPlaceable )
            updateDragYaw();

        EditorPlacementHit hit{};
        bool               bFound = false;
        Ray                mouseRay{};
        if ( bPlaceable && EditorViewportProjectionUtil::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay ) )
        {
            EditorPlacementQuery query;
            query._pPhysics    = _pDragPhysics.get();
            query._pManager    = pManager;
            query._maxDistance = getPreferences<EditorViewportPreferences>()._placementMaxDistance;
            query._snapStep    = settings._bGridSnap ? settings._gridSnapValue : 0.0f;
            query._b2DMode     = b2DMode ? SW_TRUE : SW_FALSE;
            bFound             = EditorAssetPlacement::findSurface( query, mouseRay, hit );
        }

        if ( bDelivered == false )
        {
            drawGhost( pDrawList, canvasPos, canvasSize, viewProj, assetPath, bPlaceable, bFound, hit );
            _stats._ghostFrame = ImGui::GetFrameCount();
            _stats._surface    = hit._surface;
            return;
        }

        // 놓였다
        if ( bPlaceable == false )
        {
            ++_stats._openedCount;
            (void)EditorAssetCommands::openPath( assetPath ); // 놓을 수 없는 종류는 연다 — 고스트가 놓기 전에 그렇다고 알렸다. 실패는 openPath 가 알린다
            return;
        }
        if ( EditorPlaySession::refuseSceneEdit( "Viewport asset drop" ) )
            return; // 거절 이유가 경고 로그로 남는다
        _stats._surface = hit._surface;
        if ( bFound == false )
        {
            ++_stats._refusedCount;
            SW_LOG_WARNING_ID( "EditorViewportAssetPlacement.NoSurface", "'%#' was not placed - no surface under the cursor within %# m (aim at an object, a collider or the ground plane)",
                               assetPath.c_str(), getPreferences<EditorViewportPreferences>()._placementMaxDistance );
            return;
        }
        ++_stats._placedCount;
        _stats._lastPosition = hit._position;
        _stats._lastYaw      = _stats._yaw;
        SW_LOG_TRACE_ID( "EditorViewportAssetPlacement.Placed", "Placed '%#' on the %# at (%#, %#, %#), turned %# deg", assetPath.c_str(), EditorAssetPlacement::getSurfaceName( hit._surface ), hit._position._x,
                         hit._position._y, hit._position._z, MathUtil::toDegree( _stats._yaw ) );
        EditorPlacementPose pose{};
        pose._position = hit._position;
        pose._yaw      = _stats._yaw;
        pose._b2DMode  = b2DMode;
        EditorAssetCommands::placeInViewport( pManager, assetPath.c_str(), pose );
    }

    void EditorViewportAssetPlacement::updateDragYaw()
    {
        const float32 step = getPreferences<EditorViewportPreferences>()._placementRotateStep;
        if ( EditorInputGUI::wasActionTriggered( "viewport.placementRotate" ) )
            _stats._yaw = EditorAssetPlacement::stepYaw( _stats._yaw, step, 1 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.placementRotateBack" ) )
            _stats._yaw = EditorAssetPlacement::stepYaw( _stats._yaw, step, -1 );
    }

    void EditorViewportAssetPlacement::drawGhost( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const string& assetPath,
                                                  bool bPlaceable, bool bFound, const EditorPlacementHit& hit ) const
    {
        using Internal        = EditorViewportAssetPlacementInternal;
        const ImVec2 mousePos = ImGui::GetMousePos();
        const ImVec2 labelPos{ mousePos.x + 16.0f * EditorThemeUtil::getDPIScale(), mousePos.y + 16.0f * EditorThemeUtil::getDPIScale() };
        const string fileName = FileUtil::getFileNamePart( assetPath );
        EditorSelfTestMarks::record( "sceneView.placementGhost" );

        if ( bPlaceable == false )
        {
            const string text = SW_LOCKEYFORMAT( "EdFormat", "PlacementOpensInstead", TextArgumentList().addText( "asset", fileName.c_str() ) );
            pDrawList->AddText( labelPos, EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) ), text.c_str() );
            return;
        }
        if ( bFound == false )
        {
            // 색만으로 알리지 않는다 — 이유와 할 일을 글로 적는다
            const string text = SW_LOCKEYFORMAT( "EdFormat", "PlacementNoSurface", TextArgumentList().addText( "asset", fileName.c_str() ) );
            pDrawList->AddText( labelPos, EditorColorUtil::toImU32( SW_THEME_COLOR( "Signal", "Error" ) ), text.c_str() );
            return;
        }

        // 놓일 자리: 표면에 눕힌 고리 + 법선 선. 고리는 투영한 점을 잇는다(원근에 따라 타원이 된다)
        float3 tangent{};
        float3 bitangent{};
        Internal::makeTangentBasis( hit._normal, tangent, bitangent );
        const ImU32 markerColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Marker" ) );
        ImVec2      arrRing[Internal::kGhostRingSegments];
        uint32      ringCount = 0;
        for ( uint32 index = 0; index < Internal::kGhostRingSegments; ++index )
        {
            const float32 angle = MathUtil::kTwoPi * static_cast<float32>( index ) / static_cast<float32>( Internal::kGhostRingSegments );
            const float3  point = hit._position + ( tangent * MathUtil::cos( angle ) + bitangent * MathUtil::sin( angle ) ) * Internal::kGhostRingRadius;
            if ( EditorViewportProjectionUtil::projectPoint( viewProj, point, canvasPos, canvasSize, arrRing[ringCount] ) )
                ++ringCount;
        }
        if ( ringCount == Internal::kGhostRingSegments )
            pDrawList->AddPolyline( arrRing, static_cast<int32>( ringCount ), markerColor, ImDrawFlags_Closed, 2.0f );
        ImVec2 base{};
        ImVec2 tip{};
        if ( EditorViewportProjectionUtil::projectSegment( viewProj, hit._position, hit._position + hit._normal * Internal::kGhostNormalLength, canvasPos, canvasSize, base, tip ) )
        {
            pDrawList->AddLine( base, tip, markerColor, 2.0f );
            pDrawList->AddCircleFilled( base, 3.0f * EditorThemeUtil::getDPIScale(), markerColor );
        }

        // 앞쪽 화살표: 놓기 전에 고른 회전(요)이 어디를 보게 하는지 — 고리 면 위에서 회전만큼 돈 접선 방향이다.
        const float3 facing = tangent * MathUtil::cos( _stats._yaw ) + bitangent * MathUtil::sin( _stats._yaw );
        ImVec2       facingTip{};
        if ( EditorViewportProjectionUtil::projectSegment( viewProj, hit._position, hit._position + facing * Internal::kGhostRingRadius, canvasPos, canvasSize, base, facingTip ) )
            pDrawList->AddLine( base, facingTip, markerColor, 2.0f );

        const string text       = SW_LOCKEYFORMAT( "EdFormat", "PlacementOnSurface",
                                                   TextArgumentList().addText( "asset", fileName.c_str() ).addText( "surface", EditorAssetPlacement::getSurfaceName( hit._surface ) ) );
        const ImU32  labelColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) );
        pDrawList->AddText( labelPos, labelColor, text.c_str() );
        const string rotation = SW_LOCKEYFORMAT( "EdFormat", "PlacementRotation", TextArgumentList().addInteger( "degrees", static_cast<int64>( MathUtil::round( MathUtil::toDegree( _stats._yaw ) ) ) ) );
        pDrawList->AddText( ImVec2{ labelPos.x, labelPos.y + ImGui::GetTextLineHeightWithSpacing() }, labelColor, rotation.c_str() );
    }
} // namespace sw::editor
