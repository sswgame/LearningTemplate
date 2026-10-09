#include "pch.h"

#include "Editor/Panels/SpriteClipPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "sw/config/ConfigConstants.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "SpriteClip" );
    SW_EDITOR_PANEL( SpriteClipPanel, "sprite_clip", EditorPanelCategory::Tool, 1600 );

    SpriteClipPanel::SpriteClipPanel()
        : EditorDocumentPanel{ EditorAssetType::SpriteClip, false }
        , _atlasPath{}
        , _status{}
        , _listFrame{}
        , _listKey{}
        , _listAnimation{}
        , _animationName{}
        , _selectedFrame{ -1 }
        , _selectedKey{ -1 }
        , _selectedAnimation{ -1 }
    {
        const string& atlas = editor::getEditorToolDefaults()._spriteAtlas;
        if ( atlas.empty() == false )
            _atlasPath = atlas.c_str();
        _listFrame.push_back( Frame{} );
    }

    void SpriteClipPanel::drawContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        ImGui::InputText( "Atlas", _atlasPath.data(), _atlasPath.capacity() );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Sprite Clip", "sprite-clip" );
        if ( ImGui::Button( "Load" ) )
            reloadDocument();
        ImGui::SameLine();
        if ( ImGui::Button( "Save" ) )
        {
            saveJson();
            if ( getLoadedAssetPath().empty() )
                _status = string{ "Saved " } + EditorUtil::kSpriteClipDocumentFileName;
            else
                _status = string{ "Saved " } + getLoadedAssetPath();
        }
        ImGui::TextDisabled( "%s/%s (separate from AnimGraph)", config::kDirSavedEditor, EditorUtil::kSpriteClipDocumentFileName );

        ImGui::Separator();
        ImGui::TextUnformatted( "Frames (u,v,w,h,durationMs)" );
        if ( ImGui::Button( "Add Frame" ) )
        {
            _listFrame.push_back( Frame{} );
            _selectedFrame = static_cast<int32>( _listFrame.size() ) - 1;
            notifyDocumentEdited( "Add Sprite Frame" );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Remove Frame" ) && 0 <= _selectedFrame &&
             _selectedFrame < static_cast<int32>( _listFrame.size() ) )
        {
            _listFrame.erase( _listFrame.begin() + _selectedFrame );
            if ( _selectedFrame >= static_cast<int32>( _listFrame.size() ) )
                _selectedFrame = static_cast<int32>( _listFrame.size() ) - 1;
            notifyDocumentEdited( "Remove Sprite Frame" );
        }

        for ( int32 frameIndex = 0; frameIndex < static_cast<int32>( _listFrame.size() ); ++frameIndex )
        {
            ImGui::PushID( frameIndex );
            fixed_string<constant::kMaxBuffer32> label;
            formatstring( label.data(), label.capacity(), "Frame %#", frameIndex );
            if ( ImGui::Selectable( label.c_str(), _selectedFrame == frameIndex ) )
                _selectedFrame = frameIndex;
            ImGui::PopID();
        }

        if ( 0 <= _selectedFrame && _selectedFrame < static_cast<int32>( _listFrame.size() ) )
        {
            Frame& f = _listFrame[static_cast<size_t>( _selectedFrame )];
            ImGui::DragFloat( "u", &f._uvRect._x, 0.01f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
            ImGui::DragFloat( "v", &f._uvRect._y, 0.01f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
            ImGui::DragFloat( "w", &f._uvRect._z, 0.01f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
            ImGui::DragFloat( "h", &f._uvRect._w, 0.01f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
            ImGui::InputInt( "durationMs", &f._durationMs );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Frame", "sprite-clip-frame" );
        }

        drawAnimationSection();

        ImGui::Separator();
        ImGui::TextUnformatted( "TransformAnimation Keys (optional)" );
        if ( ImGui::Button( "Add Key" ) )
        {
            _listKey.push_back( TransformKey{} );
            _selectedKey = static_cast<int32>( _listKey.size() ) - 1;
            notifyDocumentEdited( "Add Sprite Key" );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Remove Key" ) && 0 <= _selectedKey &&
             _selectedKey < static_cast<int32>( _listKey.size() ) )
        {
            _listKey.erase( _listKey.begin() + _selectedKey );
            if ( _selectedKey >= static_cast<int32>( _listKey.size() ) )
                _selectedKey = static_cast<int32>( _listKey.size() ) - 1;
            notifyDocumentEdited( "Remove Sprite Key" );
        }

        for ( int32 keyIndex = 0; keyIndex < static_cast<int32>( _listKey.size() ); ++keyIndex )
        {
            ImGui::PushID( 1000 + keyIndex );
            fixed_string<constant::kMaxBuffer32> label;
            formatstring( label.data(), label.capacity(), "Key %#", keyIndex );
            if ( ImGui::Selectable( label.c_str(), _selectedKey == keyIndex ) )
                _selectedKey = keyIndex;
            ImGui::PopID();
        }

        if ( 0 <= _selectedKey && _selectedKey < static_cast<int32>( _listKey.size() ) )
        {
            TransformKey& k = _listKey[static_cast<size_t>( _selectedKey )];
            ImGui::DragFloat( "time", &k._time, 0.01f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
            ImGui::DragFloat( "x", &k._position._x, 0.1f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
            ImGui::DragFloat( "y", &k._position._y, 0.1f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
            ImGui::DragFloat( "angleDeg", &k._angleDeg, 0.5f );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Key", "sprite-clip-key" );
        }

        EditorWidgets::drawPanelStatus( _status.c_str() );
    }

    ToolAssetLoadResult SpriteClipPanel::loadDocument()
    {
        if ( EditorAssetTypeRegistry::matches( EditorAssetType::Texture, getLoadedAssetPath().c_str() ) )
        {
            _atlasPath = getLoadedAssetPath().c_str();
            return ToolAssetLoadResult::Loaded;
        }

        SpriteClipAsset           data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadSpriteClip( data, _status, getLoadedAssetPath() );
        if ( result != ToolAssetLoadResult::Loaded )
            return result;

        adoptClip( std::move( data ) );
        return ToolAssetLoadResult::Loaded;
    }

    void SpriteClipPanel::adoptClip( SpriteClipAsset&& clip )
    {
        if ( clip._atlasPath.empty() == false )
            _atlasPath = clip._atlasPath.c_str();
        _listFrame         = std::move( clip._listFrame );
        _listKey           = std::move( clip._listKey );
        _listAnimation     = std::move( clip._listAnimation );
        _selectedFrame     = _listFrame.empty() ? -1 : 0;
        _selectedKey       = _listKey.empty() ? -1 : 0;
        _selectedAnimation = _listAnimation.empty() ? -1 : 0;
        _animationName     = ( _selectedAnimation >= 0 ) ? _listAnimation[0]._name.c_str() : "";
    }

    void SpriteClipPanel::drawAnimationSection()
    {
        ImGui::Separator();
        ImGui::TextUnformatted( "Animations (name, first frame, frame count, loop) - none means the whole clip is one loop" );
        if ( ImGui::Button( "Add Animation" ) )
        {
            Animation animation{};
            animation._name       = "anim" + to_string( _listAnimation.size() );
            animation._frameCount = static_cast<int32>( _listFrame.size() );
            _listAnimation.push_back( std::move( animation ) );
            _selectedAnimation = static_cast<int32>( _listAnimation.size() ) - 1;
            _animationName     = _listAnimation.back()._name.c_str();
            notifyDocumentEdited( "Add Sprite Animation" );
        }
        ImGui::SameLine();
        const bool bHasSelection = 0 <= _selectedAnimation && _selectedAnimation < static_cast<int32>( _listAnimation.size() );
        if ( ImGui::Button( "Remove Animation" ) && bHasSelection )
        {
            _listAnimation.erase( _listAnimation.begin() + _selectedAnimation );
            if ( _selectedAnimation >= static_cast<int32>( _listAnimation.size() ) )
                _selectedAnimation = static_cast<int32>( _listAnimation.size() ) - 1;
            _animationName = ( _selectedAnimation >= 0 ) ? _listAnimation[static_cast<size_t>( _selectedAnimation )]._name.c_str() : "";
            notifyDocumentEdited( "Remove Sprite Animation" );
        }

        for ( int32 animationIndex = 0; animationIndex < static_cast<int32>( _listAnimation.size() ); ++animationIndex )
        {
            ImGui::PushID( 2000 + animationIndex );
            if ( ImGui::Selectable( _listAnimation[static_cast<size_t>( animationIndex )]._name.c_str(), _selectedAnimation == animationIndex ) )
            {
                _selectedAnimation = animationIndex;
                _animationName     = _listAnimation[static_cast<size_t>( animationIndex )]._name.c_str();
            }
            ImGui::PopID();
        }

        if ( 0 <= _selectedAnimation && _selectedAnimation < static_cast<int32>( _listAnimation.size() ) )
        {
            Animation& animation = _listAnimation[static_cast<size_t>( _selectedAnimation )];
            ImGui::InputText( "name", _animationName.data(), _animationName.capacity() );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
            {
                animation._name = _animationName.c_str();
                notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
            }
            ImGui::InputInt( "first frame", &animation._firstFrame );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
            ImGui::InputInt( "frame count", &animation._frameCount );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
            bool bLoop = animation._bLoop == SW_TRUE;
            if ( ImGui::Checkbox( "loop", &bLoop ) )
            {
                animation._bLoop = bLoop ? SW_TRUE : SW_FALSE;
                notifyDocumentEdited( "Edit Sprite Animation", "sprite-clip-animation" );
            }
        }
    }

    void SpriteClipPanel::saveJson()
    {
        if ( getLoadedAssetPath().empty() )
            return;
        if ( EditorToolAssetCommands::saveSpriteClip( captureClipData(), getLoadedAssetPath() ) )
        {
            clearDocumentDirty();
            syncDocumentUndoBaseline();
        }
    }

    bool SpriteClipPanel::saveDocument()
    {
        if ( getLoadedAssetPath().empty() )
            return false;
        if ( EditorToolAssetCommands::saveSpriteClip( captureClipData(), getLoadedAssetPath() ) )
        {
            clearDocumentDirty();
            syncDocumentUndoBaseline();
            return true;
        }
        return false;
    }

    SpriteClipAsset SpriteClipPanel::captureClipData() const
    {
        SpriteClipAsset data;
        data._atlasPath     = _atlasPath.c_str();
        data._listFrame     = _listFrame;
        data._listKey       = _listKey;
        data._listAnimation = _listAnimation;
        return data;
    }

    string SpriteClipPanel::captureDocumentText() const
    {
        return captureClipData().toJson();
    }

    void SpriteClipPanel::applyDocumentText( string_view text )
    {
        SpriteClipAsset restored;
        if ( text.empty() == false && restored.parseJson( text ) == false )
            SW_LOG_WARNING( "Sprite clip undo snapshot could not be read - showing an empty clip" );
        adoptClip( std::move( restored ) );
    }
} // namespace sw::editor
