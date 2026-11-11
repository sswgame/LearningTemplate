#include "pch.h"

#include "Editor/Panels/Animation/SpriteClipPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct SpriteClipPanelInternal
        {
            /** @brief 문서를 읽은 열린 스프라이트 클립 창입니다. 닫혔거나 문서가 없으면 nullptr 입니다. */
            [[nodiscard]] static const SpriteClipPanel* findLoadedPanel()
            {
                const EditorPanelManager* pPanels = EditorPanelManager::findInEditorContext();
                if ( pPanels == nullptr )
                    return nullptr;
                const SpriteClipPanel* pPanel = pPanels->findPanel<SpriteClipPanel>();
                return pPanel != nullptr && pPanel->isOpen() && pPanel->isClipDocumentLoaded() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readFrameCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const SpriteClipPanel* pPanel = findLoadedPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFrameCount() );
                return true;
            }

            /** @brief 고른 프레임의 그림 영역입니다(뒤집힘을 푼 uv). 창 · 프레임이 없으면 false 입니다. */
            [[nodiscard]] static bool findSelectedBounds( const SpriteClipPanel*& outPanel, float4& outBounds )
            {
                outPanel = findLoadedPanel();
                return outPanel != nullptr && outPanel->getModel().findSelectedFrameBounds( outBounds );
            }

            /** @brief 고른 프레임의 폭(아틀라스 픽셀)입니다. 아틀라스 크기를 모르면 uv 폭입니다. */
            [[nodiscard]] static bool readSelectedFrameWidthPixels( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const SpriteClipPanel* pPanel = nullptr;
                float4                 bounds{};
                if ( findSelectedBounds( pPanel, bounds ) == false )
                    return false;
                const float32 atlasWidth = pPanel->getModel().getAtlasPixelSize()._x;
                outValue                 = static_cast<float64>( ( bounds._z - bounds._x ) * ( atlasWidth > 0.0f ? atlasWidth : 1.0f ) );
                return true;
            }

            /** @brief 고른 프레임의 네 가장자리가 모두 아틀라스 픽셀 경계 위면 1 입니다(불규칙 프레임 끌기가 픽셀에 맞췄는지). */
            [[nodiscard]] static bool readSelectedFrameOnPixels( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const SpriteClipPanel* pPanel = nullptr;
                float4                 bounds{};
                if ( findSelectedBounds( pPanel, bounds ) == false )
                    return false;
                const SpriteClipModel& model   = pPanel->getModel();
                const bool             bOnGrid = model.snapToAtlasPixel( bounds._x, true ) == bounds._x && model.snapToAtlasPixel( bounds._z, true ) == bounds._z &&
                                     model.snapToAtlasPixel( bounds._y, false ) == bounds._y && model.snapToAtlasPixel( bounds._w, false ) == bounds._w;
                outValue = bOnGrid ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readAnimationCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const SpriteClipPanel* pPanel = findLoadedPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getAnimationCount() );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_PANEL( SpriteClipPanel, SpriteClipPanel::kPanelID, EditorPanelCategory::Tool, 1600 );
    SW_AUTOMATION_WATCH( editorSpriteClipFrameCount, "Editor.SpriteClipFrameCount", "Frames of the document the Sprite Clip window holds (fails while it is closed or has no document)",
                         &SpriteClipPanelInternal::readFrameCount );
    SW_AUTOMATION_WATCH( editorSpriteClipSelectedFrameWidth, "Editor.SpriteClipSelectedFrameWidthPx", "Width in atlas pixels of the frame selected in the Sprite Clip window",
                         &SpriteClipPanelInternal::readSelectedFrameWidthPixels );
    SW_AUTOMATION_WATCH( editorSpriteClipSelectedFrameOnPixels, "Editor.SpriteClipSelectedFrameOnPixels", "1 when every edge of the selected sprite frame sits on an atlas pixel boundary",
                         &SpriteClipPanelInternal::readSelectedFrameOnPixels );
    SW_AUTOMATION_WATCH( editorSpriteClipAnimationCount, "Editor.SpriteClipAnimationCount", "Named animations of the document the Sprite Clip window holds",
                         &SpriteClipPanelInternal::readAnimationCount );

    SpriteClipPanel::SpriteClipPanel()
        : EditorDocumentPanel{ EditorAssetType{ SpriteClipAsset::kAssetType }, false }
        , _model{}
        , _atlasThumbnail{}
        , _dragStartUV{}
        , _dragEdge{}
        , _bAtlasPress{ SW_FALSE }
        , _bEdgeMoved{ SW_FALSE }
    {
    }

    void SpriteClipPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "spriteClip" );
        _atlasThumbnail.update();

        drawFileBar();
        // 아틀라스가 창의 주 캔버스다(유니티 Sprite Editor · Godot SpriteFrames 처럼 그림 위에서 자르고 고른다) — 프레임 칸보다 위에 둔다.
        drawAtlasPicker();
        drawFrameSection();

        drawAnimationSection();
        drawPreviewSection();

        drawKeySection();

        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void SpriteClipPanel::drawFileBar()
    {
        fixed_string<constant::buffer::kMax256>& atlasPath = _model.editAtlasPath();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Atlas" ) );
        if ( EditorWidgets::drawAssetPathField( "##Atlas", atlasPath, "atlas", nullptr, nullptr )._bDeactivatedAfterEdit ) // 콘텐츠 브라우저에서 끌어 놓을 수 있다
            notifyDocumentEdited( "Edit Sprite Clip" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AtlasTip" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Load" ) ) )
            reloadDocument();
        EditorSelfTestMarks::record( "spriteClip.load" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "LoadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        // 저장 단추도 저장 경로 하나(`saveDocumentAndClearDirty`)를 지난다 — 예전 단추는 못 읽은 문서(Malformed)도 덮어쓰고, 경로가 비거나 쓰기에 실패해도 "저장했다" 를 띄웠다.
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 dirty 가 남고 이유는 상태 줄에 있다
        EditorSelfTestMarks::record( "spriteClip.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SaveTip" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "SeparateFromAnimationGraph", TextArgumentList().addText( "dirSavedEditor", config::kDirSavedEditor ).addText( "spriteClipDocumentFileName", EditorPaths::kSpriteClipDocumentFileName ) ).c_str() );
    }

    void SpriteClipPanel::drawEditedDragFloat( const utf8* pLabel, float32* pValue, float32 speed, const utf8* pTooltip, const utf8* pUndoLabel )
    {
        // 이름표는 왼쪽 열이다 — ImGui 기본(오른쪽 이름표)은 같은 창의 다른 칸과 줄이 맞지 않았다. 입력을 마칠 때 한 번 기록하므로 칸마다 되돌리기 하나다(묶음 키를 쓰면 다른 프레임의 편집까지 하나로 합쳐졌다).
        EditorWidgets::drawPropertyRowLabel( pLabel );
        ImGui::PushID( pLabel );
        ImGui::DragFloat( "##value", pValue, speed );
        ImGui::PopID();
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( pUndoLabel );
        if ( pTooltip != nullptr )
            EditorWidgets::drawTooltip( pTooltip );
    }

    void SpriteClipPanel::drawFrameSection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "FramesUVWHDurationMs" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "AddFrame" ) ) )
        {
            _model.addFrame();
            notifyDocumentEdited( "Add Sprite Frame" );
        }
        EditorSelfTestMarks::record( "spriteClip.addFrame" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RemoveFrame" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RemoveFrame" ) ) && _model.removeSelectedFrame() )
            notifyDocumentEdited( "Remove Sprite Frame" );
        EditorSelfTestMarks::record( "spriteClip.removeFrame" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorSpriteClip", "DuplicateFrame" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorSpriteClip", "DuplicateFrame" ) ) && _model.duplicateSelectedFrame() )
            notifyDocumentEdited( "Duplicate Sprite Frame" );
        EditorSelfTestMarks::record( "spriteClip.duplicateFrame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "DuplicateFrameTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorSpriteClip", "MoveFrameEarlier" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorSpriteClip", "MoveFrameEarlier" ) ) && _model.moveSelectedFrame( -1 ) )
            notifyDocumentEdited( "Move Sprite Frame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "MoveFrameEarlierTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorSpriteClip", "MoveFrameLater" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorSpriteClip", "MoveFrameLater" ) ) && _model.moveSelectedFrame( 1 ) )
            notifyDocumentEdited( "Move Sprite Frame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "MoveFrameLaterTip" ) );

        const int32 frameCount = static_cast<int32>( _model.getFrames().size() );
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            ImGui::PushID( frameIndex );
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "Frame %#", frameIndex );
            if ( ImGui::Selectable( label.c_str(), _model.getSelectedFrame() == frameIndex ) )
                _model.selectFrame( frameIndex );
            formatstring( label.data(), label.capacity(), "spriteClip.frame.%#", frameIndex );
            EditorSelfTestMarks::record( label.c_str() ); // 시나리오가 프레임을 고른다
            ImGui::PopID();
        }

        SpriteClipFrame* pFrame = _model.editSelectedFrame();
        if ( pFrame == nullptr )
            return;
        drawEditedDragFloat( "u", &pFrame->_uvRect._x, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameUTip" ), "Edit Sprite Frame" );
        drawEditedDragFloat( "v", &pFrame->_uvRect._y, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameVTip" ), "Edit Sprite Frame" );
        drawEditedDragFloat( "w", &pFrame->_uvRect._z, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameWTip" ), "Edit Sprite Frame" );
        drawEditedDragFloat( "h", &pFrame->_uvRect._w, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameHTip" ), "Edit Sprite Frame" );
        EditorWidgets::drawPropertyRowLabel( "durationMs" );
        ImGui::InputInt( "##durationMs", &pFrame->_durationMs );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Frame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "FrameDurationTip" ) );
        drawFrameBorderProperties( *pFrame );
    }

    void SpriteClipPanel::drawFrameBorderProperties( SpriteClipFrame& frame )
    {
        if ( EditorWidgets::beginAdvancedSection( "##advanced.spriteBorder" ) == false )
            return;
        float2& atlasSize = _model.editAtlasPixelSize();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorSpriteClip", "AtlasSize" ) );
        ImGui::InputFloat2( "##atlasSize", &atlasSize._x, "%.0f" );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitAtlasPixelSize() )
            notifyDocumentEdited( "Edit Sprite Atlas Size" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AtlasSizeTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorSpriteClip", "Border" ) );
        ImGui::InputFloat4( "##border", &frame._borderPixels._x, "%.0f" );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitSelectedFrameBorder() )
            notifyDocumentEdited( "Edit Sprite Frame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "BorderTip" ) );
    }

    void SpriteClipPanel::drawPreviewSection()
    {
        ImGui::Separator();
        if ( ImGui::Button( _model.isPreviewPlaying() ? SW_LOCKEY( "EdText", "StopPreview" ) : SW_LOCKEY( "EdText", "PlayPreview" ) ) )
            _model.togglePreview();
        EditorSelfTestMarks::record( "spriteClip.preview" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "PreviewTip" ) );
        if ( _model.isPreviewPlaying() )
        {
            _model.advancePreview( ImGui::GetIO().DeltaTime );
            requestAnimationFrame(); // 재생하는 동안 에디터 유휴로 들지 않는다
        }

        const int32 frameIndex = _model.getPreviewFrame();
        void*       pTexture   = _model.hasAtlasPath() ? _atlasThumbnail.findTexture( _model.getAtlasPath() ) : nullptr;
        if ( frameIndex < 0 || pTexture == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorSpriteClip", "PreviewEmpty" ) );
            return;
        }
        const float4& uvRect = _model.getFrames()[static_cast<size_t>( frameIndex )]._uvRect;
        const float32 side   = 96.0f * EditorThemeUtil::getDPIScale();
        ImGui::Image( reinterpret_cast<ImTextureID>( pTexture ), ImVec2( side, side ), ImVec2( uvRect._x, uvRect._y ), ImVec2( uvRect._x + uvRect._z, uvRect._y + uvRect._w ) );
    }

    void SpriteClipPanel::drawKeySection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "TransformAnimationKeysOptional" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdLabel", "AddKey" ) ) )
        {
            _model.addKey();
            notifyDocumentEdited( "Add Sprite Key" );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RemoveKey" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RemoveKey" ) ) && _model.removeSelectedKey() )
            notifyDocumentEdited( "Remove Sprite Key" );

        const int32 keyCount = static_cast<int32>( _model.getKeys().size() );
        for ( int32 keyIndex = 0; keyIndex < keyCount; ++keyIndex )
        {
            ImGui::PushID( 1000 + keyIndex );
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "Key %#", keyIndex );
            if ( ImGui::Selectable( label.c_str(), _model.getSelectedKey() == keyIndex ) )
                _model.selectKey( keyIndex );
            ImGui::PopID();
        }

        SpriteClipKey* pKey = _model.editSelectedKey();
        if ( pKey == nullptr )
            return;
        drawEditedDragFloat( "time", &pKey->_time, 0.01f, SW_LOCKEY( "EditorSpriteClip", "KeyTimeTip" ), "Edit Sprite Key" );
        drawEditedDragFloat( "x", &pKey->_position._x, 0.1f, nullptr, "Edit Sprite Key" );
        drawEditedDragFloat( "y", &pKey->_position._y, 0.1f, nullptr, "Edit Sprite Key" );
        // 키는 라디안으로 담고(엔진의 각도 단위) 편집은 도로 한다.
        float32 angleDeg = MathUtil::toDegree( pKey->_angle );
        EditorWidgets::drawPropertyRowLabel( "angleDeg" );
        if ( ImGui::DragFloat( "##angleDeg", &angleDeg, 0.5f ) )
            pKey->_angle = MathUtil::toRadian( angleDeg );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Key" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "KeyAngleTip" ) );
    }

    ToolAssetLoadResult SpriteClipPanel::loadDocument()
    {
        return _model.loadDocument( getLoadedAssetPath() );
    }

    void SpriteClipPanel::drawAtlasPicker()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "AtlasCellsClickSelectedFrameShiftClickNew" ) );
        EditorWidgets::drawHelpMarker( SW_LOCKEY( "EditorSpriteClip", "AtlasDragTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Columns" ) );
        ImGui::SetNextItemWidth( EditorChrome::measureStepperInput( 4.0f ) );
        ImGui::InputInt( "##Columns", &_model.editSliceColumns() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SliceColumnsTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Rows" ) );
        ImGui::SetNextItemWidth( EditorChrome::measureStepperInput( 4.0f ) );
        ImGui::InputInt( "##Rows", &_model.editSliceRows() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SliceRowsTip" ) );
        _model.clampSliceGrid();
        if ( ImGui::Button( SW_LOCKEY( "EditorSpriteClip", "AddAllCells" ) ) )
        {
            _model.addFramesFromGrid();
            notifyDocumentEdited( "Add Sprite Frames" );
        }
        EditorSelfTestMarks::record( "spriteClip.addAllCells" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AddAllCellsTip" ) );

        void* pTexture = _model.hasAtlasPath() ? _atlasThumbnail.findTexture( _model.getAtlasPath() ) : nullptr;
        if ( pTexture == nullptr )
        {
            EditorWidgets::drawEmptyHint( _model.hasAtlasPath() ? SW_LOCKEY( "EditorSpriteClip", "LoadingAtlas" ) : SW_LOCKEY( "EditorSpriteClip", "NoAtlas" ) );
            return;
        }

        const float32 side     = 256.0f * EditorThemeUtil::getDPIScale();
        const ImVec2  imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image( reinterpret_cast<ImTextureID>( pTexture ), ImVec2( side, side ) );
        const bool bHovered = ImGui::IsItemHovered();
        EditorSelfTestMarks::recordRect( "spriteClip.atlas", float2{ imageMin.x, imageMin.y }, float2{ imageMin.x + side, imageMin.y + side } );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AtlasCellTip" ) );

        // 셀 격자와 고른 프레임 사각형을 이미지 위에 그린다.
        const ImU32 gridColor    = EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "Label" ) );
        ImDrawList* pDrawList    = ImGui::GetWindowDrawList();
        const int32 sliceColumns = _model.getSliceColumns();
        const int32 sliceRows    = _model.getSliceRows();
        for ( int32 column = 1; column < sliceColumns; ++column )
        {
            const float32 x = imageMin.x + side * static_cast<float32>( column ) / static_cast<float32>( sliceColumns );
            pDrawList->AddLine( ImVec2( x, imageMin.y ), ImVec2( x, imageMin.y + side ), gridColor );
        }
        for ( int32 row = 1; row < sliceRows; ++row )
        {
            const float32 y = imageMin.y + side * static_cast<float32>( row ) / static_cast<float32>( sliceRows );
            pDrawList->AddLine( ImVec2( imageMin.x, y ), ImVec2( imageMin.x + side, y ), gridColor );
        }
        if ( const SpriteClipFrame* pFrame = _model.editSelectedFrame(); pFrame != nullptr )
        {
            const float4& uvRect = pFrame->_uvRect;
            pDrawList->AddRect( ImVec2( imageMin.x + uvRect._x * side, imageMin.y + uvRect._y * side ),
                                ImVec2( imageMin.x + ( uvRect._x + uvRect._z ) * side, imageMin.y + ( uvRect._y + uvRect._w ) * side ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Highlight" ) ),
                                0.0f, 0, 2.0f );
        }

        handleAtlasPointer( float2{ imageMin.x, imageMin.y }, side, bHovered );
    }

    optional<SpriteFrameEdge> SpriteClipPanel::findFrameEdgeNear( const float2& imageMin, float32 side, const float2& mouse ) const
    {
        float4 bounds{};
        if ( _model.findSelectedFrameBounds( bounds ) == false )
            return nullopt;
        const float32 left    = imageMin._x + bounds._x * side;
        const float32 top     = imageMin._y + bounds._y * side;
        const float32 right   = imageMin._x + bounds._z * side;
        const float32 bottom  = imageMin._y + bounds._w * side;
        const float32 middleX = ( left + right ) * 0.5f;
        const float32 middleY = ( top + bottom ) * 0.5f;
        // 잡는 폭은 글자 높이의 1/4 이다(DPI 를 따른다). 표시는 가장자리 가운데 작은 사각형이다.
        const float32 reach            = ImGui::GetFontSize() * 0.25f;
        const float2  arrEdgeCenter[4] = {
            float2{   left, middleY},
            float2{middleX,     top},
            float2{  right, middleY},
            float2{middleX,  bottom}
        };
        const utf8* const arrEdgeMark[4] = { "spriteClip.frameEdge.left", "spriteClip.frameEdge.top", "spriteClip.frameEdge.right", "spriteClip.frameEdge.bottom" };
        for ( uint32 edge = 0; edge < 4; ++edge )
        {
            const float2& center = arrEdgeCenter[edge];
            EditorSelfTestMarks::recordRect( arrEdgeMark[edge], float2{ center._x - reach, center._y - reach }, float2{ center._x + reach, center._y + reach } );
        }
        const bool bInsideY = top - reach <= mouse._y && mouse._y <= bottom + reach;
        const bool bInsideX = left - reach <= mouse._x && mouse._x <= right + reach;
        if ( bInsideY && MathUtil::abs( mouse._x - left ) <= reach )
            return SpriteFrameEdge::Left;
        if ( bInsideY && MathUtil::abs( mouse._x - right ) <= reach )
            return SpriteFrameEdge::Right;
        if ( bInsideX && MathUtil::abs( mouse._y - top ) <= reach )
            return SpriteFrameEdge::Top;
        if ( bInsideX && MathUtil::abs( mouse._y - bottom ) <= reach )
            return SpriteFrameEdge::Bottom;
        return nullopt;
    }

    void SpriteClipPanel::handleAtlasPointer( const float2& imageMin, float32 side, bool bHovered )
    {
        const EditorPointerState&       pointer = EditorInputGUI::readPointer();
        const float2                    mouse   = pointer._position;
        const float2                    uv{ ( mouse._x - imageMin._x ) / side, ( mouse._y - imageMin._y ) / side };
        const optional<SpriteFrameEdge> nearEdge = findFrameEdgeNear( imageMin, side, mouse );
        if ( bHovered && pointer.wasPressed( EditorMouseButton::Left ) )
        {
            _bAtlasPress = SW_TRUE;
            _bEdgeMoved  = SW_FALSE;
            _dragStartUV = uv;
            _dragEdge    = nearEdge;
        }
        if ( _bAtlasPress == SW_FALSE )
            return;
        const bool bDragging = pointer.isDragging( EditorMouseButton::Left );
        if ( bDragging && _dragEdge.has_value() )
        {
            const bool bHorizontal = *_dragEdge == SpriteFrameEdge::Left || *_dragEdge == SpriteFrameEdge::Right;
            if ( _model.moveSelectedFrameEdge( *_dragEdge, bHorizontal ? uv._x : uv._y ) )
                _bEdgeMoved = SW_TRUE;
        }
        else if ( bDragging )
        {
            // 끄는 사각형은 선택이라 강조색 틀이다(뗄 때 프레임이 된다).
            const float32 fromX = imageMin._x + MathUtil::saturate( _dragStartUV._x ) * side;
            const float32 fromY = imageMin._y + MathUtil::saturate( _dragStartUV._y ) * side;
            const float32 toX   = imageMin._x + MathUtil::saturate( uv._x ) * side;
            const float32 toY   = imageMin._y + MathUtil::saturate( uv._y ) * side;
            ImGui::GetWindowDrawList()->AddRect( ImVec2( MathUtil::min( fromX, toX ), MathUtil::min( fromY, toY ) ), ImVec2( MathUtil::max( fromX, toX ), MathUtil::max( fromY, toY ) ),
                                                 EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) ), 0.0f, 0, 1.5f );
        }
        if ( pointer.wasReleased( EditorMouseButton::Left ) == false )
            return;
        _bAtlasPress    = SW_FALSE;
        const bool bAdd = pointer.hasModifier( ModifierKey::Shift );
        if ( _dragEdge.has_value() && _bEdgeMoved == SW_TRUE )
        {
            notifyDocumentEdited( "Edit Sprite Frame" ); // 끄는 동안의 가장자리 이동은 되돌리기 하나다
        }
        else if ( pointer.wasClickedWithoutDrag( EditorMouseButton::Left ) )
        {
            const SpriteClipModel::AtlasPickResult result = _model.pickAtlasCell( _dragStartUV._x, _dragStartUV._y, bAdd );
            if ( result == SpriteClipModel::AtlasPickResult::AddedFrame )
                notifyDocumentEdited( "Add Sprite Frame" );
            else if ( result == SpriteClipModel::AtlasPickResult::EditedFrame )
                notifyDocumentEdited( "Edit Sprite Frame" );
        }
        else if ( _dragEdge.has_value() == false )
        {
            const SpriteClipModel::AtlasPickResult result = _model.setFrameRegion( _dragStartUV, uv, bAdd );
            if ( result == SpriteClipModel::AtlasPickResult::AddedFrame )
                notifyDocumentEdited( "Add Sprite Frame" );
            else if ( result == SpriteClipModel::AtlasPickResult::EditedFrame )
                notifyDocumentEdited( "Edit Sprite Frame" );
        }
        _dragEdge.reset();
    }

    void SpriteClipPanel::shutdown()
    {
        _atlasThumbnail.clear();
    }

    void SpriteClipPanel::drawAnimationSection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "AnimationsNameFirstFrameFrameCountLoopNone" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "AddAnimation" ) ) && _model.addAnimation() )
            notifyDocumentEdited( "Add Sprite Animation" );
        EditorSelfTestMarks::record( "spriteClip.addAnimation" );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RemoveAnimation" ) ) && _model.removeSelectedAnimation() )
            notifyDocumentEdited( "Remove Sprite Animation" );

        const vector<SpriteClipAnimation>& listAnimation  = _model.getAnimations();
        const int32                        animationCount = static_cast<int32>( listAnimation.size() );
        for ( int32 animationIndex = 0; animationIndex < animationCount; ++animationIndex )
        {
            ImGui::PushID( 2000 + animationIndex );
            if ( ImGui::Selectable( listAnimation[static_cast<size_t>( animationIndex )]._name.c_str(), _model.getSelectedAnimation() == animationIndex ) )
                _model.selectAnimation( animationIndex );
            ImGui::PopID();
        }

        SpriteClipAnimation* pAnimation = _model.editSelectedAnimation();
        if ( pAnimation == nullptr )
            return;
        fixed_string<constant::buffer::kMax64>& animationName = _model.editAnimationName();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Name" ) );
        ImGui::InputText( "##name", animationName.data(), animationName.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitAnimationName() )
            notifyDocumentEdited( "Edit Sprite Animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationNameTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "FirstFrame" ) );
        ImGui::InputInt( "##firstframe", &pAnimation->_firstFrame );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitAnimationRange() )
            notifyDocumentEdited( "Edit Sprite Animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationFirstFrameTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "FrameCount" ) );
        ImGui::InputInt( "##framecount", &pAnimation->_frameCount );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitAnimationRange() )
            notifyDocumentEdited( "Edit Sprite Animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationFrameCountTip" ) );
        bool bLoop = pAnimation->_bLoop == SW_TRUE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "LoopAlt" ), &bLoop ) )
        {
            pAnimation->_bLoop = bLoop ? SW_TRUE : SW_FALSE;
            notifyDocumentEdited( "Edit Sprite Animation" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationLoopTip" ) );
    }

    bool SpriteClipPanel::saveDocument()
    {
        // 포커스한 이미지로 연 클립은 이미지 옆의 새 클립 파일로 저장하고 그 파일을 연다 - 이미지를 덮어쓰면 안 된다.
        const string clipPath = SpriteClipModel::computeClipPath( getLoadedAssetPath() );
        if ( _model.saveDocument( clipPath ) == false )
            return false;
        syncDocumentUndoBaseline();
        if ( clipPath != getLoadedAssetPath() )
            requestOpenDocument( clipPath );
        return true;
    }

    string SpriteClipPanel::captureDocumentText() const
    {
        return _model.captureText();
    }

    void SpriteClipPanel::applyDocumentText( string_view text )
    {
        _model.applyText( text );
    }
} // namespace sw::editor
