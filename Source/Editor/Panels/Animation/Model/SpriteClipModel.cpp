#include "pch.h"

#include "Editor/Panels/Animation/Model/SpriteClipModel.h"

#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    SW_LOG_CALLER( "SpriteClip" );

    namespace
    {
        /** @brief @p index 가 @p list 범위 안이면 true 입니다. */
        template <typename T>
        bool isValidIndex( const vector<T>& list, int32 index )
        {
            return 0 <= index && index < static_cast<int32>( list.size() );
        }

        /** @brief @p list 에서 @p rSelected 를 지우고 고름을 범위 안(끝이면 하나 앞)으로 둡니다. 고른 것이 없으면 false 입니다. */
        template <typename T>
        bool eraseSelected( vector<T>& list, int32& rSelected )
        {
            if ( isValidIndex( list, rSelected ) == false )
                return false;
            list.erase( list.begin() + rSelected );
            if ( rSelected >= static_cast<int32>( list.size() ) )
                rSelected = static_cast<int32>( list.size() ) - 1;
            return true;
        }
    } // namespace

    SpriteClipModel::SpriteClipModel()
        : _atlasPath{}
        , _status{}
        , _listFrame{}
        , _listKey{}
        , _listAnimation{}
        , _animationName{}
        , _selectedFrame{ -1 }
        , _selectedKey{ -1 }
        , _selectedAnimation{ -1 }
        , _sliceColumns{ 4 }
        , _sliceRows{ 4 }
    {
        const DataPath& atlas = editor::getEditorToolDefaults()._spriteAtlas;
        if ( atlas.isSet() )
            _atlasPath = atlas.c_str();
        _listFrame.push_back( SpriteClipFrame{} );
    }

    ToolAssetLoadResult SpriteClipModel::loadDocument( string_view path )
    {
        if ( EditorAssetTypeRegistry::matches( EditorAssetType{ Texture2D::kAssetType }, path ) )
        {
            _atlasPath = path;
            return ToolAssetLoadResult::Loaded;
        }

        SpriteClipAsset           data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadSpriteClip( data, _status, path );
        if ( result != ToolAssetLoadResult::Loaded )
            return result;

        adoptClip( std::move( data ) );
        return ToolAssetLoadResult::Loaded;
    }

    bool SpriteClipModel::saveDocument( string_view path )
    {
        if ( path.empty() )
            return false;
        if ( EditorToolAssetCommands::saveSpriteClip( captureClipData(), path ) == false )
            return false;
        _status = SW_LOCKEYFORMAT( "EditorSpriteClip", "SavedStatus", TextArgumentList().addText( "path", path ) );
        return true;
    }

    string SpriteClipModel::captureText() const
    {
        return captureClipData().toXML();
    }

    void SpriteClipModel::applyText( string_view text )
    {
        SpriteClipAsset restored;
        if ( text.empty() == false && restored.parseXML( text ) == false )
            SW_LOG_WARNING( "Sprite clip undo snapshot could not be read - showing an empty clip" );
        adoptClip( std::move( restored ) );
    }

    SpriteClipFrame* SpriteClipModel::editSelectedFrame()
    {
        return isValidIndex( _listFrame, _selectedFrame ) ? &_listFrame[static_cast<size_t>( _selectedFrame )] : nullptr;
    }

    void SpriteClipModel::addFrame()
    {
        _listFrame.push_back( SpriteClipFrame{} );
        _selectedFrame = static_cast<int32>( _listFrame.size() ) - 1;
    }

    bool SpriteClipModel::removeSelectedFrame()
    {
        return eraseSelected( _listFrame, _selectedFrame );
    }

    SpriteClipKey* SpriteClipModel::editSelectedKey()
    {
        return isValidIndex( _listKey, _selectedKey ) ? &_listKey[static_cast<size_t>( _selectedKey )] : nullptr;
    }

    void SpriteClipModel::addKey()
    {
        _listKey.push_back( SpriteClipKey{} );
        _selectedKey = static_cast<int32>( _listKey.size() ) - 1;
    }

    bool SpriteClipModel::removeSelectedKey()
    {
        return eraseSelected( _listKey, _selectedKey );
    }

    void SpriteClipModel::selectAnimation( int32 animationIndex )
    {
        _selectedAnimation = isValidIndex( _listAnimation, animationIndex ) ? animationIndex : -1;
        _animationName     = ( _selectedAnimation >= 0 ) ? _listAnimation[static_cast<size_t>( _selectedAnimation )]._name.c_str() : "";
    }

    SpriteClipAnimation* SpriteClipModel::editSelectedAnimation()
    {
        return isValidIndex( _listAnimation, _selectedAnimation ) ? &_listAnimation[static_cast<size_t>( _selectedAnimation )] : nullptr;
    }

    void SpriteClipModel::addAnimation()
    {
        SpriteClipAnimation animation{};
        animation._name       = formatToString( "anim%#", _listAnimation.size() );
        animation._frameCount = static_cast<int32>( _listFrame.size() );
        _listAnimation.push_back( std::move( animation ) );
        selectAnimation( static_cast<int32>( _listAnimation.size() ) - 1 );
    }

    bool SpriteClipModel::removeSelectedAnimation()
    {
        if ( eraseSelected( _listAnimation, _selectedAnimation ) == false )
            return false;
        selectAnimation( _selectedAnimation );
        return true;
    }

    bool SpriteClipModel::commitAnimationName()
    {
        SpriteClipAnimation* pAnimation = editSelectedAnimation();
        if ( pAnimation == nullptr )
            return false;
        pAnimation->_name = _animationName.c_str();
        return true;
    }

    void SpriteClipModel::clampSliceGrid()
    {
        _sliceColumns = MathUtil::clamp( _sliceColumns, 1, kMaxSliceCount );
        _sliceRows    = MathUtil::clamp( _sliceRows, 1, kMaxSliceCount );
    }

    SpriteClipModel::AtlasPickResult SpriteClipModel::pickAtlasCell( float32 u, float32 v, bool bAddFrame )
    {
        const int32 cell = AtlasGridUtil::findCellAtUv( u, v, _sliceColumns, _sliceRows );
        if ( cell < 0 )
            return AtlasPickResult::None;
        const bool bAdded = bAddFrame || isValidIndex( _listFrame, _selectedFrame ) == false;
        if ( bAdded )
            addFrame();
        _listFrame[static_cast<size_t>( _selectedFrame )]._uvRect = AtlasGridUtil::computeCellUvRect( cell, _sliceColumns, _sliceRows );
        return bAdded ? AtlasPickResult::AddedFrame : AtlasPickResult::EditedFrame;
    }

    SpriteClipAsset SpriteClipModel::captureClipData() const
    {
        SpriteClipAsset data;
        data._atlasPath     = _atlasPath.c_str();
        data._listFrame     = _listFrame;
        data._listKey       = _listKey;
        data._listAnimation = _listAnimation;
        return data;
    }

    void SpriteClipModel::adoptClip( SpriteClipAsset&& clip )
    {
        if ( clip._atlasPath.empty() == false )
            _atlasPath = clip._atlasPath.c_str();
        _listFrame     = std::move( clip._listFrame );
        _listKey       = std::move( clip._listKey );
        _listAnimation = std::move( clip._listAnimation );
        _selectedFrame = _listFrame.empty() ? -1 : 0;
        _selectedKey   = _listKey.empty() ? -1 : 0;
        selectAnimation( _listAnimation.empty() ? -1 : 0 );
    }
} // namespace sw::editor
