#include "pch.h"

#include "Editor/Panels/Animation/Model/SpriteClipViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/TileMap/Model/TileMapPaintUtil.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

namespace sw::editor
{
    namespace
    {
        struct SpriteClipViewModelInternal
        {
            /** @brief @p index 가 @p list 범위 안이면 true 입니다. */
            template <typename T>
            [[nodiscard]] static bool isValidIndex( const vector<T>& list, int32 index )
            {
                return 0 <= index && index < static_cast<int32>( list.size() );
            }

            /** @brief @p list 에서 @p rSelected 를 지우고 고름을 범위 안(끝이면 하나 앞)으로 둡니다. 고른 것이 없으면 false 입니다. */
            template <typename T>
            [[nodiscard]] static bool eraseSelected( vector<T>& list, int32& rSelected )
            {
                if ( isValidIndex( list, rSelected ) == false )
                    return false;
                list.erase( list.begin() + rSelected );
                rSelected = MathUtil::min( rSelected, static_cast<int32>( list.size() ) - 1 );
                return true;
            }

            /** @brief 에디터 설정의 기본 아틀라스입니다. 칸이 없으면 빈 글입니다. */
            [[nodiscard]] static string_view findDefaultAtlas()
            {
                const DataPath& atlas = editor::getEditorToolDefaults()._spriteAtlas;
                return atlas.isSet() ? string_view{ atlas.c_str() } : string_view{};
            }

            /** @brief 스프라이트 클립 문서가 아닌 이미지 경로인지입니다. */
            [[nodiscard]] static bool isImagePath( string_view path ) { return EditorAssetTypeRegistry::matches( EditorAssetType{ Texture2D::kAssetType }, path ); }
        };
    } // namespace

    SpriteClipViewModel::SpriteClipViewModel()
        : _clip{}
        , _atlasPath{}
        , _status{}
        , _animationName{}
        , _atlasPixelSizeInput{ 0.0f, 0.0f }
        , _selectedFrame{ -1 }
        , _selectedKey{ -1 }
        , _selectedAnimation{ -1 }
        , _sliceColumns{ 4 }
        , _sliceRows{ 4 }
        , _previewSeconds{ 0.0f }
        , _bPreviewPlaying{ SW_FALSE }
    {
        applyClip( makeNewClip( SpriteClipViewModelInternal::findDefaultAtlas() ) );
    }

    string SpriteClipViewModel::computeClipPath( string_view path )
    {
        if ( SpriteClipViewModelInternal::isImagePath( path ) == false )
            return string{ path };
        StringBuilder<constant::buffer::kMax256> pathBuilder;
        pathBuilder.appendFormat( "%#%#", FileUtil::removeExtension( path ), SpriteClipAsset::kExtension );
        return string( pathBuilder.view() );
    }

    SpriteClipAsset SpriteClipViewModel::makeNewClip( string_view atlasPath )
    {
        SpriteClipAsset clip;
        clip._atlasPath = string{ atlasPath };
        clip._listFrame.push_back( SpriteClipFrame{} );
        return clip;
    }

    ToolAssetLoadResult SpriteClipViewModel::loadDocument( string_view path )
    {
        SpriteClipAsset           data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadSpriteClip( data, _status, path ); // 이미지는 그 아틀라스의 빈 클립으로 온다
        if ( result == ToolAssetLoadResult::Malformed )
            return result; // 저장은 기반이 막는다

        // 새 문서는 맨 앞 항목을 고른다(범위는 `applyClip` 이 묶는다).
        _selectedFrame     = 0;
        _selectedKey       = 0;
        _selectedAnimation = 0;
        _bPreviewPlaying   = SW_FALSE;
        if ( result == ToolAssetLoadResult::Missing )
        {
            applyClip( makeNewClip( SpriteClipViewModelInternal::findDefaultAtlas() ) );
        }
        else if ( SpriteClipViewModelInternal::isImagePath( path ) )
        {
            applyClip( makeNewClip( data._atlasPath ) );
        }
        else
        {
            applyClip( std::move( data ) );
        }
        return result;
    }

    bool SpriteClipViewModel::saveDocument( string_view path )
    {
        // 패널은 `computeClipPath` 를 거쳐 부른다 - 여기가 이미지를 덮는 마지막 관문이다.
        const bool bSaved = SpriteClipViewModelInternal::isImagePath( path ) == false && EditorToolAssetCommands::saveSpriteClip( captureClipData(), path );
        if ( bSaved == false )
        {
            _status = SW_LOCKEY( "EditorSpriteClip", "SaveFailedStatus" );
            return false;
        }
        _status = SW_LOCKEYFORMAT( "EditorSpriteClip", "SavedStatus", TextArgumentList().addText( "path", path.empty() ? string_view{ EditorPaths::kSpriteClipDocumentFileName } : path ) );
        return true;
    }

    string SpriteClipViewModel::captureText() const
    {
        return captureClipData().toXML();
    }

    void SpriteClipViewModel::applyText( string_view text )
    {
        SpriteClipAsset restored;
        if ( text.empty() == false && restored.loadFromXMLText( text ) == false )
            SW_LOG_WARNING_ID( "SpriteClip.SpriteClipUndoSnapshot", "Sprite clip undo snapshot could not be read - showing an empty clip" );
        applyClip( std::move( restored ) );
    }

    SpriteClipFrame* SpriteClipViewModel::editSelectedFrame()
    {
        return SpriteClipViewModelInternal::isValidIndex( _clip._listFrame, _selectedFrame ) ? &_clip._listFrame[static_cast<size_t>( _selectedFrame )] : nullptr;
    }

    void SpriteClipViewModel::appendFrame()
    {
        _clip._listFrame.push_back( SpriteClipFrame{} );
        _selectedFrame = _clip.getFrameCount() - 1;
    }

    bool SpriteClipViewModel::applyRemoveSelectedFrame()
    {
        const int32 removedFrame = _selectedFrame;
        if ( SpriteClipViewModelInternal::eraseSelected( _clip._listFrame, _selectedFrame ) == false )
            return false;
        for ( SpriteClipAnimation& animation : _clip._listAnimation )
        {
            if ( removedFrame < animation._firstFrame )
                --animation._firstFrame;
            else if ( removedFrame < animation._firstFrame + animation._frameCount )
                --animation._frameCount;
        }
        // 앞쪽의 빈 구간이 사라지면 고른 구간의 번호도 당긴다 — 그대로 두면 다음 구간이 골라져 거기에 편집이 들어간다.
        int32 removedBefore = 0;
        for ( int32 animationIndex = 0; animationIndex < _selectedAnimation; ++animationIndex )
        {
            if ( _clip._listAnimation[static_cast<size_t>( animationIndex )]._frameCount <= 0 )
                ++removedBefore;
        }
        _selectedAnimation -= removedBefore;
        VectorUtil::removeIf( _clip._listAnimation, []( const SpriteClipAnimation& animation )
        { return animation._frameCount <= 0; } );
        selectAnimation( MathUtil::min( _selectedAnimation, static_cast<int32>( _clip._listAnimation.size() ) - 1 ) );
        return true;
    }

    bool SpriteClipViewModel::applyDuplicateSelectedFrame()
    {
        const SpriteClipFrame* pFrame = editSelectedFrame();
        if ( pFrame == nullptr )
            return false;
        const SpriteClipFrame copy        = *pFrame;
        const int32           insertIndex = _selectedFrame + 1;
        _clip._listFrame.insert( _clip._listFrame.begin() + insertIndex, copy );
        for ( SpriteClipAnimation& animation : _clip._listAnimation )
        {
            if ( insertIndex <= animation._firstFrame )
                ++animation._firstFrame;
            else if ( insertIndex < animation._firstFrame + animation._frameCount )
                ++animation._frameCount;
        }
        _selectedFrame = insertIndex;
        return true;
    }

    bool SpriteClipViewModel::applyMoveSelectedFrame( int32 delta )
    {
        const int32 target = _selectedFrame + delta;
        if ( editSelectedFrame() == nullptr || SpriteClipViewModelInternal::isValidIndex( _clip._listFrame, target ) == false || delta == 0 )
            return false;
        std::swap( _clip._listFrame[static_cast<size_t>( _selectedFrame )], _clip._listFrame[static_cast<size_t>( target )] );
        _selectedFrame = target;
        return true;
    }

    void SpriteClipViewModel::applyAddFramesFromGrid()
    {
        const int32 cellCount = _sliceColumns * _sliceRows;
        for ( int32 cell = 0; cell < cellCount; ++cell )
        {
            SpriteClipFrame frame{};
            frame._uvRect = AtlasGridUtil::computeCellUVRect( cell, _sliceColumns, _sliceRows );
            _clip._listFrame.push_back( frame );
        }
        _selectedFrame = _clip.getFrameCount() - 1;
    }

    bool SpriteClipViewModel::applyCommitSelectedFrameBorder()
    {
        SpriteClipFrame* pFrame = editSelectedFrame();
        if ( pFrame == nullptr )
            return false;
        float4& border = pFrame->_borderPixels;
        border._x      = MathUtil::max( border._x, 0.0f );
        border._y      = MathUtil::max( border._y, 0.0f );
        border._z      = MathUtil::max( border._z, 0.0f );
        border._w      = MathUtil::max( border._w, 0.0f );
        if ( _clip.rebuildFrameBorders() == 0 )
            return true;
        border  = float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        _status = SW_LOCKEY( "EditorSpriteClip", "BorderNeedsAtlasSizeStatus" );
        return false;
    }

    SpriteClipKey* SpriteClipViewModel::editSelectedKey()
    {
        return SpriteClipViewModelInternal::isValidIndex( _clip._listKey, _selectedKey ) ? &_clip._listKey[static_cast<size_t>( _selectedKey )] : nullptr;
    }

    void SpriteClipViewModel::applyAddKey()
    {
        _clip._listKey.push_back( SpriteClipKey{} );
        _selectedKey = static_cast<int32>( _clip._listKey.size() ) - 1;
    }

    bool SpriteClipViewModel::applyRemoveSelectedKey()
    {
        return SpriteClipViewModelInternal::eraseSelected( _clip._listKey, _selectedKey );
    }

    void SpriteClipViewModel::selectAnimation( int32 animationIndex )
    {
        _selectedAnimation = SpriteClipViewModelInternal::isValidIndex( _clip._listAnimation, animationIndex ) ? animationIndex : -1;
        _animationName     = ( _selectedAnimation >= 0 ) ? _clip._listAnimation[static_cast<size_t>( _selectedAnimation )]._name.c_str() : "";
        _previewSeconds    = 0.0f;
    }

    SpriteClipAnimation* SpriteClipViewModel::editSelectedAnimation()
    {
        return SpriteClipViewModelInternal::isValidIndex( _clip._listAnimation, _selectedAnimation ) ? &_clip._listAnimation[static_cast<size_t>( _selectedAnimation )] : nullptr;
    }

    bool SpriteClipViewModel::applyAddAnimation()
    {
        if ( _clip._listFrame.empty() )
        {
            _status = SW_LOCKEY( "EditorSpriteClip", "AnimationNeedsFrameStatus" );
            return false;
        }
        // 개수로 이름을 지으면 앞 구간을 지운 뒤 남은 이름과 겹쳤다 - 겹친 이름은 `play( name )` 이 찾지 못한다.
        size_t                                   suffix = _clip._listAnimation.size();
        StringBuilder<constant::buffer::kMax256> nameBuilder2;
        nameBuilder2.appendFormat( "anim%#", suffix );
        string name{ nameBuilder2.view() };
        while ( _clip.findAnimation( name ) != nullptr )
        {
            ++suffix;
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "anim%#", suffix );
            name = string( nameBuilder.view() );
        }
        SpriteClipAnimation animation{};
        animation._name       = std::move( name );
        animation._frameCount = _clip.getFrameCount();
        _clip._listAnimation.push_back( std::move( animation ) );
        selectAnimation( static_cast<int32>( _clip._listAnimation.size() ) - 1 );
        return true;
    }

    bool SpriteClipViewModel::applyRemoveSelectedAnimation()
    {
        if ( SpriteClipViewModelInternal::eraseSelected( _clip._listAnimation, _selectedAnimation ) == false )
            return false;
        selectAnimation( _selectedAnimation );
        return true;
    }

    bool SpriteClipViewModel::applyCommitAnimationName()
    {
        SpriteClipAnimation* pAnimation = editSelectedAnimation();
        if ( pAnimation == nullptr )
            return false;
        const string_view                name   = _animationName.c_str();
        const SpriteClipAnimation* const pOther = _clip.findAnimation( name );
        if ( name.empty() || ( pOther != nullptr && pOther != pAnimation ) )
        {
            _animationName = pAnimation->_name.c_str();
            _status        = SW_LOCKEY( "EditorSpriteClip", "AnimationNameRejectedStatus" );
            return false;
        }
        pAnimation->_name = _animationName.c_str();
        return true;
    }

    bool SpriteClipViewModel::applyCommitAnimationRange()
    {
        SpriteClipAnimation* pAnimation = editSelectedAnimation();
        if ( pAnimation == nullptr || _clip._listFrame.empty() )
            return false;
        const int32 frameCount  = _clip.getFrameCount();
        pAnimation->_firstFrame = MathUtil::clamp( pAnimation->_firstFrame, 0, frameCount - 1 );
        pAnimation->_frameCount = MathUtil::clamp( pAnimation->_frameCount, 1, frameCount - pAnimation->_firstFrame );
        _previewSeconds         = 0.0f;
        return true;
    }

    bool SpriteClipViewModel::applyCommitAtlasPixelSize()
    {
        const float2 oldSize = _clip._atlasPixelSize;
        const float2 newSize{ MathUtil::max( _atlasPixelSizeInput._x, 0.0f ), MathUtil::max( _atlasPixelSizeInput._y, 0.0f ) };
        _clip._atlasPixelSize = newSize;
        if ( _clip.rebuildFrameBorders() > 0 )
        {
            _clip._atlasPixelSize = oldSize;
            (void)_clip.rebuildFrameBorders();
            _atlasPixelSizeInput = oldSize;
            _status              = SW_LOCKEY( "EditorSpriteClip", "BorderNeedsAtlasSizeStatus" );
            return false;
        }
        _atlasPixelSizeInput = newSize;
        return newSize._x != oldSize._x || newSize._y != oldSize._y;
    }

    void SpriteClipViewModel::clampSliceGrid()
    {
        _sliceColumns = MathUtil::clamp( _sliceColumns, 1, kMaxSliceCount );
        _sliceRows    = MathUtil::clamp( _sliceRows, 1, kMaxSliceCount );
    }

    SpriteClipViewModel::AtlasPickResult SpriteClipViewModel::applyPickAtlasCell( float32 u, float32 v, bool bAddFrame )
    {
        const optional<int32> cell = AtlasGridUtil::findCellAtUV( u, v, _sliceColumns, _sliceRows );
        if ( cell.has_value() == false )
            return AtlasPickResult::None;
        const bool bAdded = bAddFrame || SpriteClipViewModelInternal::isValidIndex( _clip._listFrame, _selectedFrame ) == false;
        if ( bAdded )
            appendFrame();
        _clip._listFrame[static_cast<size_t>( _selectedFrame )]._uvRect = AtlasGridUtil::computeCellUVRect( *cell, _sliceColumns, _sliceRows );
        return bAdded ? AtlasPickResult::AddedFrame : AtlasPickResult::EditedFrame;
    }

    float32 SpriteClipViewModel::snapToAtlasPixel( float32 value, bool bHorizontal ) const
    {
        const float32 pixels = bHorizontal ? _clip._atlasPixelSize._x : _clip._atlasPixelSize._y;
        if ( pixels <= 0.0f )
            return MathUtil::saturate( value );
        return MathUtil::saturate( MathUtil::round( value * pixels ) / pixels );
    }

    bool SpriteClipViewModel::findSelectedFrameBounds( float4& outBounds ) const
    {
        if ( SpriteClipViewModelInternal::isValidIndex( _clip._listFrame, _selectedFrame ) == false )
            return false;
        const float4& uv   = _clip._listFrame[static_cast<size_t>( _selectedFrame )]._uvRect;
        const float32 left = MathUtil::min( uv._x, uv._x + uv._z );
        const float32 top  = MathUtil::min( uv._y, uv._y + uv._w );
        outBounds          = float4{ left, top, left + MathUtil::abs( uv._z ), top + MathUtil::abs( uv._w ) };
        return true;
    }

    SpriteClipViewModel::AtlasPickResult SpriteClipViewModel::applySetFrameRegion( const float2& uvFrom, const float2& uvTo, bool bAddFrame )
    {
        const float32 left   = snapToAtlasPixel( MathUtil::min( uvFrom._x, uvTo._x ), true );
        const float32 right  = snapToAtlasPixel( MathUtil::max( uvFrom._x, uvTo._x ), true );
        const float32 top    = snapToAtlasPixel( MathUtil::min( uvFrom._y, uvTo._y ), false );
        const float32 bottom = snapToAtlasPixel( MathUtil::max( uvFrom._y, uvTo._y ), false );
        if ( right - left <= 0.0f || bottom - top <= 0.0f )
            return AtlasPickResult::None;
        const bool bAdded = bAddFrame || SpriteClipViewModelInternal::isValidIndex( _clip._listFrame, _selectedFrame ) == false;
        if ( bAdded )
            appendFrame();
        _clip._listFrame[static_cast<size_t>( _selectedFrame )]._uvRect = float4{ left, top, right - left, bottom - top };
        return bAdded ? AtlasPickResult::AddedFrame : AtlasPickResult::EditedFrame;
    }

    bool SpriteClipViewModel::moveSelectedFrameEdge( SpriteFrameEdge edge, float32 position )
    {
        float4 bounds{};
        if ( findSelectedFrameBounds( bounds ) == false )
            return false;
        const bool    bHorizontal = edge == SpriteFrameEdge::Left || edge == SpriteFrameEdge::Right;
        const float32 snapped     = snapToAtlasPixel( position, bHorizontal );
        float4        moved       = bounds;
        switch ( edge )
        {
            case SpriteFrameEdge::Left:
            {
                moved._x = snapped;
                break;
            }
            case SpriteFrameEdge::Top:
            {
                moved._y = snapped;
                break;
            }
            case SpriteFrameEdge::Right:
            {
                moved._z = snapped;
                break;
            }
            case SpriteFrameEdge::Bottom:
            {
                moved._w = snapped;
                break;
            }
        }
        // 맞은편을 넘거나 닿으면 폭 · 높이가 0 이하라 읽기가 버리는 프레임이 된다 — 옮기지 않는다.
        const bool bCollapsed = moved._z - moved._x <= 0.0f || moved._w - moved._y <= 0.0f;
        // 같은지는 픽셀에 맞춘 옛 가장자리와 비교한다 — 경계는 uv 에서 다시 셈해(0.5 + -0.3) 픽셀 경계와 몇 ulp 어긋나 그대로 둔 가장자리도 바뀐 것으로 보였다.
        const float32 oldEdge = edge == SpriteFrameEdge::Left ? bounds._x : edge == SpriteFrameEdge::Top ? bounds._y
                                                                      : edge == SpriteFrameEdge::Right   ? bounds._z
                                                                                                         : bounds._w;
        const bool    bSame   = snapToAtlasPixel( oldEdge, bHorizontal ) == snapped;
        if ( bCollapsed || bSame )
            return false;
        float4&       uv        = _clip._listFrame[static_cast<size_t>( _selectedFrame )]._uvRect;
        const bool    bFlippedX = uv._z < 0.0f;
        const bool    bFlippedY = uv._w < 0.0f;
        const float32 width     = moved._z - moved._x;
        const float32 height    = moved._w - moved._y;
        uv                      = float4{ bFlippedX ? moved._z : moved._x, bFlippedY ? moved._w : moved._y, bFlippedX ? -width : width, bFlippedY ? -height : height };
        return true;
    }

    void SpriteClipViewModel::togglePreview()
    {
        _bPreviewPlaying = isPreviewPlaying() ? SW_FALSE : SW_TRUE;
        _previewSeconds  = 0.0f;
    }

    void SpriteClipViewModel::findPreviewRange( int32& outFirstFrame, int32& outFrameCount, bool& outLoop ) const
    {
        outFirstFrame = 0;
        outFrameCount = _clip.getFrameCount();
        outLoop       = true;
        if ( SpriteClipViewModelInternal::isValidIndex( _clip._listAnimation, _selectedAnimation ) == false )
            return;
        const SpriteClipAnimation& animation = _clip._listAnimation[static_cast<size_t>( _selectedAnimation )];
        outFirstFrame                        = MathUtil::clamp( animation._firstFrame, 0, outFrameCount );
        outFrameCount                        = MathUtil::clamp( animation._frameCount, 0, outFrameCount - outFirstFrame );
        outLoop                              = animation._bLoop == SW_TRUE;
    }

    void SpriteClipViewModel::advancePreview( float32 deltaSeconds )
    {
        if ( isPreviewPlaying() == false )
            return;
        int32 firstFrame{ 0 };
        int32 frameCount{ 0 };
        bool  bLoop{ true };
        findPreviewRange( firstFrame, frameCount, bLoop );
        float32 totalSeconds{ 0.0f };
        for ( int32 frameIndex = firstFrame; frameIndex < firstFrame + frameCount; ++frameIndex )
        {
            totalSeconds += getPreviewFrameSeconds( frameIndex );
        }
        if ( totalSeconds <= 0.0f )
        {
            _bPreviewPlaying = SW_FALSE;
            return;
        }
        _previewSeconds += deltaSeconds;
        if ( _previewSeconds < totalSeconds )
            return;
        if ( bLoop )
        {
            _previewSeconds = MathUtil::fmod( _previewSeconds, totalSeconds );
            return;
        }
        _previewSeconds  = totalSeconds;
        _bPreviewPlaying = SW_FALSE;
    }

    int32 SpriteClipViewModel::getPreviewFrame() const
    {
        int32 firstFrame{ 0 };
        int32 frameCount{ 0 };
        bool  bLoop{ true };
        findPreviewRange( firstFrame, frameCount, bLoop );
        if ( frameCount <= 0 )
            return -1;
        float32 remainingSeconds = _previewSeconds;
        for ( int32 frameIndex = firstFrame; frameIndex < firstFrame + frameCount; ++frameIndex )
        {
            remainingSeconds -= getPreviewFrameSeconds( frameIndex );
            if ( remainingSeconds < 0.0f )
                return frameIndex;
        }
        return firstFrame + frameCount - 1; // 반복이 아닌 구간의 끝이다
    }

    SpriteClipAsset SpriteClipViewModel::captureClipData() const
    {
        SpriteClipAsset data = _clip;
        data._atlasPath      = _atlasPath.c_str();
        return data;
    }

    void SpriteClipViewModel::applyClip( SpriteClipAsset&& clip )
    {
        _clip                = std::move( clip );
        _atlasPath           = _clip._atlasPath.c_str();
        _atlasPixelSizeInput = _clip._atlasPixelSize;
        _selectedFrame       = MathUtil::min( _selectedFrame, _clip.getFrameCount() - 1 );
        _selectedKey         = MathUtil::min( _selectedKey, static_cast<int32>( _clip._listKey.size() ) - 1 );
        selectAnimation( MathUtil::min( _selectedAnimation, static_cast<int32>( _clip._listAnimation.size() ) - 1 ) );
    }

    void SpriteClipViewModel::recordDocumentEdit( string_view label ) const
    {
        SW_ASSERT( _pEditRecorder != nullptr ); // 문서 패널이 만들 때 건다(`bindDocumentEditRecorder`)
        _pEditRecorder->notifyEdited( label );
    }

    // 편집 연산 — 바꿨으면 모델이 정한 되돌리기 이름으로 알린다(감사 E-16)
    void SpriteClipViewModel::addFrame()
    {
        appendFrame();
        recordDocumentEdit( "Add Sprite Frame" );
    }

    void SpriteClipViewModel::addKey()
    {
        applyAddKey();
        recordDocumentEdit( "Add Sprite Key" );
    }

    void SpriteClipViewModel::addFramesFromGrid()
    {
        applyAddFramesFromGrid();
        recordDocumentEdit( "Add Sprite Frames" );
    }

    void SpriteClipViewModel::recordPickResult( AtlasPickResult result ) const
    {
        if ( result == AtlasPickResult::AddedFrame )
            recordDocumentEdit( "Add Sprite Frame" );
        else if ( result == AtlasPickResult::EditedFrame )
            recordDocumentEdit( "Edit Sprite Frame" );
    }

    SpriteClipViewModel::AtlasPickResult SpriteClipViewModel::pickAtlasCell( float32 u, float32 v, bool bAddFrame )
    {
        const AtlasPickResult result = applyPickAtlasCell( u, v, bAddFrame );
        recordPickResult( result );
        return result;
    }

    SpriteClipViewModel::AtlasPickResult SpriteClipViewModel::setFrameRegion( const float2& uvFrom, const float2& uvTo, bool bAddFrame )
    {
        const AtlasPickResult result = applySetFrameRegion( uvFrom, uvTo, bAddFrame );
        recordPickResult( result );
        return result;
    }

    // 편집 연산 — 본문이 바꿨으면 모델이 정한 되돌리기 이름으로 알린다(감사 E-16). View 는 연산만 부른다.
    bool SpriteClipViewModel::removeSelectedFrame()
    {
        if ( applyRemoveSelectedFrame() == false )
            return false;
        recordDocumentEdit( "Remove Sprite Frame" );
        return true;
    }

    bool SpriteClipViewModel::duplicateSelectedFrame()
    {
        if ( applyDuplicateSelectedFrame() == false )
            return false;
        recordDocumentEdit( "Duplicate Sprite Frame" );
        return true;
    }

    bool SpriteClipViewModel::moveSelectedFrame( int32 delta )
    {
        if ( applyMoveSelectedFrame( delta ) == false )
            return false;
        recordDocumentEdit( "Move Sprite Frame" );
        return true;
    }

    bool SpriteClipViewModel::commitSelectedFrameBorder()
    {
        if ( applyCommitSelectedFrameBorder() == false )
            return false;
        recordDocumentEdit( "Edit Sprite Frame" );
        return true;
    }

    bool SpriteClipViewModel::removeSelectedKey()
    {
        if ( applyRemoveSelectedKey() == false )
            return false;
        recordDocumentEdit( "Remove Sprite Key" );
        return true;
    }

    bool SpriteClipViewModel::addAnimation()
    {
        if ( applyAddAnimation() == false )
            return false;
        recordDocumentEdit( "Add Sprite Animation" );
        return true;
    }

    bool SpriteClipViewModel::removeSelectedAnimation()
    {
        if ( applyRemoveSelectedAnimation() == false )
            return false;
        recordDocumentEdit( "Remove Sprite Animation" );
        return true;
    }

    bool SpriteClipViewModel::commitAnimationName()
    {
        if ( applyCommitAnimationName() == false )
            return false;
        recordDocumentEdit( "Edit Sprite Animation" );
        return true;
    }

    bool SpriteClipViewModel::commitAnimationRange()
    {
        if ( applyCommitAnimationRange() == false )
            return false;
        recordDocumentEdit( "Edit Sprite Animation" );
        return true;
    }

    bool SpriteClipViewModel::commitAtlasPixelSize()
    {
        if ( applyCommitAtlasPixelSize() == false )
            return false;
        recordDocumentEdit( "Edit Sprite Atlas Size" );
        return true;
    }

    bool SpriteClipViewModel::setSelectedAnimationLoop( bool bLoop )
    {
        SpriteClipAnimation* const pAnimation = editSelectedAnimation();
        if ( pAnimation == nullptr )
            return false;
        pAnimation->_bLoop = bLoop ? SW_TRUE : SW_FALSE;
        recordDocumentEdit( "Edit Sprite Animation" );
        return true;
    }
} // namespace sw::editor
