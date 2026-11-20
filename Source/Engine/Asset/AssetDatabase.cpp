#include "pch.h"

#include "Engine/Asset/AssetDatabase.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONFields.h"

namespace sw
{
    namespace
    {
        struct AssetDatabaseInternal
        {
            static string toLowerID( string_view assetID ) { return StringUtil::toLower( assetID ); }

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
        bool               bValid = JSONFields::readProperties( document.getRoot(), loaded, path, { "_comment" } );
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
        StringBuilder<constant::buffer::kMax512> wrapped;
        wrapped.appendFormat( "/%#", AssetDatabaseInternal::toLowerID( assetID ) );
        StringBuilder<constant::buffer::kMax256> marker;
        for ( const string& folder : _listDerivedFolder )
        {
            marker.clear();
            marker.appendFormat( "/%#/", folder );
            if ( wrapped.view().find( marker.view() ) != string_view::npos )
                return true;
        }
        return false;
    }

    string AssetMeta::toXML() const
    {
        // 저장소의 서술(.meta)과 `Scripts/lint/asset_identity.py` 의 make_meta_text 는 끝 줄바꿈이 없다 — 같은 바이트여야 저장 → 다시 읽기 → 저장이 같고
        // (editor.importsettings 되돌리기), 린트의 글 비교가 에디터가 쓴 서술을 바뀐 것으로 보지 않는다.
        string text = XMLSerializer::serialize( this, *StaticType() );
        while ( text.empty() == false && ( text.back() == '\n' || text.back() == '\r' ) )
            text.pop_back();
        return text;
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
        StringBuilder<constant::buffer::kMax512> builder;
        builder.appendFormat( "%#%#", sourcePath, kExtension );
        return string( builder.view() );
    }

    string AssetMeta::makeText( string_view sourcePath, const AssetGUID& guid, const vector<AssetImportOverride>& listOverride )
    {
        AssetMeta meta;
        meta._guid               = guid.toString();
        meta._source             = FileUtil::getFileNamePart( sourcePath );
        meta._listImportOverride = listOverride;
        return meta.toXML();
    }

    bool AssetIdentityRules::assignNewIdentity( string_view absPath, string_view assetID ) const
    {
        const AssetGUID guid = AssetGUID::create();
        switch ( classify( assetID ) )
        {
            case AssetIdentityKind::Skipped:
            case AssetIdentityKind::Sidecar:
                return true;
            case AssetIdentityKind::Header:
            {
                string text;
                if ( FileUtil::readTextFile( absPath, text ) && FileUtil::writeTextFile( absPath, AssetGUIDHeader::write( text, guid ) ) )
                {
                    AssetRegistry::record( guid, assetID ); // 실행 중 색인도 안다 — 이 에셋을 가리키는 참조가 다음 기동 전에도 GUID 로 쓰인다
                    return true;
                }
                break;
            }
            case AssetIdentityKind::Source:
            {
                // 베낀 서술의 임포트 예외는 지킨다(유니티 Duplicate 와 같다) — 복사본은 새 에셋이지만 같은 임포트 설정이다. 읽지 못한 서술은 오류를 남기고 예외 없이 새로 쓴다(복사본의 정체성이 먼저다).
                AssetMeta  copied;
                string     copiedText;
                const bool bCopied = FileUtil::isRegularFile( AssetMeta::makePath( absPath ) ) && FileUtil::readTextFile( AssetMeta::makePath( absPath ), copiedText ) &&
                                     copied.parseXML( copiedText, AssetMeta::makePath( assetID ) ).has_value();
                if ( FileUtil::writeTextFile( AssetMeta::makePath( absPath ), AssetMeta::makeText( assetID, guid, bCopied ? copied._listImportOverride : vector<AssetImportOverride>{} ) ) )
                {
                    AssetRegistry::record( guid, assetID );
                    return true;
                }
                break;
            }
        }
        SW_LOG_ERROR_ID( "AssetIdentity.AssignFailed", "Could not give '%#' its asset GUID - run py -3 -m Scripts asset-ids --fix", assetID );
        return false;
    }

    bool AssetIdentityRules::replaceContentKeepingIdentity( string_view targetAbs, string_view targetID, string_view newContentAbs ) const
    {
        const AssetIdentityKind kind = classify( targetID );
        if ( kind == AssetIdentityKind::Header )
        {
            string oldText;
            string newText;
            if ( FileUtil::readTextFile( targetAbs, oldText ) == false || FileUtil::readTextFile( newContentAbs, newText ) == false )
            {
                SW_LOG_ERROR_ID( "AssetIdentity.ReplaceUnreadable", "Could not read '%#' or the new content '%#' - nothing was replaced", targetID, string( newContentAbs ) );
                return false;
            }
            const AssetGUIDHeader header = AssetGUIDHeader::read( oldText );
            if ( header._state != AssetGUIDHeaderState::Found )
            {
                SW_LOG_ERROR_ID( "AssetIdentity.ReplaceNoIdentity", "'%#' has no asset GUID to keep - run py -3 -m Scripts asset-ids --fix first, nothing was replaced", targetID );
                return false;
            }
            // 쓰기는 임시 파일을 바꿔 끼운다(`writeTextFile`) — 지면 옛 글이 그대로다.
            if ( FileUtil::writeTextFile( targetAbs, AssetGUIDHeader::write( newText, *header._guid ) ) )
                return true;
            SW_LOG_ERROR_ID( "AssetIdentity.ReplaceWriteFailed", "Could not write '%#' - nothing was replaced", targetID );
            return false;
        }
        if ( kind == AssetIdentityKind::Source && FileUtil::isRegularFile( AssetMeta::makePath( targetAbs ) ) == false )
        {
            SW_LOG_ERROR_ID( "AssetIdentity.ReplaceNoIdentity", "'%#' has no asset GUID to keep - run py -3 -m Scripts asset-ids --fix first, nothing was replaced", targetID );
            return false;
        }
        // 원본 · 정체성이 없는 파일은 바이트만 바꾼다(곁의 .meta 는 그대로). 지면 옛 바이트로 되돌린다.
        StringBuilder<constant::buffer::kMax512> backupBuilder;
        backupBuilder.appendFormat( "%#.replacing", targetAbs );
        const string backupAbs( backupBuilder.view() );
        if ( FileUtil::copyFile( targetAbs, backupAbs ) == false )
        {
            SW_LOG_ERROR_ID( "AssetIdentity.ReplaceWriteFailed", "Could not write '%#' - nothing was replaced", targetID );
            return false;
        }
        if ( FileUtil::copyFile( newContentAbs, targetAbs ) )
        {
            (void)FileUtil::tryRemoveFile( backupAbs ); // 남아도 다음 바꾸기가 덮는다
            return true;
        }
        if ( FileUtil::copyFile( backupAbs, targetAbs ) )
        {
            (void)FileUtil::tryRemoveFile( backupAbs );
            SW_LOG_ERROR_ID( "AssetIdentity.ReplaceWriteFailed", "Could not write '%#' - nothing was replaced", targetID );
            return false;
        }
        SW_LOG_ERROR_ID( "AssetIdentity.ReplaceRestoreFailed", "Could not replace '%#' nor put it back - the old bytes are kept in '%#'", targetID, backupAbs );
        return false;
    }

    string AssetIdentityIssue::toString() const
    {
        StringBuilder<constant::buffer::kMax512> builder;
        switch ( _kind )
        {
            case AssetIdentityIssueKind::DuplicateGUID:
                builder.appendFormat( "%#: has the same GUID as %# - a copy needs a new GUID (py -3 -m Scripts asset-ids --fix)", _assetID, _otherAssetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::MissingHeader:
                builder.appendFormat( "%#: has no <?asset guid?> header (py -3 -m Scripts asset-ids --fix)", _assetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::InvalidHeader:
                builder.appendFormat( "%#: the <?asset guid?> header is unreadable, misplaced or repeated", _assetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::MissingMeta:
                builder.appendFormat( "%#: has no .meta beside it (py -3 -m Scripts asset-ids --fix)", _assetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::OrphanMeta:
                builder.appendFormat( "%#: its source file is missing - move the .meta with the file or let asset-ids --fix reattach it", _assetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::InvalidMeta:
                builder.appendFormat( "%#: the .meta is unreadable or names another source file", _assetID );
                return string( builder.view() );
            case AssetIdentityIssueKind::Unreadable:
                builder.appendFormat( "%#: could not be read", _assetID );
                return string( builder.view() );
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
        VectorUtil::sort( listSorted, []( const AssetIdentityFile* pLeft, const AssetIdentityFile* pRight )
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
            const string assetID = normalized.substr( root.size() + 1 );
            // 작업 도메인(시나리오 · 테스트 사본 — 실행이 쓰고 지운다)은 기동 색인에 넣지 않는다: 원본을 머리째 복사해 GUID 가 겹친다. 정체성 규칙(`classify`)은
            // 그 안도 보통 에셋으로 본다 — 사본 안의 가져오기 · 내용 바꾸기가 GUID 를 주고 읽어야 한다(editor.assetidentity).
            if ( FileUtil::startsWithPathComponent( FileUtil::normalizePath( assetID ), AssetPaths::kScratchDomain ) )
                continue;
            listID.push_back( assetID );
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

    optional<AssetGUID> AssetIdentityRules::readIdentity( string_view absPath, string_view assetID ) const
    {
        const AssetIdentityKind kind = classify( assetID );
        string                  text;
        if ( kind == AssetIdentityKind::Header && FileUtil::readTextFile( absPath, text ) )
            return AssetGUIDHeader::read( text )._guid;
        if ( kind == AssetIdentityKind::Source && FileUtil::isRegularFile( AssetMeta::makePath( absPath ) ) && FileUtil::readTextFile( AssetMeta::makePath( absPath ), text ) )
        {
            AssetMeta meta;
            return meta.parseXML( text, AssetMeta::makePath( assetID ) );
        }
        return nullopt;
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

    AssetRegistryLayer AssetDatabase::makeRegistryLayer( string_view sourceName ) const
    {
        AssetRegistryLayer layer{ string( sourceName ), {} };
        layer._listEntry.reserve( _mapGUIDToPath.size() );
        for ( const auto& [guid, assetID] : _mapGUIDToPath )
        {
            layer._listEntry.push_back( AssetRegistryEntry{ guid, assetID } );
        }
        return layer;
    }
} // namespace sw
