#include "pch.h"

#include "Editor/Panels/Animation/SpriteClipPanel.h"

#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( SpriteClipPanel, SpriteClipPanel::kPanelID, EditorPanelCategory::Tool, 1600 );

    SpriteClipPanel::SpriteClipPanel()
        : EditorDocumentPanel{ EditorAssetType{ SpriteClipAsset::kAssetType }, false }
        , _model{}
        , _atlasThumbnail{}
    {
    }

    void SpriteClipPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( "spriteClip" );
        _atlasThumbnail.update();

        drawFileBar();
        drawFrameSection();
        drawAtlasPicker();

        drawAnimationSection();

        drawKeySection();

        EditorWidgets::drawPanelStatus( _model.getStatus().c_str() );
    }

    void SpriteClipPanel::drawFileBar()
    {
        fixed_string<constant::kMaxBuffer256>& atlasPath = _model.editAtlasPath();
        ImGui::InputText( "Atlas", atlasPath.data(), atlasPath.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Clip", "sprite-clip" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AtlasTip" ) );
        if ( ImGui::Button( "Load" ) )
            reloadDocument();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "LoadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        // 저장 단추도 저장 경로 하나(`saveDocumentAndClearDirty`)를 지난다 — 예전 단추는 못 읽은 문서(Malformed)도 덮어쓰고, 경로가 비거나 쓰기에 실패해도 "저장했다" 를 띄웠다.
        if ( ImGui::Button( "Save" ) )
            (void)saveDocumentAndClearDirty(); // 실패하면 dirty 가 남고 상태 줄은 그대로다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SaveTip" ) );
        ImGui::TextDisabled( "%s/%s (separate from AnimationGraph)", config::kDirSavedEditor, EditorPaths::kSpriteClipDocumentFileName );
    }

    void SpriteClipPanel::drawEditedDragFloat( const utf8* pLabel, float32* pValue, float32 speed, const utf8* pTooltip, const utf8* pUndoLabel, const utf8* pMergeKey )
    {
        ImGui::DragFloat( pLabel, pValue, speed );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( pUndoLabel, pMergeKey );
        if ( pTooltip != nullptr )
            EditorWidgets::drawTooltip( pTooltip );
    }

    void SpriteClipPanel::drawFrameSection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "Frames (u,v,w,h,durationMs)" );
        if ( ImGui::Button( "Add Frame" ) )
        {
            _model.addFrame();
            notifyDocumentEdited( "Add Sprite Frame" );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove Frame" ) );
        if ( ImGui::Button( "Remove Frame" ) && _model.removeSelectedFrame() )
            notifyDocumentEdited( "Remove Sprite Frame" );

        const int32 frameCount = static_cast<int32>( _model.getFrames().size() );
        for ( int32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            ImGui::PushID( frameIndex );
            fixed_string<constant::kMaxBuffer32> label;
            formatstring( label.data(), label.capacity(), "Frame %#", frameIndex );
            if ( ImGui::Selectable( label.c_str(), _model.getSelectedFrame() == frameIndex ) )
                _model.selectFrame( frameIndex );
            ImGui::PopID();
        }

        SpriteClipFrame* pFrame = _model.editSelectedFrame();
        if ( pFrame == nullptr )
            return;
        drawEditedDragFloat( "u", &pFrame->_uvRect._x, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameUTip" ), "Edit Sprite Frame", "sprite-clip-frame" );
        drawEditedDragFloat( "v", &pFrame->_uvRect._y, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameVTip" ), "Edit Sprite Frame", "sprite-clip-frame" );
        drawEditedDragFloat( "w", &pFrame->_uvRect._z, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameWTip" ), "Edit Sprite Frame", "sprite-clip-frame" );
        drawEditedDragFloat( "h", &pFrame->_uvRect._w, 0.01f, SW_LOCKEY( "EditorSpriteClip", "FrameHTip" ), "Edit Sprite Frame", "sprite-clip-frame" );
        ImGui::InputInt( "durationMs", &pFrame->_durationMs );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "FrameDurationTip" ) );
    }

    void SpriteClipPanel::drawKeySection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "TransformAnimation Keys (optional)" );
        if ( ImGui::Button( "Add Key" ) )
        {
            _model.addKey();
            notifyDocumentEdited( "Add Sprite Key" );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove Key" ) );
        if ( ImGui::Button( "Remove Key" ) && _model.removeSelectedKey() )
            notifyDocumentEdited( "Remove Sprite Key" );

        const int32 keyCount = static_cast<int32>( _model.getKeys().size() );
        for ( int32 keyIndex = 0; keyIndex < keyCount; ++keyIndex )
        {
            ImGui::PushID( 1000 + keyIndex );
            fixed_string<constant::kMaxBuffer32> label;
            formatstring( label.data(), label.capacity(), "Key %#", keyIndex );
            if ( ImGui::Selectable( label.c_str(), _model.getSelectedKey() == keyIndex ) )
                _model.selectKey( keyIndex );
            ImGui::PopID();
        }

        SpriteClipKey* pKey = _model.editSelectedKey();
        if ( pKey == nullptr )
            return;
        drawEditedDragFloat( "time", &pKey->_time, 0.01f, SW_LOCKEY( "EditorSpriteClip", "KeyTimeTip" ), "Edit Sprite Key", "sprite-clip-key" );
        drawEditedDragFloat( "x", &pKey->_position._x, 0.1f, nullptr, "Edit Sprite Key", "sprite-clip-key" );
        drawEditedDragFloat( "y", &pKey->_position._y, 0.1f, nullptr, "Edit Sprite Key", "sprite-clip-key" );
        // 키는 라디안으로 담고(엔진의 각도 단위) 편집은 도로 한다.
        float32 angleDeg = MathUtil::toDegree( pKey->_angle );
        if ( ImGui::DragFloat( "angleDeg", &angleDeg, 0.5f ) )
            pKey->_angle = MathUtil::toRadian( angleDeg );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "KeyAngleTip" ) );
    }

    ToolAssetLoadResult SpriteClipPanel::loadDocument()
    {
        return _model.loadDocument( getLoadedAssetPath() );
    }

    void SpriteClipPanel::drawAtlasPicker()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "Atlas cells (click: selected frame, Shift+click: new frame)" );
        EditorChrome::setNextItemWidthColumns( 12.0f );
        ImGui::InputInt( "Columns", &_model.editSliceColumns() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SliceColumnsTip" ) );
        ImGui::SameLine();
        EditorChrome::setNextItemWidthColumns( 12.0f );
        ImGui::InputInt( "Rows", &_model.editSliceRows() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "SliceRowsTip" ) );
        _model.clampSliceGrid();

        void* pTexture = _model.hasAtlasPath() ? _atlasThumbnail.findTexture( _model.getAtlasPath() ) : nullptr;
        if ( pTexture == nullptr )
        {
            ImGui::TextDisabled( "%s", _model.hasAtlasPath() ? SW_LOCKEY( "EditorSpriteClip", "LoadingAtlas" ) : SW_LOCKEY( "EditorSpriteClip", "NoAtlas" ) );
            return;
        }

        const float32 side     = 256.0f * EditorThemeUtil::getDPIScale();
        const ImVec2  imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image( reinterpret_cast<ImTextureID>( pTexture ), ImVec2( side, side ) );
        const bool bHovered = ImGui::IsItemHovered();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AtlasCellTip" ) );

        // 셀 격자와 고른 프레임 사각형을 이미지 위에 그린다.
        static constexpr Color kSliceGrid{ Color::fromBytes( 255, 255, 255, 90 ) };
        ImDrawList*            pDrawList    = ImGui::GetWindowDrawList();
        const int32            sliceColumns = _model.getSliceColumns();
        const int32            sliceRows    = _model.getSliceRows();
        for ( int32 column = 1; column < sliceColumns; ++column )
        {
            const float32 x = imageMin.x + side * static_cast<float32>( column ) / static_cast<float32>( sliceColumns );
            pDrawList->AddLine( ImVec2( x, imageMin.y ), ImVec2( x, imageMin.y + side ), EditorColorUtil::toImU32( kSliceGrid ) );
        }
        for ( int32 row = 1; row < sliceRows; ++row )
        {
            const float32 y = imageMin.y + side * static_cast<float32>( row ) / static_cast<float32>( sliceRows );
            pDrawList->AddLine( ImVec2( imageMin.x, y ), ImVec2( imageMin.x + side, y ), EditorColorUtil::toImU32( kSliceGrid ) );
        }
        if ( const SpriteClipFrame* pFrame = _model.editSelectedFrame(); pFrame != nullptr )
        {
            const float4& uvRect = pFrame->_uvRect;
            pDrawList->AddRect( ImVec2( imageMin.x + uvRect._x * side, imageMin.y + uvRect._y * side ),
                                ImVec2( imageMin.x + ( uvRect._x + uvRect._z ) * side, imageMin.y + ( uvRect._y + uvRect._w ) * side ), EditorColorUtil::toImU32( EditorThemeUtil::getHighlightColor() ),
                                0.0f, 0, 2.0f );
        }

        if ( bHovered == false || EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) == false )
            return;
        const ImVec2                           mouse  = ImGui::GetMousePos();
        const SpriteClipModel::AtlasPickResult result = _model.pickAtlasCell( ( mouse.x - imageMin.x ) / side, ( mouse.y - imageMin.y ) / side,
                                                                              EditorInputGUI::readPointer().hasModifier( ModifierKey::Shift ) );
        if ( result == SpriteClipModel::AtlasPickResult::AddedFrame )
            notifyDocumentEdited( "Add Sprite Frame" );
        else if ( result == SpriteClipModel::AtlasPickResult::EditedFrame )
            notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
    }

    void SpriteClipPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _atlasThumbnail.clear();
    }

    void SpriteClipPanel::drawAnimationSection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "Animations (name, first frame, frame count, loop) - none means the whole clip is one loop" );
        if ( ImGui::Button( "Add Animation" ) )
        {
            _model.addAnimation();
            notifyDocumentEdited( "Add Sprite Animation" );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Remove Animation" ) && _model.removeSelectedAnimation() )
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
        fixed_string<constant::kMaxBuffer64>& animationName = _model.editAnimationName();
        ImGui::InputText( "name", animationName.data(), animationName.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() && _model.commitAnimationName() )
            notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationNameTip" ) );
        ImGui::InputInt( "first frame", &pAnimation->_firstFrame );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationFirstFrameTip" ) );
        ImGui::InputInt( "frame count", &pAnimation->_frameCount );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationFrameCountTip" ) );
        bool bLoop = pAnimation->_bLoop == SW_TRUE;
        if ( ImGui::Checkbox( "loop", &bLoop ) )
        {
            pAnimation->_bLoop = bLoop ? SW_TRUE : SW_FALSE;
            notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSpriteClip", "AnimationLoopTip" ) );
    }

    bool SpriteClipPanel::saveDocument()
    {
        if ( _model.saveDocument( getLoadedAssetPath() ) == false )
            return false;
        syncDocumentUndoBaseline();
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
