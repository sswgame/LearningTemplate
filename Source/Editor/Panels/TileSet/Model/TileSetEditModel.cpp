#include "pch.h"

#include "Editor/Panels/TileSet/Model/TileSetEditModel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

namespace sw::editor
{
    namespace
    {
        struct TileSetEditModelInternal
        {
            /** @brief 프레임을 덧붙여 애니메이션이 될 때 fps 가 0 이면 둘 값입니다(읽기는 fps 없는 애니메이션을 거절한다). */
            static constexpr float32 kDefaultFramesPerSecond = 4.0f;
            /** @brief 이동 비용의 범위입니다(`TileBrushParams::_navCost`, 255 는 막힘). */
            static constexpr int32 kMinNavCost = 1;
            static constexpr int32 kMaxNavCost = 255;

            static TileVisual makeCellVisual( int32 cell )
            {
                TileVisual visual{};
                visual._listFrameCell.push_back( cell );
                return visual;
            }
        };
    } // namespace

    TileSetEditModel::TileSetEditModel()
        : _content{}
        , _status{}
        , _selectedBrush{ -1 }
        , _selectedRule{}
    {
    }

    ToolAssetLoadResult TileSetEditModel::loadDocument( string_view path )
    {
        TileSetContent            loaded;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadTileSet( path, loaded, _status );
        if ( result == ToolAssetLoadResult::Malformed )
            return result; // 저장은 기반이 막는다
        _content       = result == ToolAssetLoadResult::Loaded ? std::move( loaded ) : TileSetContent{};
        _selectedBrush = _content._listBrush.empty() ? -1 : 0;
        _selectedRule  = nullopt;
        return result;
    }

    bool TileSetEditModel::saveDocument( string_view path )
    {
        vector<TileSetIssue> listIssue;
        collectIssues( listIssue );
        if ( listIssue.empty() == false )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Not saved: %#", listIssue.front()._message );
            _status = string( statusBuilder.view() );
            return false;
        }
        if ( EditorToolAssetCommands::saveTileSet( path, _content ) == false )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Failed to save %# (see the log)", path );
            _status = string( statusBuilder.view() );
            return false;
        }
        StringBuilder<constant::buffer::kMax256> statusBuilder;
        statusBuilder.appendFormat( "Saved %#", path );
        _status = string( statusBuilder.view() );
        return true;
    }

    void TileSetEditModel::applyText( string_view text )
    {
        TileSetContent restored;
        if ( text.empty() == false && TileSetAsset::parseContent( text, "(tile set undo)", restored ) == false )
            SW_LOG_WARNING_ID( "TileSetEditModel.UndoSnapshot", "Tile set undo snapshot could not be read - showing an empty tile set" );
        _content = std::move( restored );
        clampSelection();
    }

    void TileSetEditModel::collectIssues( vector<TileSetIssue>& outListIssue ) const
    {
        outListIssue.clear();
        _content.collectIssues( outListIssue );
    }

    bool TileSetEditModel::applySetAtlasPath( string_view path )
    {
        if ( string_view( _content._atlasPath ) == path )
            return false;
        _content._atlasPath = string( path );
        return true;
    }

    bool TileSetEditModel::applySetAtlasGrid( int32 columnCount, int32 rowCount )
    {
        const int32 columns = MathUtil::max( columnCount, 1 );
        const int32 rows    = MathUtil::max( rowCount, 1 );
        if ( columns == _content._columnCount && rows == _content._rowCount )
            return false;
        _content._columnCount = columns;
        _content._rowCount    = rows;
        return true;
    }

    bool TileSetEditModel::applySetTileSize( float32 tileSize )
    {
        if ( tileSize <= 0.0f || tileSize == _content._tileSize )
            return false;
        _content._tileSize = tileSize;
        return true;
    }

    void TileSetEditModel::selectBrush( int32 brushIndex )
    {
        _selectedBrush = brushIndex;
        _selectedRule  = nullopt;
        clampSelection();
    }

    const TileBrush* TileSetEditModel::findSelectedBrush() const
    {
        const bool bValid = 0 <= _selectedBrush && _selectedBrush < static_cast<int32>( _content._listBrush.size() );
        return bValid ? &_content._listBrush[static_cast<size_t>( _selectedBrush )] : nullptr;
    }

    TileBrush* TileSetEditModel::editSelectedBrush()
    {
        return const_cast<TileBrush*>( findSelectedBrush() );
    }

    void TileSetEditModel::addBrush( bool bRuleTile )
    {
        const utf8* const pStem = bRuleTile ? "rule" : "tile";
        string            name  = pStem;
        for ( uint32 suffix = 2; _content.findBrush( hashed_string( string_view( name ) ) ).has_value(); ++suffix )
        {
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "%#%#", pStem, suffix );
            name = string( nameBuilder.view() );
        }
        TileBrush brush{};
        brush._name          = hashed_string( string_view( name ) );
        brush._defaultVisual = TileSetEditModelInternal::makeCellVisual( 0 );
        brush._navCost       = 1; // 읽기의 기본값 — 적지 않은 칸과 같은 값이다
        if ( bRuleTile )
        {
            TileRule rule{};
            rule._visual = brush._defaultVisual;
            brush._listRule.push_back( rule );
        }
        _content._listBrush.push_back( brush );
        _selectedBrush = static_cast<int32>( _content._listBrush.size() ) - 1;
        _selectedRule  = nullopt;
        recordDocumentEdit( bRuleTile ? "Add Rule Tile Brush" : "Add Tile Brush" );
    }

    bool TileSetEditModel::applyRemoveSelectedBrush()
    {
        if ( findSelectedBrush() == nullptr )
            return false;
        _content._listBrush.erase( _content._listBrush.begin() + _selectedBrush );
        clampSelection();
        return true;
    }

    bool TileSetEditModel::applyRenameSelectedBrush( string_view name )
    {
        TileBrush* pBrush = editSelectedBrush();
        if ( pBrush == nullptr )
            return false;
        const hashed_string newName( name );
        if ( name.empty() || ( newName != pBrush->_name && _content.findBrush( newName ).has_value() ) )
        {
            StringBuilder<constant::buffer::kMax256> statusBuilder;
            statusBuilder.appendFormat( "Brush name '%#' is empty or already used - the name stays '%#'", name, pBrush->_name.view() );
            _status = string( statusBuilder.view() );
            return false;
        }
        if ( newName == pBrush->_name )
            return false;
        pBrush->_name = newName;
        return true;
    }

    bool TileSetEditModel::applySetSelectedSolid( bool bSolid )
    {
        TileBrush*  pBrush = editSelectedBrush();
        const uint8 value  = bSolid ? SW_TRUE : SW_FALSE;
        if ( pBrush == nullptr || pBrush->_bSolid == value )
            return false;
        pBrush->_bSolid = value;
        return true;
    }

    bool TileSetEditModel::applySetSelectedNavCost( int32 navCost )
    {
        using Internal     = TileSetEditModelInternal;
        TileBrush*  pBrush = editSelectedBrush();
        const uint8 value  = static_cast<uint8>( MathUtil::clamp( navCost, Internal::kMinNavCost, Internal::kMaxNavCost ) );
        if ( pBrush == nullptr || pBrush->_navCost == value )
            return false;
        pBrush->_navCost = value;
        return true;
    }

    bool TileSetEditModel::applySetSelectedOutsideSame( bool bOutsideIsSame )
    {
        TileBrush*  pBrush = editSelectedBrush();
        const uint8 value  = bOutsideIsSame ? SW_TRUE : SW_FALSE;
        if ( pBrush == nullptr || pBrush->_bOutsideIsSame == value )
            return false;
        pBrush->_bOutsideIsSame = value;
        return true;
    }

    void TileSetEditModel::selectRule( optional<int32> ruleIndex )
    {
        _selectedRule = ruleIndex;
        clampSelection();
    }

    bool TileSetEditModel::applyAddRule()
    {
        TileBrush* pBrush = editSelectedBrush();
        if ( pBrush == nullptr )
            return false;
        TileRule rule{};
        rule._visual = pBrush->_defaultVisual;
        pBrush->_listRule.push_back( rule );
        _selectedRule = static_cast<int32>( pBrush->_listRule.size() ) - 1;
        return true;
    }

    bool TileSetEditModel::applyRemoveSelectedRule()
    {
        TileBrush* pBrush = editSelectedBrush();
        if ( pBrush == nullptr || _selectedRule.has_value() == false || *_selectedRule < 0 || *_selectedRule >= static_cast<int32>( pBrush->_listRule.size() ) )
            return false;
        if ( pBrush->_listRule.size() <= 1 )
        {
            _status = "A rule tile keeps at least one rule";
            return false;
        }
        pBrush->_listRule.erase( pBrush->_listRule.begin() + *_selectedRule );
        clampSelection();
        return true;
    }

    bool TileSetEditModel::applyMoveSelectedRule( int32 delta )
    {
        TileBrush* pBrush = editSelectedBrush();
        if ( pBrush == nullptr || _selectedRule.has_value() == false || *_selectedRule < 0 )
            return false;
        const int32 target = *_selectedRule + delta;
        if ( target < 0 || target >= static_cast<int32>( pBrush->_listRule.size() ) )
            return false;
        std::swap( pBrush->_listRule[static_cast<size_t>( *_selectedRule )], pBrush->_listRule[static_cast<size_t>( target )] );
        _selectedRule = target;
        return true;
    }

    bool TileSetEditModel::applyCycleNeighbor( int32 neighbor )
    {
        TileBrush* pBrush       = editSelectedBrush();
        const bool bRuleInRange = _selectedRule.has_value() && *_selectedRule >= 0 && *_selectedRule < static_cast<int32>( pBrush != nullptr ? pBrush->_listRule.size() : 0 );
        if ( pBrush == nullptr || bRuleInRange == false || neighbor < 0 || neighbor >= 8 )
            return false;
        TileNeighborRule& condition = pBrush->_listRule[static_cast<size_t>( *_selectedRule )]._arrNeighbor[neighbor];
        switch ( condition )
        {
            case TileNeighborRule::Any:
            {
                condition = TileNeighborRule::Same;
                break;
            }
            case TileNeighborRule::Same:
            {
                condition = TileNeighborRule::Other;
                break;
            }
            case TileNeighborRule::Other:
            {
                condition = TileNeighborRule::Any;
                break;
            }
        }
        return true;
    }

    const TileVisual* TileSetEditModel::findSelectedVisual() const
    {
        const TileBrush* pBrush = findSelectedBrush();
        if ( pBrush == nullptr )
            return nullptr;
        if ( _selectedRule.has_value() == false || *_selectedRule < 0 )
            return &pBrush->_defaultVisual;
        return *_selectedRule < static_cast<int32>( pBrush->_listRule.size() ) ? &pBrush->_listRule[static_cast<size_t>( *_selectedRule )]._visual : nullptr;
    }

    TileVisual* TileSetEditModel::editSelectedVisual()
    {
        return const_cast<TileVisual*>( findSelectedVisual() );
    }

    bool TileSetEditModel::applyPickCell( int32 cell, bool bAppendFrame )
    {
        TileVisual* pVisual = editSelectedVisual();
        if ( pVisual == nullptr || cell < 0 || cell >= _content.getCellCount() )
            return false;
        if ( bAppendFrame )
        {
            pVisual->_listFrameCell.push_back( cell );
            if ( pVisual->_framesPerSecond <= 0.0f )
                pVisual->_framesPerSecond = TileSetEditModelInternal::kDefaultFramesPerSecond;
            return true;
        }
        const bool bSame = pVisual->_listFrameCell.size() == 1 && pVisual->_listFrameCell[0] == cell;
        if ( bSame )
            return false;
        pVisual->_listFrameCell.assign( 1, cell );
        pVisual->_framesPerSecond = 0.0f;
        return true;
    }

    bool TileSetEditModel::applyRemoveLastFrame()
    {
        TileVisual* pVisual = editSelectedVisual();
        if ( pVisual == nullptr || pVisual->_listFrameCell.size() <= 1 )
            return false;
        pVisual->_listFrameCell.pop_back();
        return true;
    }

    bool TileSetEditModel::applySetFramesPerSecond( float32 framesPerSecond )
    {
        TileVisual*   pVisual = editSelectedVisual();
        const float32 value   = MathUtil::max( framesPerSecond, 0.0f );
        if ( pVisual == nullptr || pVisual->_framesPerSecond == value )
            return false;
        pVisual->_framesPerSecond = value;
        return true;
    }

    void TileSetEditModel::clampSelection()
    {
        const int32 brushCount = static_cast<int32>( _content._listBrush.size() );
        if ( _selectedBrush >= brushCount )
            _selectedBrush = brushCount - 1;
        if ( _selectedBrush < 0 && brushCount > 0 )
            _selectedBrush = 0;
        const TileBrush* pBrush    = findSelectedBrush();
        const int32      ruleCount = pBrush != nullptr ? static_cast<int32>( pBrush->_listRule.size() ) : 0;
        if ( _selectedRule.has_value() && *_selectedRule >= ruleCount )
            _selectedRule = ruleCount - 1;
        if ( _selectedRule.has_value() && *_selectedRule < 0 )
            _selectedRule = nullopt;
    }

    void TileSetEditModel::recordDocumentEdit( string_view label ) const
    {
        SW_ASSERT( _pEditRecorder != nullptr ); // 문서 패널이 만들 때 건다(`bindDocumentEditRecorder`)
        _pEditRecorder->notifyEdited( label );
    }

    // 편집 연산 — 본문이 바꿨으면 모델이 정한 되돌리기 이름으로 알린다(감사 E-16). View 는 연산만 부른다.
    bool TileSetEditModel::setAtlasPath( string_view path )
    {
        if ( applySetAtlasPath( path ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Set Atlas" );
        return true;
    }

    bool TileSetEditModel::setAtlasGrid( int32 columnCount, int32 rowCount )
    {
        if ( applySetAtlasGrid( columnCount, rowCount ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Set Grid" );
        return true;
    }

    bool TileSetEditModel::setTileSize( float32 tileSize )
    {
        if ( applySetTileSize( tileSize ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Set Tile Size" );
        return true;
    }

    bool TileSetEditModel::removeSelectedBrush()
    {
        if ( applyRemoveSelectedBrush() == false )
            return false;
        recordDocumentEdit( "Remove Tile Brush" );
        return true;
    }

    bool TileSetEditModel::renameSelectedBrush( string_view name )
    {
        if ( applyRenameSelectedBrush( name ) == false )
            return false;
        recordDocumentEdit( "Rename Tile Brush" );
        return true;
    }

    bool TileSetEditModel::setSelectedSolid( bool bSolid )
    {
        if ( applySetSelectedSolid( bSolid ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Brush" );
        return true;
    }

    bool TileSetEditModel::setSelectedNavCost( int32 navCost )
    {
        if ( applySetSelectedNavCost( navCost ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Brush" );
        return true;
    }

    bool TileSetEditModel::setSelectedOutsideSame( bool bOutsideIsSame )
    {
        if ( applySetSelectedOutsideSame( bOutsideIsSame ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Brush" );
        return true;
    }

    bool TileSetEditModel::addRule()
    {
        if ( applyAddRule() == false )
            return false;
        recordDocumentEdit( "Add Tile Rule" );
        return true;
    }

    bool TileSetEditModel::removeSelectedRule()
    {
        if ( applyRemoveSelectedRule() == false )
            return false;
        recordDocumentEdit( "Remove Tile Rule" );
        return true;
    }

    bool TileSetEditModel::moveSelectedRule( int32 delta )
    {
        if ( applyMoveSelectedRule( delta ) == false )
            return false;
        recordDocumentEdit( "Reorder Tile Rules" );
        return true;
    }

    bool TileSetEditModel::cycleNeighbor( int32 neighbor )
    {
        if ( applyCycleNeighbor( neighbor ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Rule Pattern" );
        return true;
    }

    bool TileSetEditModel::removeLastFrame()
    {
        if ( applyRemoveLastFrame() == false )
            return false;
        recordDocumentEdit( "Remove Tile Frame" );
        return true;
    }

    bool TileSetEditModel::setFramesPerSecond( float32 framesPerSecond )
    {
        if ( applySetFramesPerSecond( framesPerSecond ) == false )
            return false;
        recordDocumentEdit( "Edit Tile Frames" );
        return true;
    }

    bool TileSetEditModel::pickCell( int32 cell, bool bAppendFrame )
    {
        if ( applyPickCell( cell, bAppendFrame ) == false )
            return false;
        recordDocumentEdit( bAppendFrame ? "Add Tile Frame" : "Pick Tile Cell" );
        return true;
    }
} // namespace sw::editor
