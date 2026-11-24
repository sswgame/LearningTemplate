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
#include "Engine/Content/AssetScanCache.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"
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

            /** @brief 정체성을 드는 파일의 글 @p text 에서 `AssetDatabase::build` 가 보는 것만 남긴 글입니다(훑기 캐시에 둔다). 결함이 있는 글은 없음입니다 — 다음 훑기가 다시 읽는다. */
            [[nodiscard]] static optional<string> makeScanPayload( AssetIdentityKind kind, string_view assetID, const string& text )
            {
                if ( kind == AssetIdentityKind::Sidecar || AssetRedirect::isTable( assetID ) )
                    return text;
                const AssetGUIDHeader header = AssetGUIDHeader::read( text );
                switch ( header._state )
                {
                    case AssetGUIDHeaderState::Found:
                        return AssetGUIDHeader::write( {}, *header._guid );
                    case AssetGUIDHeaderState::Missing:
                        return string{}; // 머리 없는 글(임포트 산출물)은 빈 글과 같다
                    case AssetGUIDHeaderState::Invalid:
                        break;
                }
                return nullopt;
            }

            /** @brief 훑은 파일 하나입니다. */
            struct ScannedFile
            {
                string    _assetID;
                FileStamp _fileStamp{};
            };
        };
    } // namespace

    bool AssetIdentityRules::load( const AbsolutePath& projectRoot )
    {
        const AbsolutePath path = projectRoot.join( kConfigFile );
        JSONDocument       document;
        if ( document.loadFromFile( path ) == false )
        {
            SW_LOG_ERROR_ID( "AssetIdentityRules.ConfigUnreadable", "Asset identity rules %# could not be read", path.str() );
            return false;
        }
        AssetIdentityRules loaded;
        // 구조체 목록 칸은 `JSONFields` 가 직렬화기로 넘겨 C++ 칸 이름을 키로 기대한다 — 이 파일은 칸 이름 규칙이라 원소마다 읽는다.
        bool            bValid   = JSONFields::readProperties( document.getRoot(), loaded, path.str(), { "_comment", "listImportedFolder" } );
        const JSONValue imported = document.getRoot().get( "listImportedFolder" );
        if ( imported.isArray() == false || imported.size() == 0 )
        {
            SW_LOG_ERROR_ID( "AssetIdentityRules.NoImportedFolder", "Asset identity rules %# need listImportedFolder", path.str() );
            bValid = false;
        }
        for ( size_t index = 0; imported.isArray() && index < imported.size(); ++index )
        {
            AssetImportedFolder folder;
            bValid = JSONFields::readProperties( imported.at( index ), folder, path.str() ) && bValid;
            bValid = AssetDatabaseInternal::hasNoEmptyText( folder._listSourceExtension, "listSourceExtension", path.str() ) && bValid;
            bValid = AssetDatabaseInternal::hasNoEmptyText( folder._listOutputSuffix, "listOutputSuffix", path.str() ) && bValid;
            loaded._listImportedFolder.push_back( std::move( folder ) );
        }
        bValid = AssetDatabaseInternal::hasNoEmptyText( loaded._listHeaderSuffix, "listHeaderSuffix", path.str() ) && bValid;
        bValid = AssetDatabaseInternal::hasNoEmptyText( loaded._listSkipPattern, "listSkipPattern", path.str() ) && bValid;
        bValid = AssetDatabaseInternal::hasNoEmptyText( loaded._listKeptDerived, "listKeptDerived", path.str() ) && bValid;
        if ( bValid == false )
            return false;
        _listHeaderSuffix   = std::move( loaded._listHeaderSuffix );
        _listSkipPattern    = std::move( loaded._listSkipPattern );
        _listKeptDerived    = std::move( loaded._listKeptDerived );
        _listImportedFolder = std::move( loaded._listImportedFolder );
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
        for ( const AssetImportedFolder& imported : _listImportedFolder )
        {
            marker.clear();
            marker.appendFormat( "/%#/", imported._folder );
            if ( wrapped.view().find( marker.view() ) != string_view::npos )
                return true;
        }
        return false;
    }

    bool AssetIdentityRules::isMisplacedInDerivedFolder( string_view assetID ) const
    {
        if ( classify( assetID ) != AssetIdentityKind::Header || isUnderDerivedFolder( assetID ) == false )
            return false;
        for ( const string& pattern : _listKeptDerived )
        {
            if ( StringUtil::matchesWildcard( pattern, assetID ) )
                return false;
        }
        return true;
    }

    void AssetIdentityRules::collectImportedSources( string_view outputID, vector<AssetImportedSource>& outListSource ) const
    {
        const string lowered = FileUtil::normalizePath( outputID ).str();
        string_view  fileName;
        FileUtil::getFileNamePart( lowered, fileName );
        const bool bFile = fileName.find( '.' ) != string_view::npos; // 폴더는 접미사를 보지 않는다
        for ( const AssetImportedFolder& imported : _listImportedFolder )
        {
            const bool bOutput = std::any_of( imported._listOutputSuffix.begin(), imported._listOutputSuffix.end(), [fileName]( const string& suffix )
            { return StringUtil::endsWith( fileName, suffix ); } );
            if ( bFile && bOutput == false )
                continue;
            StringBuilder<constant::buffer::kMax256> markerBuilder;
            markerBuilder.appendFormat( "/%#/", imported._folder );
            const string_view marker = markerBuilder.view();
            for ( size_t position = lowered.find( marker ); position != string::npos; position = lowered.find( marker, position + 1 ) )
            {
                const size_t tailStart = position + marker.size();
                // 줄기 끝을 뒤에서부터 옮긴다 — 마지막 조각은 확장자를 뺀 두 줄기(첫 점 앞 · 마지막 점 앞), 그 앞 조각은 폴더 이름도 줄기다.
                for ( size_t segmentEnd = lowered.size(); segmentEnd > tailStart; segmentEnd = lowered.rfind( '/', segmentEnd - 1 ) )
                {
                    const size_t slash        = lowered.rfind( '/', segmentEnd - 1 ); // 폴더 표지의 `/` 가 있어 늘 있다
                    const string segment      = lowered.substr( slash + 1, segmentEnd - slash - 1 );
                    const size_t firstStem    = segment.find( '.' );
                    const size_t lastStem     = segment.rfind( '.' );
                    const size_t arrStemEnd[] = { firstStem == string::npos ? segment.size() : firstStem, lastStem == string::npos ? segment.size() : lastStem };
                    for ( size_t stemIndex = 0; stemIndex < SW_COUNT_OF( arrStemEnd ); ++stemIndex )
                    {
                        if ( arrStemEnd[stemIndex] == 0 || ( stemIndex > 0 && arrStemEnd[stemIndex] == arrStemEnd[0] ) )
                            continue;
                        const size_t stemEnd = slash + 1 + arrStemEnd[stemIndex];
                        for ( const string& extension : imported._listSourceExtension )
                        {
                            StringBuilder<constant::buffer::kMax512> sourceBuilder;
                            sourceBuilder.appendFormat( "%#/%#/%#%#", string_view( lowered ).substr( 0, position ), imported._sourceFolder, string_view( lowered ).substr( tailStart, stemEnd - tailStart ), extension );
                            outListSource.push_back( AssetImportedSource{ string( sourceBuilder.view() ), lowered.substr( stemEnd ) } );
                        }
                    }
                    if ( slash < tailStart )
                        break; // 산출물 폴더 바로 아래 조각까지 봤다
                }
            }
        }
    }

    optional<AssetSourceAddress> AssetSourceAddress::tryParse( string_view text )
    {
        AssetSourceAddress address;
        if ( StringUtil::startsWith( text, kStoreScheme ) )
        {
            const string_view rest = text.substr( kStoreScheme.size() );
            if ( rest.size() <= kContentHashLength || rest[kContentHashLength] != '/' )
                return nullopt;
            address._contentHash = rest.substr( 0, kContentHashLength );
            text                 = rest.substr( kContentHashLength + 1 );
            const bool bHex      = std::all_of( address._contentHash.begin(), address._contentHash.end(), []( utf8 character )
                 { return ( '0' <= character && character <= '9' ) || ( 'a' <= character && character <= 'f' ); } );
            if ( bHex == false )
                return nullopt;
        }
        if ( text.empty() || text == "." || text == ".." || text.find_first_of( "/\\:" ) != string_view::npos )
            return nullopt;
        address._name = text;
        return address;
    }

    string AssetSourceAddress::toText() const
    {
        if ( isStored() == false )
            return _name;
        StringBuilder<constant::buffer::kMax256> builder;
        builder.appendFormat( "%#%#/%#", kStoreScheme, _contentHash, _name );
        return string( builder.view() );
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
        if ( AssetSourceAddress::tryParse( _source ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "AssetMeta.InvalidSource", "%# has source '%#' - a source is a file name or store:<SHA-256>/<file name>", label, _source );
            return nullopt;
        }
        return guid;
    }

    void AssetMeta::renameSource( string_view sourcePath )
    {
        optional<AssetSourceAddress> address = AssetSourceAddress::tryParse( _source );
        SW_ASSERT( address.has_value() ); // 읽은 서술이면 늘 있다(`parseXML`)
        if ( address.has_value() == false )
            return;
        address->_name = FileUtil::getFileNamePart( sourcePath );
        _source        = address->toText();
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

    bool AssetIdentityRules::assignNewIdentity( const AbsolutePath& absPath, string_view assetID ) const
    {
        const AssetGUID guid = AssetGUID::create();
        switch ( classify( assetID ) )
        {
            case AssetIdentityKind::Skipped:
            case AssetIdentityKind::Sidecar:
                return true;
            case AssetIdentityKind::Header:
            {
                const AbsolutePath diskPath = absPath;
                string             text;
                if ( FileUtil::readTextFile( diskPath, text ) && FileUtil::writeTextFile( diskPath, AssetGUIDHeader::write( text, guid ) ) )
                {
                    AssetRegistry::record( guid, assetID ); // 실행 중 색인도 안다 — 이 에셋을 가리키는 참조가 다음 기동 전에도 GUID 로 쓰인다
                    return true;
                }
                break;
            }
            case AssetIdentityKind::Source:
            {
                // 베낀 서술의 임포트 예외는 지킨다(유니티 Duplicate 와 같다) — 복사본은 새 에셋이지만 같은 임포트 설정이다. 읽지 못한 서술은 오류를 남기고 예외 없이 새로 쓴다(복사본의 정체성이 먼저다).
                const AbsolutePath metaPath = AssetMeta::makePath( absPath );
                AssetMeta          copied;
                string             copiedText;
                const bool         bCopied = FileUtil::isRegularFile( metaPath ) && FileUtil::readTextFile( metaPath, copiedText ) && copied.parseXML( copiedText, AssetMeta::makePath( assetID ) ).has_value();
                if ( FileUtil::writeTextFile( metaPath, AssetMeta::makeText( assetID, guid, bCopied ? copied._listImportOverride : vector<AssetImportOverride>{} ) ) )
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

    bool AssetIdentityRules::replaceContentKeepingIdentity( const AbsolutePath& targetAbs, string_view targetID, const AbsolutePath& newContentAbs ) const
    {
        const AssetIdentityKind kind = classify( targetID );
        if ( kind == AssetIdentityKind::Header )
        {
            string oldText;
            string newText;
            if ( FileUtil::readTextFile( targetAbs, oldText ) == false || FileUtil::readTextFile( newContentAbs, newText ) == false )
            {
                SW_LOG_ERROR_ID( "AssetIdentity.ReplaceUnreadable", "Could not read '%#' or the new content '%#' - nothing was replaced", targetID, string( newContentAbs.str() ) );
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
        const AbsolutePath backupAbs = targetAbs.appendSuffix( ".replacing" );
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
        SW_LOG_ERROR_ID( "AssetIdentity.ReplaceRestoreFailed", "Could not replace '%#' nor put it back - the old bytes are kept in '%#'", targetID, backupAbs.str() );
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
            case AssetIdentityIssueKind::InvalidRedirect:
                builder.appendFormat( "%#: an <IdentityRedirect> row of the redirect table is malformed (see the log)", _assetID );
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

        AssetDatabase         database;
        vector<const string*> listOutputID; // 임포트 산출물 — 원본이 모두 오른 뒤에 줄을 적는다
        for ( const AssetIdentityFile* pFile : listSorted )
        {
            const string& assetID = pFile->_assetID;
            switch ( rules.classify( assetID ) )
            {
                case AssetIdentityKind::Skipped:
                    break;
                case AssetIdentityKind::Header:
                {
                    if ( AssetRedirect::isTable( assetID ) && AssetRedirect::makeTableID( assetID ) == assetID )
                    {
                        const expected<vector<AssetIdentityRedirect>> rows = AssetRedirect::parseIdentityRows( pFile->_text, assetID );
                        if ( rows.has_value() )
                            database._listRedirect.insert( database._listRedirect.end(), rows->begin(), rows->end() );
                        else
                            outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::InvalidRedirect, assetID, {} } );
                    }
                    const AssetGUIDHeader header = AssetGUIDHeader::read( pFile->_text );
                    if ( header._state == AssetGUIDHeaderState::Found )
                        Internal::registerGUID( database._mapGUIDToPath, database._mapPathToGUID, *header._guid, assetID, outListIssue );
                    else if ( header._state == AssetGUIDHeaderState::Missing && rules.isUnderDerivedFolder( assetID ) )
                        listOutputID.push_back( &assetID );
                    else
                        outListIssue.push_back( AssetIdentityIssue{ header._state == AssetGUIDHeaderState::Missing ? AssetIdentityIssueKind::MissingHeader : AssetIdentityIssueKind::InvalidHeader, assetID, {} } );
                    break;
                }
                case AssetIdentityKind::Sidecar:
                {
                    const string_view sourceID = Internal::toSourceID( assetID );
                    if ( rules.classify( sourceID ) != AssetIdentityKind::Source )
                    {
                        // 곁의 파일이 정체성을 스스로 드는 종류다(머리 글 · 건너뛰는 파일의 .meta 는 짝이 아니다)
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::OrphanMeta, assetID, {} } );
                        break;
                    }
                    AssetMeta                          meta;
                    const optional<AssetGUID>          guid    = meta.parseXML( pFile->_text, assetID );
                    const optional<AssetSourceAddress> address = AssetSourceAddress::tryParse( meta._source );
                    const bool                         bBeside = setSourceID.contains( Internal::toLowerID( sourceID ) );
                    if ( guid.has_value() && address->isStored() == false && bBeside == false )
                    {
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::OrphanMeta, assetID, {} } );
                        break;
                    }
                    // 곁에 파일이 있는데 저장소 주소를 적었으면 어느 쪽이 원본인지 둘이 된다 — 고르지 않고 결함이다
                    if ( guid.has_value() == false || address->_name != FileUtil::getFileNamePart( sourceID ) || address->isStored() == bBeside )
                    {
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::InvalidMeta, assetID, {} } );
                        break;
                    }
                    Internal::registerGUID( database._mapGUIDToPath, database._mapPathToGUID, *guid, sourceID, outListIssue );
                    break;
                }
                case AssetIdentityKind::Source:
                    if ( setSidecarID.contains( Internal::toLowerID( AssetMeta::makePath( assetID ) ) ) )
                        break;
                    if ( rules.isUnderDerivedFolder( assetID ) )
                        listOutputID.push_back( &assetID );
                    else
                        outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::MissingMeta, assetID, {} } );
                    break;
            }
        }
        vector<AssetImportedSource> listSource;
        for ( const string* pOutputID : listOutputID )
        {
            listSource.clear();
            rules.collectImportedSources( *pOutputID, listSource );
            for ( const AssetImportedSource& source : listSource )
            {
                const optional<AssetGUID> sourceGUID = database.findGUID( source._sourceID );
                if ( sourceGUID.has_value() == false )
                    continue;
                Internal::registerGUID( database._mapGUIDToPath, database._mapPathToGUID, AssetGUID::fromName( *sourceGUID, source._outputName ), *pOutputID, outListIssue );
                break;
            }
        }
        return database;
    }

    AssetDatabase AssetDatabase::scan( const AbsolutePath& contentRoot, const AssetIdentityRules& rules, vector<AssetIdentityIssue>& outListIssue, AssetScanCache* pCache )
    {
        using Internal                         = AssetDatabaseInternal;
        const AbsolutePath            root     = contentRoot;
        const AbsolutePath            rootPath = root;
        vector<Internal::ScannedFile> listScanned;
        auto                          collectEntry = [&rootPath, &listScanned]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory || entry._path.size() <= rootPath.str().size() + 1 )
                return true;
            string assetID{ entry._path.substr( rootPath.str().size() + 1 ) };
            // 작업 도메인(시나리오 · 테스트 사본 — 실행이 쓰고 지운다)은 기동 색인에 넣지 않는다: 원본을 머리째 복사해 GUID 가 겹친다. 정체성 규칙(`classify`)은
            // 그 안도 보통 에셋으로 본다 — 사본 안의 가져오기 · 내용 바꾸기가 GUID 를 주고 읽어야 한다(editor.assetidentity).
            if ( FileUtil::startsWithPathComponent( FileUtil::normalizePath( assetID ), AssetPaths::kScratchDomain ) == false )
                listScanned.push_back( Internal::ScannedFile{ std::move( assetID ), entry._fileStamp } );
            return true;
        };
        if ( FileUtil::isDirectory( rootPath ) == false || FileUtil::forEachDirectoryEntry( rootPath, true, collectEntry ) == false )
        {
            outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::Unreadable, root.str(), {} } );
            return AssetDatabase{};
        }
        vector<AssetIdentityFile> listFile;
        for ( const Internal::ScannedFile& scanned : listScanned )
        {
            const string&           assetID = scanned._assetID;
            const AssetIdentityKind kind    = rules.classify( assetID );
            if ( kind == AssetIdentityKind::Skipped )
                continue;
            AssetIdentityFile file{ assetID, {} };
            if ( kind == AssetIdentityKind::Header || kind == AssetIdentityKind::Sidecar )
            {
                const vector<uint8>* pCached = pCache != nullptr ? pCache->find( assetID, scanned._fileStamp ) : nullptr;
                if ( pCached != nullptr )
                {
                    file._text.assign( pCached->begin(), pCached->end() );
                }
                else if ( FileUtil::readTextFile( root.join( assetID ), file._text ) == false )
                {
                    outListIssue.push_back( AssetIdentityIssue{ AssetIdentityIssueKind::Unreadable, assetID, {} } );
                    continue;
                }
                else if ( pCache != nullptr )
                {
                    const optional<string> payload = Internal::makeScanPayload( kind, assetID, file._text );
                    if ( payload.has_value() )
                        pCache->record( assetID, scanned._fileStamp, vector<uint8>( payload->begin(), payload->end() ) );
                }
            }
            listFile.push_back( std::move( file ) );
        }
        return build( listFile, rules, outListIssue );
    }

    optional<AssetGUID> AssetIdentityRules::readIdentity( const AbsolutePath& absPath, string_view assetID ) const
    {
        const AssetIdentityKind kind = classify( assetID );
        string                  text;
        const bool              bHeader = kind == AssetIdentityKind::Header;
        if ( bHeader == false && kind != AssetIdentityKind::Source )
            return nullopt;
        if ( bHeader && FileUtil::readTextFile( absPath, text ) )
        {
            const optional<AssetGUID> headerGUID = AssetGUIDHeader::read( text )._guid;
            if ( headerGUID.has_value() )
                return headerGUID;
        }
        const AbsolutePath metaPath = AssetMeta::makePath( absPath );
        if ( bHeader == false && FileUtil::isRegularFile( metaPath ) && FileUtil::readTextFile( metaPath, text ) )
        {
            AssetMeta meta;
            return meta.parseXML( text, AssetMeta::makePath( assetID ) );
        }
        // 자기 정체성이 없는 산출물 자리의 파일은 임포트 산출물이다 — 원본의 `.meta` 와 산출물 이름이 정체성이다.
        if ( isUnderDerivedFolder( assetID ) == false || absPath.str().size() <= assetID.size() )
            return nullopt;
        const AbsolutePath          contentRoot = FileUtil::toDiskPath( absPath.str().substr( 0, absPath.str().size() - assetID.size() ) );
        vector<AssetImportedSource> listSource;
        collectImportedSources( assetID, listSource );
        for ( const AssetImportedSource& source : listSource )
        {
            const AbsolutePath sourceMetaPath = AssetMeta::makePath( contentRoot.join( source._sourceID ) );
            if ( FileUtil::isRegularFile( sourceMetaPath ) == false || FileUtil::readTextFile( sourceMetaPath, text ) == false )
                continue;
            AssetMeta                 meta;
            const optional<AssetGUID> sourceGUID = meta.parseXML( text, AssetMeta::makePath( source._sourceID ) );
            return sourceGUID.has_value() ? optional<AssetGUID>{ AssetGUID::fromName( *sourceGUID, source._outputName ) } : nullopt;
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
        AssetRegistryLayer layer{ string( sourceName ), {}, _listRedirect };
        layer._listEntry.reserve( _mapGUIDToPath.size() );
        for ( const auto& [guid, assetID] : _mapGUIDToPath )
        {
            layer._listEntry.push_back( AssetRegistryEntry{ guid, assetID } );
        }
        return layer;
    }
} // namespace sw
