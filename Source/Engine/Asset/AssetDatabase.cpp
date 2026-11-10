#include "pch.h"

#include "Engine/Asset/AssetDatabase.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONFields.h"

namespace sw
{
    namespace
    {
        struct AssetDatabaseInternal
        {
            static string toLowerID( string_view assetID ) { return StringUtil::toLower( string( assetID ).c_str() ); }

            /** @brief 서술 리소스 id(`a/b.png.meta`)의 원본 리소스 id(`a/b.png`)입니다. */
            static string_view toSourceID( string_view sidecarID ) { return sidecarID.substr( 0, sidecarID.size() - AssetMeta::kExtension.size() ); }

            static bool hasNoEmptyText( const vector<string>& listText, const utf8* pField, string_view path )
            {
                for ( const string& text : listText )
                {
                    if ( text.empty() )
                    {
                        SW_LOG_ERROR_ID( "AssetIdentityRules.EmptyEntry", "%# has an empty entry in %#", path, pField );
                        return false;
                    }
                }
                return true;
            }

            /** @brief @p guid 를 @p assetID 로 올립니다. 이미 있으면 겹침을 결함으로 남기고 올리지 않습니다. */
            static void registerGUID( unordered_map<AssetGUID, string>& inoutMapGUIDToPath, unordered_map<string, AssetGUID>& inoutMapPathToGUID, const AssetGUID& guid,
                                      string_view assetID, vector<AssetIdentityIssue>& outListIssue )
            {
                const auto found = inoutMapGUIDToPath.find( guid );
                if ( found != inoutMapGUIDToPath.end() )
                {
                    outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::DuplicateGUID, string( assetID ), found->second } );
                    return;
                }
                inoutMapGUIDToPath.emplace( guid, string( assetID ) );
                inoutMapPathToGUID.emplace( toLowerID( assetID ), guid );
            }
        };
    } // namespace

    bool AssetIdentityRules::load( string_view projectRoot )
    {
        const string path = FileUtil::joinPath( projectRoot, kConfigFile );
        JSONDocument document;
        if ( document.loadFromFile( path ) == false )
        {
            SW_LOG_ERROR_ID( "AssetIdentityRules.ConfigUnreadable", "Asset identity rules %# could not be read", path );
            return false;
        }
        AssetIdentityRules loaded;
        bool               bValid = JSONFields::readFields( document.getRoot(), loaded, path, { "_comment" } );
        bValid                    = AssetDatabaseInternal::hasNoEmptyText( loaded._listHeaderSuffix, "listHeaderSuffix", path ) && bValid;
        bValid                    = AssetDatabaseInternal::hasNoEmptyText( loaded._listSkipPattern, "listSkipPattern", path ) && bValid;
        bValid                    = AssetDatabaseInternal::hasNoEmptyText( loaded._listDerivedFolder, "listDerivedFolder", path ) && bValid;
        if ( bValid == false )
            return false;
        _listHeaderSuffix  = std::move( loaded._listHeaderSuffix );
        _listSkipPattern   = std::move( loaded._listSkipPattern );
        _listDerivedFolder = std::move( loaded._listDerivedFolder );
        return true;
    }

    AssetIdentityKind AssetIdentityRules::classify( string_view assetID ) const
    {
        for ( const string& pattern : _listSkipPattern )
        {
            if ( StringUtil::matchesWildcard( pattern, assetID ) )
                return AssetIdentityKind::Skipped;
        }
        if ( StringUtil::endsWith( assetID, AssetMeta::kExtension, true ) )
            return AssetIdentityKind::Sidecar;
        for ( const string& suffix : _listHeaderSuffix )
        {
            if ( StringUtil::endsWith( assetID, suffix, true ) )
                return AssetIdentityKind::Header;
        }
        return AssetIdentityKind::Source;
    }

    bool AssetIdentityRules::isUnderDerivedFolder( string_view assetID ) const
    {
        const string wrapped = "/" + AssetDatabaseInternal::toLowerID( assetID );
        for ( const string& folder : _listDerivedFolder )
        {
            if ( wrapped.find( "/" + folder + "/" ) != string::npos )
                return true;
        }
        return false;
    }

    string AssetMeta::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
    }

    optional<AssetGUID> AssetMeta::parseXML( string_view text, string_view sourceLabel )
    {
        // 설정 XML 과 같은 엄격한 읽기다 — 모르는 속성, 빠진 필수 칸, 읽지 못한 값이 모두 오류다.
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigXMLDocument( this, *StaticType(), text, label.c_str() ) == false )
            return nullopt;
        optional<AssetGUID> guid = AssetGUID::tryParse( _guid );
        if ( guid.has_value() == false )
            SW_LOG_ERROR_ID( "AssetMeta.InvalidGUID", "%# has guid '%#' - a GUID is 32 lowercase hex digits", label, _guid );
        return guid;
    }

    string AssetMeta::makePath( string_view sourcePath )
    {
        return string( sourcePath ) + string( kExtension );
    }

    string AssetMeta::makeText( string_view sourcePath, const AssetGUID& guid )
    {
        AssetMeta meta;
        meta._guid   = guid.toString();
        meta._source = FileUtil::getFileNamePart( sourcePath );
        return meta.toXML();
    }

    string AssetIdentityIssue::toString() const
    {
        switch ( _kind )
        {
            case AssetIdentityIssueKind::DuplicateGUID:
                return _assetID + ": has the same GUID as " + _otherAssetID + " - a copy needs a new GUID (py -3 -m Scripts asset-ids --fix)";
            case AssetIdentityIssueKind::MissingHeader:
                return _assetID + ": has no <?asset guid?> header (py -3 -m Scripts asset-ids --fix)";
            case AssetIdentityIssueKind::InvalidHeader:
                return _assetID + ": the <?asset guid?> header is unreadable, misplaced or repeated";
            case AssetIdentityIssueKind::MissingMeta:
                return _assetID + ": has no .meta beside it (py -3 -m Scripts asset-ids --fix)";
            case AssetIdentityIssueKind::OrphanMeta:
                return _assetID + ": its source file is missing - move the .meta with the file or let asset-ids --fix reattach it";
            case AssetIdentityIssueKind::InvalidMeta:
                return _assetID + ": the .meta is unreadable or names another source file";
            case AssetIdentityIssueKind::Unreadable:
                return _assetID + ": could not be read";
        }
        return _assetID;
    }

    AssetDatabase AssetDatabase::build( const vector<AssetIdentityFile>& listFile, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue )
    {
        using Internal = AssetDatabaseInternal;
        // 순서를 고정한다 — 겹친 GUID 의 어느 쪽이 남는지가 목록 순서에 기대지 않게.
        vector<const AssetIdentityFile*> listSorted;
        unordered_set<string>            setSourceID;
        unordered_set<string>            setSidecarID;
        for ( const AssetIdentityFile& file : listFile )
        {
            listSorted.push_back( &file );
            const AssetIdentityKind kind = rules.classify( file._assetID );
            if ( kind == AssetIdentityKind::Source )
                setSourceID.insert( Internal::toLowerID( file._assetID ) );
            else if ( kind == AssetIdentityKind::Sidecar )
                setSidecarID.insert( Internal::toLowerID( file._assetID ) );
        }
        std::sort( listSorted.begin(), listSorted.end(), []( const AssetIdentityFile* pLeft, const AssetIdentityFile* pRight )
        { return pLeft->_assetID < pRight->_assetID; } );

        AssetDatabase database;
        for ( const AssetIdentityFile* pFile : listSorted )
        {
            const string& assetID = pFile->_assetID;
            switch ( rules.classify( assetID ) )
            {
                case AssetIdentityKind::Skipped:
                    break;
                case AssetIdentityKind::Header:
                {
                    const AssetGUIDHeader header = AssetGUIDHeader::read( pFile->_text );
                    if ( header._state == AssetGUIDHeaderState::Found )
                        Internal::registerGUID( database._mapGUIDToPath, database._mapPathToGUID, *header._guid, assetID, outListIssue );
                    else
                        outListIssue.push_back( AssetIdentityIssue{ header._state == AssetGUIDHeaderState::Missing ? AssetIdentityIssueKind::MissingHeader : AssetIdentityIssueKind::InvalidHeader, assetID, {} } );
                    break;
                }
                case AssetIdentityKind::Sidecar:
                {
                    const string_view sourceID = Internal::toSourceID( assetID );
                    if ( setSourceID.contains( Internal::toLowerID( sourceID ) ) == false )
                    {
                        // 원본이 없거나, 곁의 파일이 정체성을 스스로 드는 종류다(머리 글 · 건너뛰는 파일의 .meta 는 짝이 아니다)
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::OrphanMeta, assetID, {} } );
                        break;
                    }
                    AssetMeta                 meta;
                    const optional<AssetGUID> guid = meta.parseXML( pFile->_text, assetID );
                    if ( guid.has_value() == false || meta._source != FileUtil::getFileNamePart( sourceID ) )
                    {
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::InvalidMeta, assetID, {} } );
                        break;
                    }
                    Internal::registerGUID( database._mapGUIDToPath, database._mapPathToGUID, *guid, sourceID, outListIssue );
                    break;
                }
                case AssetIdentityKind::Source:
                    if ( setSidecarID.contains( Internal::toLowerID( AssetMeta::makePath( assetID ) ) ) == false )
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::MissingMeta, assetID, {} } );
                    break;
            }
        }
        return database;
    }

    AssetDatabase AssetDatabase::scan( string_view contentRoot, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue )
    {
        const string   root = FileUtil::normalizeSeparators( contentRoot );
        vector<string> listAbs;
        if ( FileUtil::collectFiles( root, "", listAbs, true ) == false )
        {
            outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::Unreadable, root, {} } );
            return AssetDatabase{};
        }
        vector<string>        listID;
        unordered_set<string> setID;
        for ( const string& abs : listAbs )
        {
            const string normalized = FileUtil::normalizeSeparators( abs );
            if ( normalized.size() <= root.size() + 1 )
                continue;
            listID.push_back( normalized.substr( root.size() + 1 ) );
            setID.insert( AssetDatabaseInternal::toLowerID( listID.back() ) );
        }
        vector<AssetIdentityFile> listFile;
        for ( const string& assetID : listID )
        {
            const AssetIdentityKind kind         = rules.classify( assetID );
            const bool              bDerivedFile = kind == AssetIdentityKind::Source && rules.isUnderDerivedFolder( assetID ) &&
                                      setID.contains( AssetDatabaseInternal::toLowerID( AssetMeta::makePath( assetID ) ) ) == false;
            if ( kind == AssetIdentityKind::Skipped || bDerivedFile )
                continue; // 임포트 산출물(커밋하지 않는 캐시)은 원본의 정체성을 쓴다
            AssetIdentityFile file{ assetID, {} };
            const bool        bCarriesIdentity = kind == AssetIdentityKind::Header || kind == AssetIdentityKind::Sidecar;
            if ( bCarriesIdentity && FileUtil::readTextFile( FileUtil::joinPath( root, assetID ), file._text ) == false )
            {
                outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::Unreadable, assetID, {} } );
                continue;
            }
            listFile.push_back( std::move( file ) );
        }
        return build( listFile, rules, outListIssue );
    }

    optional<string> AssetDatabase::findPath( const AssetGUID& guid ) const
    {
        const auto found = _mapGUIDToPath.find( guid );
        if ( found == _mapGUIDToPath.end() )
            return nullopt;
        return found->second;
    }

    optional<AssetGUID> AssetDatabase::findGUID( string_view assetID ) const
    {
        const auto found = _mapPathToGUID.find( AssetDatabaseInternal::toLowerID( assetID ) );
        if ( found == _mapPathToGUID.end() )
            return nullopt;
        return found->second;
    }
} // namespace sw
