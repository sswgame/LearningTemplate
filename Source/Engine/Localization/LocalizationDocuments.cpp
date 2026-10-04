#include "pch.h"

#include "Engine/Localization/LocalizationDocuments.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        struct LocalizationDocumentsInternal
        {
            /** @brief @p value 객체의 칸이 모두 @p arrKnown 안에 있는지 봅니다. 모르는 칸이면 이름을 적고 false 입니다. */
            template <size_t N>
            static bool hasOnlyKnownFields( const JsonValue& value, const utf8* const ( &arrKnown )[N], string& outUnknown )
            {
                for ( const string& fieldName : value.getMemberNames() )
                {
                    bool bKnown{ false };
                    for ( const utf8* pKnown : arrKnown )
                        bKnown = bKnown || fieldName == pKnown;
                    if ( bKnown == false )
                    {
                        outUnknown = fieldName;
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] static bool readStringList( const JsonValue& value, vector<string>& outList )
            {
                outList.clear();
                if ( value.isValid() == false )
                    return true;
                if ( value.isArray() == false )
                    return false;
                for ( size_t index = 0; index < value.size(); ++index )
                {
                    const JsonValue element = value.at( index );
                    if ( element.isString() == false )
                        return false;
                    outList.push_back( element.asString() );
                }
                return true;
            }

            static void writeStringList( const JsonValue& parent, const utf8* pName, const vector<string>& listValue )
            {
                const JsonValue arrayValue = parent.set( pName, false );
                arrayValue.setArray();
                for ( const string& value : listValue )
                    arrayValue.pushBack().setString( value );
            }

            /** @brief 문서 머리(`culture` · `entries`)를 읽습니다. */
            [[nodiscard]] static bool readHeader( JsonDocument& document, string_view jsonText, string_view sourceName, string& outCulture, JsonValue& outEntries, string& outError )
            {
                if ( document.parse( FileUtil::skipUtf8Bom( jsonText ), sourceName ) == false )
                {
                    outError = document.getLastError();
                    return false;
                }
                const JsonValue root = document.getRoot();
                if ( root.isObject() == false )
                {
                    outError = string( sourceName ) + ": root must be an object";
                    return false;
                }
                static constexpr const utf8* kArrRootField[] = { "culture", "entries" };
                string                       unknown;
                if ( hasOnlyKnownFields( root, kArrRootField, unknown ) == false )
                {
                    outError = string( sourceName ) + ": unknown field '" + unknown + "'";
                    return false;
                }
                outCulture = CultureTable::normalizeCode( root.get( "culture", false ).asString() );
                outEntries = root.get( "entries", false );
                if ( outCulture.empty() || ( outEntries.isValid() && outEntries.isObject() == false ) )
                {
                    outError = string( sourceName ) + ": needs \"culture\" and an \"entries\" object";
                    return false;
                }
                return true;
            }

            [[nodiscard]] static bool readTextFile( string_view absolutePath, string& outText, string* pOutError )
            {
                if ( FileUtil::readTextFile( absolutePath, outText ) )
                    return true;
                if ( pOutError != nullptr )
                    *pOutError = "cannot read '" + string( absolutePath ) + "'";
                return false;
            }

            [[nodiscard]] static bool writeTextFile( string_view absolutePath, const string& text )
            {
                // 내용이 같으면 쓰지 않는다 — 파일 시간이 바뀌면 핫 리로드 · 빌드 스탬프가 헛돈다.
                string existing;
                if ( FileUtil::readTextFile( absolutePath, existing ) && existing == text )
                    return true;
                return FileUtil::ensureParentDirectoryExists( absolutePath ) && FileUtil::writeTextFile( absolutePath, text );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint64 LocalizationTextUtil::computeSourceHash( string_view sourceText )
    {
        const uint64 hash = StringUtil::computeHash64( sourceText, false );
        return hash == 0 ? 1u : hash;
    }

    string LocalizationTextUtil::formatSourceHash( uint64 sourceHash )
    {
        utf8         arrBuffer[constant::kMaxBuffer32]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, sourceHash, 16 );
        string       text( arrBuffer, length );
        while ( text.size() < 16 )
            text.insert( text.begin(), '0' );
        return text;
    }

    bool LocalizationTextUtil::tryParseSourceHash( string_view text, uint64& outSourceHash )
    {
        return text.size() == 16 && StringUtil::parseUint64( text, outSourceHash, 16 );
    }

    string LocalizationTextUtil::makeFullKey( string_view textNamespace, string_view key )
    {
        if ( textNamespace.empty() )
            return string( key );
        string fullKey( textNamespace );
        fullKey.push_back( '.' );
        fullKey.append( key );
        return fullKey;
    }

    // ------------------------------------------------------------------------------
    // SourceStringTable
    // ------------------------------------------------------------------------------
    bool SourceStringTable::loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError )
    {
        string       error;
        JsonDocument document;
        string       culture;
        JsonValue    entries;
        if ( LocalizationDocumentsInternal::readHeader( document, jsonText, sourceName, culture, entries, error ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        static constexpr const utf8* kArrEntryField[] = { "source", "context", "comment", "maxLength", "origins" };
        map<string, SourceTextEntry> mapEntry;
        for ( const string& key : entries.getMemberNames() )
        {
            const JsonValue value = entries.get( key, false );
            string          unknown;
            if ( value.isObject() == false || LocalizationDocumentsInternal::hasOnlyKnownFields( value, kArrEntryField, unknown ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' " + ( unknown.empty() ? "must be an object" : "has unknown field '" + unknown + "'" );
                break;
            }
            const JsonValue source = value.get( "source", false );
            if ( source.isString() == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' needs a \"source\" string";
                break;
            }
            SourceTextEntry entry;
            entry._source    = source.asString();
            entry._context   = value.get( "context", false ).asString();
            entry._comment   = value.get( "comment", false ).asString();
            entry._maxLength = static_cast<uint32>( value.get( "maxLength", false ).asUint( 0 ) );
            if ( LocalizationDocumentsInternal::readStringList( value.get( "origins", false ), entry._listOrigin ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' origins must be strings";
                break;
            }
            mapEntry[key] = std::move( entry );
        }
        if ( error.empty() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        _culture  = culture;
        _mapEntry = std::move( mapEntry );
        return true;
    }

    bool SourceStringTable::loadFromFile( string_view absolutePath, string* pOutError )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromJsonText( text, absolutePath, pOutError );
    }

    string SourceStringTable::toJsonText() const
    {
        JsonDocument    document;
        const JsonValue root = document.makeObject();
        root.set( "culture", false ).setString( _culture );
        const JsonValue entries = root.set( "entries", false );
        entries.setObject();
        for ( const auto& [key, entry] : _mapEntry )
        {
            const JsonValue value = entries.set( key, false );
            value.setObject();
            value.set( "source", false ).setString( entry._source );
            if ( entry._context.empty() == false )
                value.set( "context", false ).setString( entry._context );
            if ( entry._comment.empty() == false )
                value.set( "comment", false ).setString( entry._comment );
            if ( entry._maxLength != 0 )
                value.set( "maxLength", false ).setUint( entry._maxLength );
            if ( entry._listOrigin.empty() == false )
                LocalizationDocumentsInternal::writeStringList( value, "origins", entry._listOrigin );
        }
        return document.dump( 4 ) + "\n";
    }

    bool SourceStringTable::saveToFile( string_view absolutePath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absolutePath, toJsonText() );
    }

    const SourceTextEntry* SourceStringTable::findEntry( string_view key ) const
    {
        const auto it = _mapEntry.find( string( key ) );
        return it != _mapEntry.end() ? &it->second : nullptr;
    }

    SourceTextEntry& SourceStringTable::getOrAddEntry( string_view key )
    {
        return _mapEntry[string( key )];
    }

    bool SourceStringTable::removeEntry( string_view key )
    {
        return _mapEntry.erase( string( key ) ) > 0;
    }

    void SourceStringTable::setCulture( string_view culture )
    {
        _culture = CultureTable::normalizeCode( culture );
    }

    // ------------------------------------------------------------------------------
    // TranslationTable
    // ------------------------------------------------------------------------------
    bool TranslationTable::loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError )
    {
        string       error;
        JsonDocument document;
        string       culture;
        JsonValue    entries;
        if ( LocalizationDocumentsInternal::readHeader( document, jsonText, sourceName, culture, entries, error ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        static constexpr const utf8*  kArrEntryField[] = { "text", "sourceHash", "review", "translatorComment" };
        map<string, TranslationEntry> mapEntry;
        for ( const string& key : entries.getMemberNames() )
        {
            const JsonValue value = entries.get( key, false );
            string          unknown;
            if ( value.isObject() == false || LocalizationDocumentsInternal::hasOnlyKnownFields( value, kArrEntryField, unknown ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' " + ( unknown.empty() ? "must be an object" : "has unknown field '" + unknown + "'" );
                break;
            }
            const JsonValue text = value.get( "text", false );
            if ( text.isString() == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' needs a \"text\" string";
                break;
            }
            TranslationEntry entry;
            entry._text               = text.asString();
            entry._translatorComment  = value.get( "translatorComment", false ).asString();
            entry._bReview            = value.get( "review", false ).asBool( false );
            const JsonValue hashValue = value.get( "sourceHash", false );
            if ( hashValue.isValid() && LocalizationTextUtil::tryParseSourceHash( hashValue.asString(), entry._sourceHash ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' sourceHash must be 16 hex digits";
                break;
            }
            mapEntry[key] = std::move( entry );
        }
        if ( error.empty() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        _culture  = culture;
        _mapEntry = std::move( mapEntry );
        return true;
    }

    bool TranslationTable::loadFromFile( string_view absolutePath, string* pOutError )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromJsonText( text, absolutePath, pOutError );
    }

    string TranslationTable::toJsonText() const
    {
        JsonDocument    document;
        const JsonValue root = document.makeObject();
        root.set( "culture", false ).setString( _culture );
        const JsonValue entries = root.set( "entries", false );
        entries.setObject();
        for ( const auto& [key, entry] : _mapEntry )
        {
            const JsonValue value = entries.set( key, false );
            value.setObject();
            value.set( "text", false ).setString( entry._text );
            if ( entry._sourceHash != 0 )
                value.set( "sourceHash", false ).setString( LocalizationTextUtil::formatSourceHash( entry._sourceHash ) );
            if ( entry._bReview )
                value.set( "review", false ).setBool( true );
            if ( entry._translatorComment.empty() == false )
                value.set( "translatorComment", false ).setString( entry._translatorComment );
        }
        return document.dump( 4 ) + "\n";
    }

    bool TranslationTable::saveToFile( string_view absolutePath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absolutePath, toJsonText() );
    }

    const TranslationEntry* TranslationTable::findEntry( string_view key ) const
    {
        const auto it = _mapEntry.find( string( key ) );
        return it != _mapEntry.end() ? &it->second : nullptr;
    }

    TranslationEntry& TranslationTable::getOrAddEntry( string_view key )
    {
        return _mapEntry[string( key )];
    }

    bool TranslationTable::removeEntry( string_view key )
    {
        return _mapEntry.erase( string( key ) ) > 0;
    }

    TranslationState TranslationTable::computeState( string_view key, const SourceTextEntry* pSource ) const
    {
        const TranslationEntry* pEntry = findEntry( key );
        if ( pEntry == nullptr )
            return TranslationState::Missing;
        if ( pSource == nullptr )
            return TranslationState::Orphan;
        if ( pEntry->_bReview )
            return TranslationState::Review;
        const bool bStale = pEntry->_sourceHash != 0 && pEntry->_sourceHash != LocalizationTextUtil::computeSourceHash( pSource->_source );
        return bStale ? TranslationState::Stale : TranslationState::Current;
    }

    void TranslationTable::setCulture( string_view culture )
    {
        _culture = CultureTable::normalizeCode( culture );
    }

    // ------------------------------------------------------------------------------
    // LocalizationProject
    // ------------------------------------------------------------------------------
    bool LocalizationProject::loadFromJsonText( string_view jsonText, string_view sourceName, string* pOutError )
    {
        JsonDocument document;
        string       error;
        if ( document.parse( FileUtil::skipUtf8Bom( jsonText ), sourceName ) == false )
            error = document.getLastError();
        const JsonValue              root            = document.getRoot();
        static constexpr const utf8* kArrRootField[] = { "name", "sourceCulture", "cultures", "stringTables", "codeRoots", "assetRoots" };
        string                       unknown;
        if ( error.empty() && ( root.isObject() == false || LocalizationDocumentsInternal::hasOnlyKnownFields( root, kArrRootField, unknown ) == false ) )
            error = string( sourceName ) + ": " + ( unknown.empty() ? "root must be an object" : "unknown field '" + unknown + "'" );

        LocalizationProject loaded;
        if ( error.empty() )
        {
            loaded._name          = root.get( "name", false ).asString();
            loaded._sourceCulture = CultureTable::normalizeCode( root.get( "sourceCulture", false ).asString() );
            const bool bListsRead = LocalizationDocumentsInternal::readStringList( root.get( "cultures", false ), loaded._listCulture ) &&
                                    LocalizationDocumentsInternal::readStringList( root.get( "stringTables", false ), loaded._listStringTable ) &&
                                    LocalizationDocumentsInternal::readStringList( root.get( "codeRoots", false ), loaded._listCodeRoot ) &&
                                    LocalizationDocumentsInternal::readStringList( root.get( "assetRoots", false ), loaded._listAssetRoot );
            const bool bMissingField = loaded._name.empty() || loaded._sourceCulture.empty() || loaded._listStringTable.empty() || bListsRead == false;
            if ( error.empty() && bMissingField )
                error = string( sourceName ) + ": needs \"name\", \"sourceCulture\", \"stringTables\" and string lists";
            for ( string& culture : loaded._listCulture )
                culture = CultureTable::normalizeCode( culture );
        }
        if ( error.empty() == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        *this = std::move( loaded );
        return true;
    }

    bool LocalizationProject::loadFromFile( string_view absolutePath, string* pOutError )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromJsonText( text, absolutePath, pOutError );
    }

    string LocalizationProject::makeSiblingPath( string_view projectPath, string_view fileName )
    {
        return FileUtil::joinPath( FileUtil::getDirectoryPart( projectPath ), fileName );
    }

    string LocalizationProject::makeTranslationPath( string_view projectPath, string_view culture )
    {
        return makeSiblingPath( projectPath, CultureTable::normalizeCode( culture ) + TranslationTable::kFileSuffix );
    }
} // namespace sw
