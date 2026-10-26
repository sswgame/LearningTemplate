#include "pch.h"

#include "Engine/TileMap/TileSetAsset.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

namespace sw
{
    SW_LOG_CALLER( "TileSet" );

    namespace
    {
        struct TileSetAssetInternal
        {
            static constexpr int32 kBlockedNavCost = 255; ///< `NavGrid` 의 막힘 값 — 이동 비용의 상한

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

    bool TileSetAsset::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Tile set '%#' not found", path );
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
                SW_LOG_ERROR( "%#: <%#> frames must be cell numbers", sourceName, node.getName() );
                return false;
            }
            if ( node.readOptionalAttributeInRange( "fps", 0.0f, MathUtil::kMaxFloat, outVisual._framesPerSecond ) == false )
                return false;
            if ( outVisual._listFrameCell.size() > 1 && outVisual._framesPerSecond <= 0.0f )
            {
                SW_LOG_ERROR( "%#: <%#> has frames but no positive fps", sourceName, node.getName() );
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
            SW_LOG_ERROR( "%#: <%#> needs cell=\"n\" or frames=\"a b c\"", sourceName, node.getName() );
            return false;
        }
        const int32 cellCount = _columnCount * _rowCount;
        for ( const int32 cell : outVisual._listFrameCell )
        {
            if ( cell < 0 || cell >= cellCount )
            {
                SW_LOG_ERROR( "%#: <%#> cell %# is outside the %#x%# atlas", sourceName, node.getName(), cell, _columnCount, _rowCount );
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
            SW_LOG_ERROR( "%#", doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( "TileSet" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "%#: the root element must be <TileSet>", sourceName );
            return false;
        }
        static constexpr const utf8* kArrRootAttribute[] = { "name", "atlas", "normalAtlas", "columns", "rows", "tileSize" };
        if ( XMLNameCheck::reportUnknownAttributes( root, kArrRootAttribute, sourceName ) == false )
            return false;

        TileSetAsset loaded;
        loaded._atlasPath       = string( root.getAttributeText( "atlas" ) );
        loaded._normalAtlasPath = string( root.getAttributeText( "normalAtlas" ) );
        // 적지 않은 칸은 TileSetAsset 의 초기값이다. 깨진 값은 읽기 실패다.
        bool bFieldsValid = root.readOptionalAttributeInRange( "columns", MathUtil::kMinInt32, MathUtil::kMaxInt32, loaded._columnCount );
        bFieldsValid      = root.readOptionalAttributeInRange( "rows", MathUtil::kMinInt32, MathUtil::kMaxInt32, loaded._rowCount ) && bFieldsValid;
        bFieldsValid      = root.readOptionalAttributeInRange( "tileSize", -MathUtil::kMaxFloat, MathUtil::kMaxFloat, loaded._tileSize ) && bFieldsValid;
        if ( bFieldsValid == false )
            return false;
        const bool bInvalidTileSetSize = loaded._columnCount <= 0 || loaded._rowCount <= 0 || loaded._tileSize <= 0.0f;
        if ( bInvalidTileSetSize )
        {
            SW_LOG_ERROR( "%#: columns, rows and tileSize must be positive", sourceName );
            return false;
        }

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
            SW_LOG_ERROR( "%#: unknown element <%#> (Tile, RuleTile)", sourceName, node.getName() );
            return false;
        }
        static constexpr const utf8* kArrBrushAttribute[] = { "name", "cell", "frames", "fps", "solid", "navCost", "outside" };
        if ( XMLNameCheck::reportUnknownAttributes( node, kArrBrushAttribute, sourceName ) == false )
            return false;

        TileBrush&        brush = outBrush;
        const string_view name  = node.getAttributeText( "name" );
        if ( name.empty() )
        {
            SW_LOG_ERROR( "%#: a <%#> has no name", sourceName, node.getName() );
            return false;
        }
        brush._name = hashed_string( name );
        if ( findBrush( brush._name ) >= 0 )
        {
            SW_LOG_ERROR( "%#: brush '%#' is listed twice", sourceName, name );
            return false;
        }
        if ( parseVisual( node, sourceName, true, brush._defaultVisual ) == false )
            return false;
        // 적지 않은 칸은 TileBrush 의 초기값이다. 범위 밖 이동 비용은 자르지 않고 오류다.
        std::optional<bool> bSolid;
        int32               navCost = brush._navCost;
        const bool          bBrushFieldsValid =
            node.readOptionalAttribute( "solid", bSolid ) && node.readOptionalAttributeInRange( "navCost", 1, Internal::kBlockedNavCost, navCost );
        if ( bBrushFieldsValid == false )
            return false;
        if ( bSolid.has_value() )
            brush._bSolid = *bSolid ? SW_TRUE : SW_FALSE;
        brush._navCost                        = static_cast<uint8>( navCost );
        const string_view outside             = node.getAttributeText( "outside" );
        const bool        bInvalidOutsideMode = outside.empty() == false && StringUtil::equals( outside, "same" ) == false && StringUtil::equals( outside, "empty" ) == false;
        if ( bInvalidOutsideMode )
        {
            SW_LOG_ERROR( "%#: outside=\"%#\" must be 'same' or 'empty'", sourceName, outside );
            return false;
        }
        brush._bOutsideIsSame = StringUtil::equals( outside, "same" ) ? SW_TRUE : SW_FALSE;

        for ( XMLNode ruleNode = node.findChild(); ruleNode.isValid(); ruleNode = ruleNode.findNextSibling() )
        {
            if ( bRuleTile == false || StringUtil::equals( ruleNode.getName(), "Rule" ) == false )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> in <%#> (only <Rule> inside <RuleTile>)", sourceName, ruleNode.getName(), node.getName() );
                return false;
            }
            static constexpr const utf8* kArrRuleAttribute[] = { "pattern", "cell", "frames", "fps" };
            if ( XMLNameCheck::reportUnknownAttributes( ruleNode, kArrRuleAttribute, sourceName ) == false )
                return false;
            TileRule rule{};
            if ( Internal::parsePattern( ruleNode.getAttributeText( "pattern" ), rule._arrNeighbor ) == false )
            {
                SW_LOG_ERROR( "%#: rule pattern '%#' of '%#' must be nine of . o x", sourceName, ruleNode.getAttributeText( "pattern" ), name );
                return false;
            }
            if ( parseVisual( ruleNode, sourceName, true, rule._visual ) == false )
                return false;
            brush._listRule.push_back( rule );
        }
        if ( bRuleTile && brush._listRule.empty() )
        {
            SW_LOG_ERROR( "%#: rule tile '%#' has no <Rule>", sourceName, name );
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
