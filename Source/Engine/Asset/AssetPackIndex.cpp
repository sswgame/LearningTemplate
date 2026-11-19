#include "pch.h"

#include "Engine/Asset/AssetPackIndex.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AssetPackIndexInternal
        {
            static constexpr const utf8* kRootName    = "AssetIndex";
            static constexpr const utf8* kEntryName   = "Asset";
            static constexpr const utf8* kVersionAttr = "formatVersion";
            static constexpr const utf8* kGUIDAttr    = "guid";
            static constexpr const utf8* kPathAttr    = "path";

            /** @brief 줄들에 겹친 GUID · 경로(대소문자 무시)가 없으면 빈 결과, 있으면 그 까닭입니다. 정렬된 목록을 받는다. */
            [[nodiscard]] static expected<void> validateUnique( const vector<AssetPackIndexEntry>& listSorted )
            {
                unordered_set<string> setPath;
                for ( size_t index = 0; index < listSorted.size(); ++index )
                {
                    if ( index > 0 && listSorted[index]._guid == listSorted[index - 1]._guid )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "two files of one pack carry the same asset GUID", listSorted[index]._path } );
                    if ( listSorted[index]._path.empty() || setPath.insert( StringUtil::toLower( listSorted[index]._path.c_str() ) ).second == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "an asset index line has an empty or repeated path", listSorted[index]._path } );
                }
                return {};
            }

            static void sortByGUID( vector<AssetPackIndexEntry>& inoutList )
            {
                VectorUtil::sort( inoutList, []( const AssetPackIndexEntry& left, const AssetPackIndexEntry& right )
                { return left._guid < right._guid; } );
            }
        };
    } // namespace

    expected<string> AssetPackIndex::makeText() const
    {
        using Internal                         = AssetPackIndexInternal;
        vector<AssetPackIndexEntry> listSorted = _listEntry;
        Internal::sortByGUID( listSorted );
        if ( const expected<void> unique = Internal::validateUnique( listSorted ); unique.has_value() == false )
            return unexpected( unique.error() );
        XMLDocument   document;
        const XMLNode root = document.appendRoot( Internal::kRootName );
        root.appendAttribute( Internal::kVersionAttr, kFormatVersion );
        for ( const AssetPackIndexEntry& entry : listSorted )
        {
            const XMLNode line = root.appendChild( Internal::kEntryName );
            line.appendAttribute( Internal::kGUIDAttr, string_view{ entry._guid.toString() } );
            line.appendAttribute( Internal::kPathAttr, string_view{ entry._path } );
        }
        return document.saveToString();
    }

    expected<AssetPackIndex> AssetPackIndex::parseText( string_view text, string_view sourceName )
    {
        using Internal = AssetPackIndexInternal;
        XMLDocument document;
        if ( document.parse( text, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset index is not XML", sourceName } );
        const XMLNode root = document.getRoot( Internal::kRootName );
        if ( root.isValid() == false || root.getAttributeText( Internal::kVersionAttr ) != sw::to_string( kFormatVersion ) )
            return unexpected( Error{ ErrorKind::Unsupported, "the asset index root or formatVersion is not this build's", sourceName } );
        AssetPackIndex index;
        for ( XMLNode line = root.findChild(); line.isValid(); line = line.findNextSibling() )
        {
            const optional<AssetGUID> guid = AssetGUID::tryParse( line.getAttributeText( Internal::kGUIDAttr ) );
            if ( string_view{ line.getName() } != Internal::kEntryName || guid.has_value() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "an asset index line is not <Asset guid path>", sourceName } );
            index._listEntry.push_back( AssetPackIndexEntry{ *guid, string{ line.getAttributeText( Internal::kPathAttr ) } } );
        }
        vector<AssetPackIndexEntry> listSorted = index._listEntry;
        Internal::sortByGUID( listSorted );
        if ( const expected<void> unique = Internal::validateUnique( listSorted ); unique.has_value() == false )
            return unexpected( unique.error() );
        return index;
    }
} // namespace sw
