#include "pch.h"

#include "Editor/Viewport/EditorViewportAssetPlacement.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"

#include "Editor/Common/Commands/EditorDragPayload.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

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
        : _drag{}
        , _stats{}
    {
    }

    void EditorViewportAssetPlacement::process( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const ViewportToolbarSettings& settings )
    {
        if ( EditorInputGUI::isPayloadInFlight( EditorPayloadKindSet{ EditorPayloadKind::Asset } ) == false )
        {
            _drag.end();        // 끌기가 끝났다 — 사본을 버린다
            _stats._yaw = 0.0f; // 다음 끌기는 회전 없이 시작한다
            return;
        }

        string       assetPath;
        bool         bDelivered = false;
        const float2 canvasMax{ canvasPos._x + canvasSize._x, canvasPos._y + canvasSize._y };
        // 캔버스 사각형 전체가 타깃이다 — 캔버스 위에 그린 오버레이 · 기즈모 · HUD 아이템이 "마지막 아이템" 이 되어 놓은 것을 가로채지 않는다.
        if ( EditorWidgets::previewAssetDropInRect( canvasPos, canvasMax, "##ViewportAssetDrop", assetPath, bDelivered ) == false )
            return;

        // 판정 · 놓기(Model) — 커서 레이와 스냅 간격만 넘긴다.
        Ray                  mouseRay{};
        const bool           bRay = EditorViewportProjectionUtil::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay );
        EditorPlacementGhost ghost{};
        if ( _drag.query( assetPath, bRay ? &mouseRay : nullptr, settings._bIs2DMode, settings._bGridSnap ? settings._gridSnapValue : 0.0f, ghost ) == false )
            return;
        if ( ghost._bPlaceable )
            updateDragYaw();

        if ( bDelivered == false )
        {
            drawGhost( pDrawList, canvasPos, canvasSize, viewProj, assetPath, ghost );
            _stats._ghostFrame = ImGui::GetFrameCount();
            _stats._surface    = ghost._hit._surface;
            return;
        }
        EditorPlacementDrag::recordDrop( _drag.drop( assetPath, ghost, _stats._yaw, settings._bIs2DMode ), ghost, _stats );
    }

    void EditorViewportAssetPlacement::updateDragYaw()
    {
        const float32 step = getPreferences<EditorViewportPreferences>()._placementRotateStep;
        if ( EditorInputGUI::wasActionTriggered( "viewport.placementRotate" ) )
            _stats._yaw = EditorPlacementDrag::stepYaw( _stats._yaw, step, 1 );
        if ( EditorInputGUI::wasActionTriggered( "viewport.placementRotateBack" ) )
            _stats._yaw = EditorPlacementDrag::stepYaw( _stats._yaw, step, -1 );
    }

    void EditorViewportAssetPlacement::drawGhost( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const string& assetPath,
                                                  const EditorPlacementGhost& ghost ) const
    {
        const EditorPlacementHit& hit = ghost._hit;
        using Internal                = EditorViewportAssetPlacementInternal;
        const ImVec2 mousePos         = ImGui::GetMousePos();
        const ImVec2 labelPos{ mousePos.x + 16.0f * EditorThemeUtil::getDPIScale(), mousePos.y + 16.0f * EditorThemeUtil::getDPIScale() };
        const string fileName = FileUtil::getFileNamePart( assetPath );
        EditorSelfTestMarks::record( "sceneView.placementGhost" );

        if ( ghost._bPlaceable == false )
        {
            const string text = SW_LOCKEYFORMAT( "EdFormat", "PlacementOpensInstead", TextArgumentList().addText( "asset", fileName.c_str() ) );
            pDrawList->AddText( labelPos, EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) ), text.c_str() );
            return;
        }
        if ( ghost._bFound == false )
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
                                                   TextArgumentList().addText( "asset", fileName.c_str() ).addText( "surface", ghost._pSurfaceName ) );
        const ImU32  labelColor = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) );
        pDrawList->AddText( labelPos, labelColor, text.c_str() );
        const string rotation = SW_LOCKEYFORMAT( "EdFormat", "PlacementRotation", TextArgumentList().addInteger( "degrees", static_cast<int64>( MathUtil::round( MathUtil::toDegree( _stats._yaw ) ) ) ) );
        pDrawList->AddText( ImVec2{ labelPos.x, labelPos.y + ImGui::GetTextLineHeightWithSpacing() }, labelColor, rotation.c_str() );
    }
} // namespace sw::editor
