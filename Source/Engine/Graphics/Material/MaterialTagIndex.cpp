#include "pch.h"

#include "Engine/Graphics/Material/MaterialTagIndex.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct MaterialTagIndexInternal
        {
            static constexpr const utf8* kRootName        = "MaterialIndex";
            static constexpr const utf8* kEntryName       = "Material";
            static constexpr const utf8* kVersionAttr     = "formatVersion";
            static constexpr const utf8* kGUIDAttr        = "guid";
            static constexpr const utf8* kShaderAttr      = "shader";
            static constexpr const utf8* kDefineAttr      = "defines";
            static constexpr string_view kDefineSeparator = " "; // define 글에는 빈칸이 없다(`NAME` · `NAME=VALUE`)

            static void sortByGUID( vector<MaterialTagEntry>& inoutList )
            {
                VectorUtil::sort( inoutList, []( const MaterialTagEntry& left, const MaterialTagEntry& right )
                { return left._guid < right._guid; } );
            }

            /** @brief 정렬한 줄에 겹친 GUID 가 없으면 빈 결과, 있으면 그 까닭입니다. */
            [[nodiscard]] static expected<void> validateUnique( const vector<MaterialTagEntry>& listSorted, string_view sourceName )
            {
                for ( size_t index = 1; index < listSorted.size(); ++index )
                {
                    if ( listSorted[index]._guid == listSorted[index - 1]._guid )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "two material index lines carry the same asset GUID", sourceName } );
                }
                return {};
            }

            /** @brief @p listEntry 의 줄을 @p inoutIndex 에 더합니다. 같은 GUID 의 앞 줄은 뒤 줄이 이긴다(뒤 도메인 · 패치). */
            static void mergeEntries( vector<MaterialTagEntry>&& listEntry, MaterialTagIndex& inoutIndex, unordered_map<AssetGUID, size_t>& inoutMapIndex )
            {
                for ( MaterialTagEntry& entry : listEntry )
                {
                    const auto foundIt = inoutMapIndex.find( entry._guid );
                    if ( foundIt != inoutMapIndex.end() )
                    {
                        inoutIndex._listEntry[foundIt->second] = std::move( entry );
                        continue;
                    }
                    inoutMapIndex.emplace( entry._guid, inoutIndex._listEntry.size() );
                    inoutIndex._listEntry.push_back( std::move( entry ) );
                }
            }

            /** @brief 소스 콘텐츠의 도메인 @p domain 에 든 머티리얼을 모두 읽어 줄을 만듭니다(팩 쿠킹이 쓰는 줄과 같은 `makeEntry`). */
            [[nodiscard]] static expected<vector<MaterialTagEntry>> buildDomainFromSource( const string& domain )
            {
                vector<AssetID>          listMaterial;
                const AssetFolderListing listing = AssetIO::collectAssets( AssetID{ domain }, ".material", listMaterial );
                if ( listing == AssetFolderListing::FolderMissing )
                    return vector<MaterialTagEntry>{};
                if ( listing != AssetFolderListing::Listed )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "the material folder of a domain could not be listed", domain } );
                vector<MaterialTagEntry> listEntry;
                listEntry.reserve( listMaterial.size() );
                for ( const AssetID& materialID : listMaterial )
                {
                    const optional<AssetGUID> guid = AssetRegistry::resolveGUID( materialID.view() );
                    if ( guid.has_value() == false )
                        return unexpected( Error{ ErrorKind::NotFound, "a material has no asset identity (.meta)", materialID.view() } );
                    const shared_ptr<Material> material = Material::create();
                    if ( material->loadFromAsset( materialID ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "a material could not be read for the tag index", materialID.view() } );
                    listEntry.push_back( MaterialTagIndex::makeEntry( *guid, *material ) );
                }
                return listEntry;
            }
        };
    } // namespace

    MaterialTagEntry MaterialTagIndex::makeEntry( const AssetGUID& guid, const Material& material )
    {
        return MaterialTagEntry{ guid, material.getShaderPath(), material.getCachedShaderDefines() };
    }

    expected<string> MaterialTagIndex::makeText() const
    {
        using Internal                      = MaterialTagIndexInternal;
        vector<MaterialTagEntry> listSorted = _listEntry;
        Internal::sortByGUID( listSorted );
        if ( const expected<void> unique = Internal::validateUnique( listSorted, kFileName ); unique.has_value() == false )
            return unexpected( unique.error() );
        XMLDocument   document;
        const XMLNode root = document.appendRoot( Internal::kRootName );
        root.appendAttribute( Internal::kVersionAttr, kFormatVersion );
        for ( const MaterialTagEntry& entry : listSorted )
        {
            string defines;
            for ( const string& define : entry._listDefine )
            {
                if ( defines.empty() == false )
                    defines.append( Internal::kDefineSeparator );
                defines.append( define );
            }
            const XMLNode line = root.appendChild( Internal::kEntryName );
            line.appendAttribute( Internal::kGUIDAttr, string_view{ entry._guid.toString() } );
            line.appendAttribute( Internal::kShaderAttr, string_view{ entry._shaderPath } );
            line.appendAttribute( Internal::kDefineAttr, string_view{ defines } );
        }
        return document.saveToString();
    }

    expected<MaterialTagIndex> MaterialTagIndex::parseText( string_view text, string_view sourceName )
    {
        using Internal = MaterialTagIndexInternal;
        XMLDocument document;
        if ( document.parse( text, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the material index is not XML", sourceName } );
        const XMLNode root = document.getRoot( Internal::kRootName );
        if ( root.isValid() == false || root.getAttributeText( Internal::kVersionAttr ) != sw::to_string( kFormatVersion ) )
            return unexpected( Error{ ErrorKind::Unsupported, "the material index root or formatVersion is not this build's", sourceName } );
        MaterialTagIndex index;
        for ( XMLNode line = root.findChild(); line.isValid(); line = line.findNextSibling() )
        {
            const optional<AssetGUID> guid = AssetGUID::tryParse( line.getAttributeText( Internal::kGUIDAttr ) );
            if ( string_view{ line.getName() } != Internal::kEntryName || guid.has_value() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a material index line is not <Material guid shader defines>", sourceName } );
            MaterialTagEntry entry{ *guid, string{ line.getAttributeText( Internal::kShaderAttr ) }, {} };
            for ( const string_view define : StringUtil::split( line.getAttributeText( Internal::kDefineAttr ), Internal::kDefineSeparator ) )
            {
                if ( define.empty() == false )
                    entry._listDefine.emplace_back( define );
            }
            index._listEntry.push_back( std::move( entry ) );
        }
        vector<MaterialTagEntry> listSorted = index._listEntry;
        Internal::sortByGUID( listSorted );
        if ( const expected<void> unique = Internal::validateUnique( listSorted, sourceName ); unique.has_value() == false )
            return unexpected( unique.error() );
        return index;
    }

    expected<MaterialTagIndex> MaterialTagIndex::loadMounted( const vector<string>& listDomain )
    {
        using Internal = MaterialTagIndexInternal;
        MaterialTagIndex                 index;
        unordered_map<AssetGUID, size_t> mapIndex;
        const bool                       bCooked = AssetIO::getContentSource() == ContentSource::Cooked;
        for ( const string& domain : listDomain )
        {
            if ( bCooked == false )
            {
                expected<vector<MaterialTagEntry>> listEntry = Internal::buildDomainFromSource( domain );
                if ( listEntry.has_value() == false )
                    return unexpected( listEntry.error() );
                Internal::mergeEntries( std::move( *listEntry ), index, mapIndex );
                continue;
            }
            const string indexID = FileUtil::joinPath( domain, kFileName );
            string       text;
            if ( AssetIO::readTextAsset( AssetID{ indexID }, text ) == false )
                return unexpected( Error{ ErrorKind::NotFound, "a cooked domain has no material index - recook it", indexID } );
            expected<MaterialTagIndex> domainIndex = parseText( text, indexID );
            if ( domainIndex.has_value() == false )
                return unexpected( domainIndex.error() );
            Internal::mergeEntries( std::move( domainIndex->_listEntry ), index, mapIndex );
        }
        return index;
    }

    vector<AssetGUID> MaterialTagIndex::findByKeyword( string_view keyword ) const
    {
        vector<AssetGUID> listFound;
        for ( const MaterialTagEntry& entry : _listEntry )
        {
            if ( MaterialPermutation::hasDefineName( entry._listDefine, keyword ) )
                listFound.push_back( entry._guid );
        }
        return listFound;
    }

    vector<AssetGUID> MaterialTagIndex::findByShaderPath( string_view shaderPath ) const
    {
        vector<AssetGUID> listFound;
        for ( const MaterialTagEntry& entry : _listEntry )
        {
            if ( StringUtil::equals( entry._shaderPath, shaderPath, true ) )
                listFound.push_back( entry._guid );
        }
        return listFound;
    }

    vector<AssetGUID> MaterialTagIndex::findDrawnByPass( const MaterialPassFilter& filter ) const
    {
        vector<AssetGUID> listFound;
        for ( const MaterialTagEntry& entry : _listEntry )
        {
            if ( filter.shouldDraw( &entry._listDefine ) )
                listFound.push_back( entry._guid );
        }
        return listFound;
    }
} // namespace sw
