/**
 * @file AssetReferenceFields.cpp
 * @brief 에셋 참조 칸 목록의 읽기 · 뽑기 · 있음 판정 · 고쳐 쓰기를 구현합니다. 뽑기는 `Scripts/lint/AssetReferences.py` 의 `_extract*` 와 같은 규칙이다.
 */
#include "pch.h"

#include "Engine/Asset/Reference/AssetReferenceFields.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/JSON/JSONFields.h"

namespace sw
{
    namespace
    {
        struct AssetReferenceFieldsInternal
        {
            /** @brief 경로 낱말을 가르는 글자입니다(값 하나에 경로 여럿 — 쉼표 · 세미콜론 · 공백). 리소스 이름 규칙(`[a-z0-9_.-/]`) 안에는 오지 않는다. */
            static constexpr string_view kTokenSeparators = ",; \t\r\n#"; // `#` 뒤는 파일 안의 항목 id 다(`DataCatalogInheritance`) - 참조는 앞의 파일이다
            /** @brief 이 글자가 든 낱말은 경로가 아니라 틀입니다(`{bind:…}` · CMake `${…}` · 와일드카드). */
            static constexpr string_view kTemplateCharacters = "{}$*%<>";
            /** @brief CMake 인자 앞에 올 수 있는 글자(줄 처음 말고)와 값이 끝나는 글자입니다. */
            static constexpr string_view kCMakeArgumentBoundary = " \t\r\n\"(";
            static constexpr string_view kCMakeValueEnd         = " \t\r\n\")";
            /** @brief 목록 원소 요소의 이름입니다(`<_listX><item>경로</item></_listX>` — 리플렉션 컨테이너 직렬화). */
            static constexpr string_view kItemElement = "item";

            static bool isOneOf( utf8 character, string_view set ) { return set.find( character ) != string_view::npos; }

            static bool isNameStart( utf8 character ) { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || character == '_'; }

            static bool isNameChar( utf8 character )
            {
                return isNameStart( character ) || ( '0' <= character && character <= '9' ) || character == '.' || character == ':' || character == '-';
            }

            static bool isSpace( utf8 character ) { return character == ' ' || character == '\t' || character == '\r' || character == '\n'; }

            static string normalizePath( string_view token )
            {
                string normalized( token );
                for ( utf8& character : normalized )
                {
                    character = character == '\\' ? '/' : StringUtil::toLowerChar( character );
                }
                return normalized;
            }

            static bool startsWithDomain( string_view path, const vector<string>& listDomain )
            {
                for ( const string& domain : listDomain )
                {
                    if ( path.size() > domain.size() && StringUtil::startsWith( path, domain ) && path[domain.size()] == '/' )
                        return true;
                }
                return false;
            }

            static vector<size_t> makeLineStarts( string_view text )
            {
                vector<size_t> listLineStart{ 0 };
                for ( size_t index = 0; index < text.size(); ++index )
                {
                    if ( text[index] == '\n' )
                        listLineStart.push_back( index + 1 );
                }
                return listLineStart;
            }

            static uint32 lineOf( const vector<size_t>& listLineStart, size_t offset )
            {
                const auto itNext = std::upper_bound( listLineStart.begin(), listLineStart.end(), offset );
                return static_cast<uint32>( itNext - listLineStart.begin() );
            }

            /** @brief `>` 다음 자리입니다. 따옴표 안의 `>` 는 건너뛴다. */
            static size_t findTagEnd( string_view text, size_t tagStart )
            {
                utf8 quote = '\0';
                for ( size_t index = tagStart + 1; index < text.size(); ++index )
                {
                    const utf8 character = text[index];
                    if ( quote != '\0' )
                    {
                        if ( character == quote )
                            quote = '\0';
                    }
                    else if ( character == '"' || character == '\'' )
                        quote = character;
                    else if ( character == '>' )
                        return index + 1;
                }
                return text.size();
            }

            /** @brief JSON 문자열 `"…"` 의 내용 끝(닫는 따옴표 자리)입니다. 닫히지 않으면 npos. */
            static size_t findStringEnd( string_view text, size_t contentStart )
            {
                for ( size_t index = contentStart; index < text.size(); ++index )
                {
                    if ( text[index] == '\\' )
                        ++index;
                    else if ( text[index] == '"' )
                        return index;
                }
                return string_view::npos;
            }

            /** @brief 칸 목록의 글이 모두 비어 있지 않으면 true 입니다. 빈 글은 모든 경로의 앞머리가 되어 규칙을 망가뜨린다. */
            [[nodiscard]] static bool hasNoEmptyText( const vector<string>& listText, string_view key, string_view sourceLabel )
            {
                for ( size_t index = 0; index < listText.size(); ++index )
                {
                    if ( listText[index].empty() == false )
                        continue;
                    SW_LOG_ERROR_ID( "AssetReferenceFields.ElementNotText", "%#: '%#' element %# is not a non-empty string", sourceLabel, key, static_cast<uint32>( index ) );
                    return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AssetIDSet::addFile( string_view assetID )
    {
        const string lowered = AssetReferenceFieldsInternal::normalizePath( assetID );
        size_t       slash   = lowered.rfind( '/' );
        while ( slash != string::npos && slash > 0 )
        {
            if ( _setFolder.insert( lowered.substr( 0, slash ) ).second == false )
                break; // 위 폴더들은 이미 들어 있다
            slash = lowered.rfind( '/', slash - 1 );
        }
        _setFile.insert( lowered );
    }

    bool AssetIDSet::contains( string_view assetID ) const
    {
        const string key( assetID );
        return _setFile.find( key ) != _setFile.end() || _setFolder.find( key ) != _setFolder.end();
    }

    bool AssetReferenceFields::load( string_view projectRoot )
    {
        const string path = FileUtil::joinPath( projectRoot, kConfigFile );
        JSONDocument document;
        if ( document.loadFromFile( path ) == false )
        {
            SW_LOG_ERROR_ID( "AssetReferenceFields.ConfigUnreadable", "Asset reference fields %# could not be read", path );
            return false;
        }
        AssetReferenceFieldsConfig config;
        using Internal       = AssetReferenceFieldsInternal;
        const JSONValue root = document.getRoot();
        // 구조체 목록 칸은 `JSONFields` 가 직렬화기로 넘겨 C++ 칸 이름을 키로 기대한다 — 이 파일은 칸 이름 규칙이라 원소마다 읽는다.
        bool                        bValid   = JSONFields::readFields( root, config, path, { "listImportedFolder" } );
        const JSONValue             imported = root.get( "listImportedFolder" );
        vector<AssetImportedFolder> listImported;
        if ( imported.isArray() == false )
        {
            SW_LOG_ERROR_ID( "AssetReferenceFields.ConfigInvalid", "Asset reference fields %# need listImportedFolder", path );
            bValid = false;
        }
        for ( size_t index = 0; imported.isArray() && index < imported.size(); ++index )
        {
            AssetImportedFolder folder;
            bValid = JSONFields::readFields( imported.at( index ), folder, path ) && bValid;
            listImported.push_back( std::move( folder ) );
        }
        bValid = Internal::hasNoEmptyText( config._listDomain, "listDomain", path ) && bValid;
        bValid = Internal::hasNoEmptyText( config._listUncheckedDomain, "listUncheckedDomain", path ) && bValid;
        bValid = Internal::hasNoEmptyText( config._listTextExtension, "listTextExtension", path ) && bValid;
        bValid = Internal::hasNoEmptyText( config._listCMakeFile, "listCMakeFile", path ) && bValid;
        bValid = Internal::hasNoEmptyText( config._listCMakeArgument, "listCMakeArgument", path ) && bValid;
        for ( const AssetImportedFolder& folder : listImported )
        {
            bValid = Internal::hasNoEmptyText( folder._listSourceExtension, "listSourceExtension", path ) && bValid;
        }
        if ( bValid == false )
            return false;
        _listDomain          = std::move( config._listDomain );
        _listUncheckedDomain = std::move( config._listUncheckedDomain );
        _listTextExtension   = std::move( config._listTextExtension );
        _listCMakeFile       = std::move( config._listCMakeFile );
        _listCMakeArgument   = std::move( config._listCMakeArgument );
        _listImported        = std::move( listImported );
        _setField.clear();
        return loadGeneratedFields( projectRoot );
    }

    bool AssetReferenceFields::loadGeneratedFields( string_view projectRoot )
    {
        const string path = FileUtil::joinPath( projectRoot, kGeneratedFile );
        JSONDocument document;
        if ( document.loadFromFile( path ) == false )
        {
            SW_LOG_ERROR_ID( "AssetReferenceFields.GeneratedUnreadable", "Generated asset reference fields %# could not be read", path );
            return false;
        }
        const JSONValue listOwner = document.getRoot().get( "listOwner" );
        if ( listOwner.isArray() == false )
        {
            SW_LOG_ERROR_ID( "AssetReferenceFields.GeneratedInvalid", "Generated asset reference fields %# need listOwner", path );
            return false;
        }
        for ( size_t ownerIndex = 0; ownerIndex < listOwner.size(); ++ownerIndex )
        {
            const JSONValue listField = listOwner.at( ownerIndex ).get( "listField" );
            if ( listField.isArray() == false )
            {
                SW_LOG_ERROR_ID( "AssetReferenceFields.GeneratedInvalid2", "Generated asset reference fields %#: owner %# needs listField", path, static_cast<uint32>( ownerIndex ) );
                return false;
            }
            for ( size_t fieldIndex = 0; fieldIndex < listField.size(); ++fieldIndex )
            {
                const auto field = listField.at( fieldIndex ).read<string>();
                if ( field.has_value() == false || field->empty() )
                {
                    SW_LOG_ERROR_ID( "AssetReferenceFields.GeneratedInvalid3", "Generated asset reference fields %#: owner %# field %# is not a non-empty string", path, static_cast<uint32>( ownerIndex ),
                                     static_cast<uint32>( fieldIndex ) );
                    return false;
                }
                _setField.insert( *field );
            }
        }
        return true;
    }

    bool AssetReferenceFields::isField( string_view name ) const
    {
        return _setField.find( string( name ) ) != _setField.end();
    }

    bool AssetReferenceFields::isTextAsset( string_view assetID ) const
    {
        const string lowered = AssetReferenceFieldsInternal::normalizePath( assetID );
        return std::any_of( _listTextExtension.begin(), _listTextExtension.end(), [&lowered]( const string& extension )
        { return StringUtil::endsWith( lowered, extension ); } );
    }

    bool AssetReferenceFields::isReferrer( string_view assetID ) const
    {
        const string lowered = AssetReferenceFieldsInternal::normalizePath( assetID );
        // 리다이렉트 표(`redirects.xml`)는 옛 id 를 적는 글이다 — 있는지 검사하거나 옮기기가 고칠 참조가 아니다(`from` 은 다른 형식의 경로 칸 이름이기도 하다).
        if ( isTextAsset( lowered ) == false || AssetReferenceFieldsInternal::startsWithDomain( lowered, _listUncheckedDomain ) || AssetRedirect::isTable( lowered ) )
            return false;
        const string wrapped = "/" + lowered;
        return std::none_of( _listImported.begin(), _listImported.end(), [&wrapped]( const AssetImportedFolder& folder )
        { return folder._bGeneratedReferrers && wrapped.find( "/" + folder._folder + "/" ) != string::npos; } );
    }

    bool AssetReferenceFields::isChecked( string_view path ) const
    {
        return AssetReferenceFieldsInternal::startsWithDomain( path, _listUncheckedDomain ) == false;
    }

    AssetReferenceSyntax AssetReferenceFields::syntaxOf( string_view path )
    {
        if ( FileUtil::hasExtension( path, ".json" ) )
            return AssetReferenceSyntax::JSON;
        const bool bCMake = FileUtil::hasExtension( path, ".cmake" ) || StringUtil::endsWith( FileUtil::normalizeSeparators( path ), "/CMakeLists.txt" ) || path == "CMakeLists.txt";
        return bCMake ? AssetReferenceSyntax::CMake : AssetReferenceSyntax::XML;
    }

    void AssetReferenceFields::appendTokens( string_view text, size_t valueStart, size_t valueEnd, string_view field, const vector<size_t>& listLineStart,
                                             vector<AssetReference>& outListReference ) const
    {
        size_t cursor = valueStart;
        while ( cursor < valueEnd )
        {
            while ( cursor < valueEnd && AssetReferenceFieldsInternal::isOneOf( text[cursor], AssetReferenceFieldsInternal::kTokenSeparators ) )
            {
                ++cursor;
            }
            const size_t tokenStart = cursor;
            while ( cursor < valueEnd && AssetReferenceFieldsInternal::isOneOf( text[cursor], AssetReferenceFieldsInternal::kTokenSeparators ) == false )
            {
                ++cursor;
            }
            const string_view token = text.substr( tokenStart, cursor - tokenStart );
            const bool        bTemplate =
                std::any_of( token.begin(), token.end(), []( utf8 character )
            { return AssetReferenceFieldsInternal::isOneOf( character, AssetReferenceFieldsInternal::kTemplateCharacters ); } );
            if ( token.empty() || bTemplate )
                continue;
            string normalized = AssetReferenceFieldsInternal::normalizePath( token );
            if ( AssetReferenceFieldsInternal::startsWithDomain( normalized, _listDomain ) == false )
                continue;
            AssetReference reference;
            reference._line   = AssetReferenceFieldsInternal::lineOf( listLineStart, tokenStart );
            reference._field  = string( field );
            reference._offset = tokenStart;
            reference._length = token.size();
            reference._path   = std::move( normalized );
            outListReference.push_back( std::move( reference ) );
        }
    }

    void AssetReferenceFields::extractXML( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const
    {
        vector<string> listOpenField; // 열린 경로 요소(안의 글이 값이다). `<item>` 은 부모 칸 이름으로 쌓는다
        size_t         cursor = 0;
        while ( cursor < text.size() )
        {
            const size_t tagStart = text.find( '<', cursor );
            const size_t textEnd  = tagStart == string_view::npos ? text.size() : tagStart;
            if ( listOpenField.empty() == false && textEnd > cursor )
                appendTokens( text, cursor, textEnd, listOpenField.back(), listLineStart, outListReference );
            if ( tagStart == string_view::npos )
                break;
            const string_view rest = text.substr( tagStart );
            if ( StringUtil::startsWith( rest, "<!--" ) )
            {
                const size_t commentEnd = text.find( "-->", tagStart + 4 );
                cursor                  = commentEnd == string_view::npos ? text.size() : commentEnd + 3;
                continue;
            }
            if ( StringUtil::startsWith( rest, "<?" ) || StringUtil::startsWith( rest, "<!" ) )
            {
                const size_t declarationEnd = text.find( '>', tagStart );
                cursor                      = declarationEnd == string_view::npos ? text.size() : declarationEnd + 1;
                continue;
            }
            const size_t tagEnd   = AssetReferenceFieldsInternal::findTagEnd( text, tagStart );
            const bool   bClosing = tagStart + 1 < text.size() && text[tagStart + 1] == '/';
            size_t       position = tagStart + ( bClosing ? 2 : 1 );
            while ( position < tagEnd && AssetReferenceFieldsInternal::isSpace( text[position] ) )
            {
                ++position;
            }
            const size_t nameStart = position;
            if ( position >= tagEnd || AssetReferenceFieldsInternal::isNameStart( text[position] ) == false )
            {
                cursor = tagStart + 1;
                continue;
            }
            while ( position < tagEnd && AssetReferenceFieldsInternal::isNameChar( text[position] ) )
            {
                ++position;
            }
            const string_view name = text.substr( nameStart, position - nameStart );
            if ( bClosing )
            {
                if ( listOpenField.empty() == false && listOpenField.back() == name )
                    listOpenField.pop_back();
                cursor = tagEnd;
                continue;
            }

            // 속성: `이름 = "값"` · `이름 = '값'`. 모양이 아닌 글자는 하나씩 건너뛴다.
            while ( position < tagEnd )
            {
                if ( AssetReferenceFieldsInternal::isNameStart( text[position] ) == false )
                {
                    ++position;
                    continue;
                }
                const size_t attributeStart = position;
                while ( position < tagEnd && AssetReferenceFieldsInternal::isNameChar( text[position] ) )
                {
                    ++position;
                }
                const string_view attribute = text.substr( attributeStart, position - attributeStart );
                size_t            equals    = position;
                while ( equals < tagEnd && AssetReferenceFieldsInternal::isSpace( text[equals] ) )
                {
                    ++equals;
                }
                if ( equals >= tagEnd || text[equals] != '=' )
                    continue;
                size_t quote = equals + 1;
                while ( quote < tagEnd && AssetReferenceFieldsInternal::isSpace( text[quote] ) )
                {
                    ++quote;
                }
                if ( quote >= tagEnd || ( text[quote] != '"' && text[quote] != '\'' ) )
                {
                    position = quote;
                    continue;
                }
                const size_t valueEnd = text.find( text[quote], quote + 1 );
                if ( valueEnd == string_view::npos || valueEnd >= tagEnd )
                    break;
                if ( isField( attribute ) )
                    appendTokens( text, quote + 1, valueEnd, attribute, listLineStart, outListReference );
                position = valueEnd + 1;
            }

            size_t last = tagEnd - 1; // `>` 자리
            while ( last > tagStart && ( text[last] == '>' || AssetReferenceFieldsInternal::isSpace( text[last] ) ) )
            {
                --last;
            }
            const bool bSelfClosing = text[last] == '/';
            if ( bSelfClosing == false )
            {
                if ( isField( name ) )
                    listOpenField.emplace_back( name );
                else if ( listOpenField.empty() == false && name == AssetReferenceFieldsInternal::kItemElement )
                    listOpenField.push_back( listOpenField.back() );
            }
            cursor = tagEnd;
        }
    }

    void AssetReferenceFields::extractJSON( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const
    {
        size_t cursor = 0;
        while ( ( cursor = text.find( '"', cursor ) ) != string_view::npos )
        {
            // 키 `"이름"` 뒤에 `:` 가 오는지 본다. 아니면 다음 따옴표부터 다시(정규식 finditer 와 같은 뜻).
            const size_t keyStart = cursor + 1;
            size_t       keyEnd   = keyStart;
            if ( keyEnd >= text.size() || AssetReferenceFieldsInternal::isNameStart( text[keyEnd] ) == false )
            {
                ++cursor;
                continue;
            }
            while ( keyEnd < text.size() && ( AssetReferenceFieldsInternal::isNameChar( text[keyEnd] ) && text[keyEnd] != ':' ) )
            {
                ++keyEnd;
            }
            size_t colon = keyEnd + 1;
            while ( colon < text.size() && AssetReferenceFieldsInternal::isSpace( text[colon] ) )
            {
                ++colon;
            }
            if ( keyEnd >= text.size() || text[keyEnd] != '"' || colon >= text.size() || text[colon] != ':' )
            {
                ++cursor;
                continue;
            }
            size_t valueStart = colon + 1;
            while ( valueStart < text.size() && AssetReferenceFieldsInternal::isSpace( text[valueStart] ) )
            {
                ++valueStart;
            }
            cursor                = valueStart;
            const string_view key = text.substr( keyStart, keyEnd - keyStart );
            if ( valueStart >= text.size() || isField( key ) == false )
                continue;
            if ( text[valueStart] == '"' )
            {
                const size_t valueEnd = AssetReferenceFieldsInternal::findStringEnd( text, valueStart + 1 );
                if ( valueEnd != string_view::npos )
                    appendTokens( text, valueStart + 1, valueEnd, key, listLineStart, outListReference );
            }
            else if ( text[valueStart] == '[' )
            {
                const size_t arrayEnd = std::min( text.find( ']', valueStart ), text.size() );
                size_t       element  = valueStart;
                while ( ( element = text.find( '"', element ) ) != string_view::npos && element < arrayEnd )
                {
                    const size_t elementEnd = AssetReferenceFieldsInternal::findStringEnd( text, element + 1 );
                    if ( elementEnd == string_view::npos || elementEnd > arrayEnd )
                        break;
                    appendTokens( text, element + 1, elementEnd, key, listLineStart, outListReference );
                    element = elementEnd + 1;
                }
            }
        }
    }

    void AssetReferenceFields::extractCMake( string_view text, const vector<size_t>& listLineStart, vector<AssetReference>& outListReference ) const
    {
        for ( const string& argument : _listCMakeArgument )
        {
            const string prefix = argument + "=";
            size_t       start  = text.find( prefix );
            while ( start != string_view::npos )
            {
                const bool   bBoundary  = start == 0 || AssetReferenceFieldsInternal::isOneOf( text[start - 1], AssetReferenceFieldsInternal::kCMakeArgumentBoundary );
                const size_t valueStart = start + prefix.size();
                size_t       valueEnd   = valueStart;
                while ( valueEnd < text.size() && AssetReferenceFieldsInternal::isOneOf( text[valueEnd], AssetReferenceFieldsInternal::kCMakeValueEnd ) == false )
                {
                    ++valueEnd;
                }
                if ( bBoundary )
                    appendTokens( text, valueStart, valueEnd, argument, listLineStart, outListReference );
                start = text.find( prefix, valueEnd );
            }
        }
    }

    void AssetReferenceFields::extract( string_view text, AssetReferenceSyntax syntax, vector<AssetReference>& outListReference ) const
    {
        const vector<size_t> listLineStart = AssetReferenceFieldsInternal::makeLineStarts( text );
        switch ( syntax )
        {
            case AssetReferenceSyntax::XML:
                extractXML( text, listLineStart, outListReference );
                return;
            case AssetReferenceSyntax::JSON:
                extractJSON( text, listLineStart, outListReference );
                return;
            case AssetReferenceSyntax::CMake:
                extractCMake( text, listLineStart, outListReference );
                return;
        }
    }

    bool AssetReferenceFields::isImportedFromSource( string_view assetID, const AssetIDSet& assets ) const
    {
        for ( const AssetImportedFolder& imported : _listImported )
        {
            const string marker   = "/" + imported._folder + "/";
            size_t       position = assetID.find( marker );
            while ( position != string_view::npos )
            {
                const string              base        = string( assetID.substr( 0, position ) ) + "/" + imported._sourceFolder + "/";
                const vector<string_view> listSegment = StringUtil::split( assetID.substr( position + marker.size() ), "/" );
                // 원본은 산출물 자리의 어느 조상이다 — `models/a/b/c.mesh` 는 `models_raw/a/b/c.glb` → `a/b.glb` → `a.glb` 순서로 본다(옆 폴더 `a/clips` 도 `a.glb` 의 것).
                for ( size_t count = listSegment.size(); count > 0; --count )
                {
                    string head;
                    for ( size_t segment = 0; segment + 1 < count; ++segment )
                    {
                        head.append( listSegment[segment] ).append( "/" );
                    }
                    const string last( listSegment[count - 1] );
                    const string arrStem[] = { last.substr( 0, last.find( '.' ) ), FileUtil::removeExtension( last ) };
                    for ( const string& stem : arrStem )
                    {
                        for ( const string& extension : imported._listSourceExtension )
                        {
                            if ( assets.contains( base + head + stem + extension ) )
                                return true;
                        }
                    }
                }
                position = assetID.find( marker, position + 1 );
            }
        }
        return false;
    }

    bool AssetReferenceFields::canResolve( string_view assetID, const AssetIDSet& assets ) const
    {
        return assets.contains( assetID ) || isImportedFromSource( assetID, assets );
    }

    vector<AssetReferenceRewriteRule> AssetReferenceFields::makeRewriteRules( string_view oldID, string_view newID, bool bFolder ) const
    {
        const string                      oldPath = AssetReferenceFieldsInternal::normalizePath( oldID );
        const string                      newPath = AssetReferenceFieldsInternal::normalizePath( newID );
        vector<AssetReferenceRewriteRule> listRule{
            AssetReferenceRewriteRule{ oldPath, newPath, bFolder }
        };
        for ( const AssetImportedFolder& imported : _listImported )
        {
            const string marker      = "/" + imported._sourceFolder + "/";
            string       oldImported = "/" + oldPath;
            string       newImported = "/" + newPath;
            const size_t oldAt       = oldImported.find( marker );
            const size_t newAt       = newImported.find( marker );
            if ( oldAt == string::npos || newAt == string::npos )
                continue;
            oldImported.replace( oldAt, marker.size(), "/" + imported._folder + "/" );
            newImported.replace( newAt, marker.size(), "/" + imported._folder + "/" );
            if ( bFolder == false )
            {
                const auto hasSourceExtension = [&imported]( const string& path )
                {
                    return std::any_of( imported._listSourceExtension.begin(), imported._listSourceExtension.end(), [&path]( const string& extension )
                    { return StringUtil::endsWith( path, extension ); } );
                };
                if ( hasSourceExtension( oldImported ) == false || hasSourceExtension( newImported ) == false )
                    continue;
                oldImported = FileUtil::removeExtension( oldImported );
                newImported = FileUtil::removeExtension( newImported );
            }
            // 산출물은 `<이름>.mesh` · `<이름>/…` 라 접두로 바꾼다.
            listRule.push_back( AssetReferenceRewriteRule{ oldImported.substr( 1 ), newImported.substr( 1 ), true } );
        }
        return listRule;
    }

    uint32 AssetReferenceFields::rewrite( string& inoutText, AssetReferenceSyntax syntax, const vector<AssetReferenceRewriteRule>& listRule ) const
    {
        vector<AssetReference> listReference;
        extract( inoutText, syntax, listReference );
        // 뒤에서부터 바꿔 앞 참조의 자리가 밀리지 않게 한다. CMake 는 인자마다 따로 뽑아 글 순서가 아니므로 자리로 정렬한다.
        std::sort( listReference.begin(), listReference.end(), []( const AssetReference& left, const AssetReference& right )
        { return left._offset < right._offset; } );
        uint32 rewriteCount{ 0 };
        for ( auto itReference = listReference.rbegin(); itReference != listReference.rend(); ++itReference )
        {
            const string& path = itReference->_path;
            for ( const AssetReferenceRewriteRule& rule : listRule )
            {
                const bool bExact  = path == rule._oldPath;
                const bool bPrefix = rule._bPrefix && path.size() > rule._oldPath.size() && StringUtil::startsWith( path, rule._oldPath ) &&
                                     ( path[rule._oldPath.size()] == '/' || path[rule._oldPath.size()] == '.' );
                if ( bExact == false && bPrefix == false )
                    continue;
                const string rewritten = rule._newPath + path.substr( rule._oldPath.size() );
                inoutText.replace( itReference->_offset, itReference->_length, rewritten );
                ++rewriteCount;
                break;
            }
        }
        return rewriteCount;
    }
} // namespace sw
