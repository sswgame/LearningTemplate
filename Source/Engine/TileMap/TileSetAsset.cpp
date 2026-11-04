#include "pch.h"

#include "Engine/TileMap/TileSetAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    namespace
    {
        struct TileSetAssetInternal
        {

            static constexpr string_view kFrameListSeparators = " ,\t"; ///< `frames` 정수 목록의 구분 글자

            /** @brief 아홉 글자 패턴(공백은 건너뜀)을 이웃 여덟 셀의 조건으로 읽습니다. 가운데(다섯째) 글자는 보지 않습니다. */
            [[nodiscard]] static bool parsePattern( string_view text, TileNeighborRule ( &outArrNeighbor )[8] )
            {
                TileNeighborRule arrCell[9]{};
                uint32           count = 0;
                for ( const utf8 character : text )
                {
                    const bool bWhitespaceCharacter = character == ' ' || character == '\t' || character == '\n' || character == '\r';
                    if ( bWhitespaceCharacter )
                        continue;
                    if ( count >= 9 )
                        return false;
                    switch ( character )
                    {
                        case '.':
                        {
                            arrCell[count] = TileNeighborRule::Any;
                            break;
                        }
                        case 'o':
                        {
                            arrCell[count] = TileNeighborRule::Same;
                            break;
                        }
                        case 'x':
                        {
                            arrCell[count] = TileNeighborRule::Other;
                            break;
                        }
                        default:
                        {
                            return false;
                        }
                    }
                    ++count;
                }
                if ( count != 9 )
                    return false;
                constexpr uint32 kArrCellOfNeighbor[8] = { 0, 1, 2, 3, 5, 6, 7, 8 };
                for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
                {
                    outArrNeighbor[neighbor] = arrCell[kArrCellOfNeighbor[neighbor]];
                }
                return true;
            }
        };
    } // namespace

    int32 TileVisual::computeCellAt( float32 seconds ) const
    {
        if ( _listFrameCell.empty() )
            return -1;
        if ( isAnimated() == false )
            return _listFrameCell[0];
        const float32 frame = MathUtil::floor( MathUtil::max( seconds, 0.0f ) * _framesPerSecond );
        const uint64  index = static_cast<uint64>( frame ) % static_cast<uint64>( _listFrameCell.size() );
        return _listFrameCell[static_cast<size_t>( index )];
    }

    bool TileSetAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.TileSetNotFound", "Tile set '%#' not found", path );
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool TileSetAsset::parseVisual( const XMLNode& node, string_view sourceName, bool bRequired, TileVisual& outVisual ) const
    {
        outVisual = TileVisual{};
        if ( node.findAttribute( "frames" ) != nullptr )
        {
            if ( StringUtil::parseIntList( node.getAttributeText( "frames" ), TileSetAssetInternal::kFrameListSeparators, outVisual._listFrameCell ) == false || outVisual._listFrameCell.empty() )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> frames must be cell numbers", sourceName, node.getName() );
                return false;
            }
            if ( node.readOptionalAttributeInRange( "fps", 0.0f, MathUtil::kMaxFloat, outVisual._framesPerSecond ) == false )
                return false;
            if ( outVisual._listFrameCell.size() > 1 && outVisual._framesPerSecond <= 0.0f )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> has frames but no positive fps", sourceName, node.getName() );
                return false;
            }
        }
        else if ( node.findAttribute( "cell" ) != nullptr )
        {
            const std::optional<int32> cell = node.readRequiredAttribute<int32>( "cell" );
            if ( cell.has_value() == false )
                return false;
            outVisual._listFrameCell.push_back( *cell );
        }
        else if ( bRequired )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> needs cell=\"n\" or frames=\"a b c\"", sourceName, node.getName() );
            return false;
        }
        const int32 cellCount = _columnCount * _rowCount;
        for ( const int32 cell : outVisual._listFrameCell )
        {
            if ( cell < 0 || cell >= cellCount )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: <%#> cell %# is outside the %#x%# atlas", sourceName, node.getName(), cell, _columnCount, _rowCount );
                return false;
            }
        }
        return true;
    }

    bool TileSetAsset::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        XMLDocument doc;
        if ( doc.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.ParseFailed", "%#", doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( "TileSet" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: the root element must be <TileSet>", sourceName );
            return false;
        }
        // 칸 · 범위는 스키마(`TileSetRootFields`)가 정본이다. 깨진 값 · 범위 밖 값은 읽기 실패다.
        TileSetRootFields rootFields;
        if ( XMLFields::readFields( root, rootFields, sourceName ) == false )
            return false;
        TileSetAsset loaded;
        loaded._atlasPath       = rootFields._atlas;
        loaded._normalAtlasPath = rootFields._normalAtlas;
        loaded._columnCount     = rootFields._columns;
        loaded._rowCount        = rootFields._rows;
        loaded._tileSize        = rootFields._tileSize;

        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            TileBrush brush{};
            if ( loaded.parseBrush( node, sourceName, brush ) == false )
                return false;
            loaded._listBrush.push_back( brush );
        }
        *this = loaded;
        return true;
    }

    bool TileSetAsset::parseBrush( const XMLNode& node, string_view sourceName, TileBrush& outBrush ) const
    {
        using Internal       = TileSetAssetInternal;
        const bool bRuleTile = StringUtil::equals( node.getName(), "RuleTile" );
        if ( bRuleTile == false && StringUtil::equals( node.getName(), "Tile" ) == false )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: unknown element <%#> (Tile, RuleTile)", sourceName, node.getName() );
            return false;
        }
        TileBrushFields fields;
        if ( XMLFields::readFields( node, fields, sourceName, { "cell", "frames", "fps" } ) == false || fields._name.empty() )
            return false;
        TileBrush&        brush = outBrush;
        const string_view name  = fields._name.view();
        brush._name             = fields._name;
        if ( findBrush( brush._name ) >= 0 )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: brush '%#' is listed twice", sourceName, name );
            return false;
        }
        if ( parseVisual( node, sourceName, true, brush._defaultVisual ) == false )
            return false;
        brush._bSolid                         = fields._bSolid ? SW_TRUE : SW_FALSE;
        brush._navCost                        = static_cast<uint8>( fields._navCost );
        const string_view outside             = fields._outside;
        const bool        bInvalidOutsideMode = outside.empty() == false && StringUtil::equals( outside, "same" ) == false && StringUtil::equals( outside, "empty" ) == false;
        if ( bInvalidOutsideMode )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: outside=\"%#\" must be 'same' or 'empty'", sourceName, outside );
            return false;
        }
        brush._bOutsideIsSame = StringUtil::equals( outside, "same" ) ? SW_TRUE : SW_FALSE;

        for ( XMLNode ruleNode = node.findChild(); ruleNode.isValid(); ruleNode = ruleNode.findNextSibling() )
        {
            if ( bRuleTile == false || StringUtil::equals( ruleNode.getName(), "Rule" ) == false )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: unknown element <%#> in <%#> (only <Rule> inside <RuleTile>)", sourceName, ruleNode.getName(), node.getName() );
                return false;
            }
            TileRuleFields ruleFields;
            if ( XMLFields::readFields( ruleNode, ruleFields, sourceName, { "cell", "frames", "fps" } ) == false )
                return false;
            TileRule rule{};
            if ( Internal::parsePattern( ruleFields._pattern, rule._arrNeighbor ) == false )
            {
                SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: rule pattern '%#' of '%#' must be nine of . o x", sourceName, ruleFields._pattern.c_str(), name );
                return false;
            }
            if ( parseVisual( ruleNode, sourceName, true, rule._visual ) == false )
                return false;
            brush._listRule.push_back( rule );
        }
        if ( bRuleTile && brush._listRule.empty() )
        {
            SW_LOG_ERROR_ID( "TileSet.Invalid", "%#: rule tile '%#' has no <Rule>", sourceName, name );
            return false;
        }
        return true;
    }

    float4 TileSetAsset::computeCellUvRect( int32 cell ) const
    {
        const int32   column = ( _columnCount > 0 ) ? cell % _columnCount : 0;
        const int32   row    = ( _columnCount > 0 ) ? cell / _columnCount : 0;
        const float32 width  = 1.0f / static_cast<float32>( MathUtil::max( _columnCount, 1 ) );
        const float32 height = 1.0f / static_cast<float32>( MathUtil::max( _rowCount, 1 ) );
        return float4{ static_cast<float32>( column ) * width, static_cast<float32>( row ) * height, width, height };
    }

    int32 TileSetAsset::findBrush( const hashed_string& name ) const
    {
        for ( size_t brushIndex = 0; brushIndex < _listBrush.size(); ++brushIndex )
        {
            if ( _listBrush[brushIndex]._name == name )
                return static_cast<int32>( brushIndex );
        }
        return -1;
    }

    const TileVisual* TileSetAsset::resolveVisual( const vector<uint16>& listBrushIndex, int32 width, int32 height, int32 x, int32 y ) const
    {
        const bool bOutsideVisualGrid = x < 0 || y < 0 || x >= width || y >= height;
        if ( bOutsideVisualGrid )
            return nullptr;
        const size_t cellCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
        if ( listBrushIndex.size() < cellCount )
            return nullptr;
        const uint16 brushValue = listBrushIndex[static_cast<size_t>( y ) * static_cast<size_t>( width ) + static_cast<size_t>( x )];
        if ( brushValue == 0 || brushValue > _listBrush.size() )
            return nullptr;
        const TileBrush& brush = _listBrush[brushValue - 1u];
        if ( brush.isRuleTile() == false )
            return &brush._defaultVisual;

        // 이웃마다 "같은 브러시인가"를 한 번 측정하고, 규칙을 앞에서부터 맞춘다(첫 규칙이 이긴다. 유니티 RuleTile 과 같다).
        bool arrSame[8]{};
        for ( uint32 neighbor = 0; neighbor < 8; ++neighbor )
        {
            const int32 neighborX        = x + kNeighborOffset[neighbor][0];
            const int32 neighborY        = y + kNeighborOffset[neighbor][1];
            const bool  bNeighborOutside = neighborX < 0 || neighborY < 0 || neighborX >= width || neighborY >= height;
            if ( bNeighborOutside )
            {
                arrSame[neighbor] = brush._bOutsideIsSame != SW_FALSE;
                continue;
            }
            arrSame[neighbor] = listBrushIndex[static_cast<size_t>( neighborY ) * static_cast<size_t>( width ) + static_cast<size_t>( neighborX )] == brushValue;
        }
        for ( const TileRule& rule : brush._listRule )
        {
            bool bMatch = true;
            for ( uint32 neighbor = 0; neighbor < 8 && bMatch; ++neighbor )
            {
                const TileNeighborRule condition = rule._arrNeighbor[neighbor];
                if ( condition == TileNeighborRule::Same && arrSame[neighbor] == false )
                    bMatch = false;
                else if ( condition == TileNeighborRule::Other && arrSame[neighbor] )
                    bMatch = false;
            }
            if ( bMatch )
                return &rule._visual;
        }
        return &brush._defaultVisual;
    }
} // namespace sw
