#include "pch.h"

#include "Engine/Localization/LocalizationDocuments.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct LocalizationDocumentsInternal
        {
            /** @brief 목록 원소 요소의 이름입니다. */
            static constexpr string_view kItemTag = "item";

            /** @brief 문서 머리의 속성(`culture`)과 자식(`entries`) 이름입니다. */
            static constexpr string_view kEntriesTag = "entries";
            static constexpr string_view kEntryTag   = "entry";

            /** @brief 요소의 속성 · 자식 이름이 모두 알려진 것인지 봅니다. 알려지지 않은 첫 이름을 @p outUnknown 에 적고 false 를 반환합니다. */
            static bool hasOnlyKnownNames( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string& outUnknown )
            {
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( VectorUtil::contains( listAttribute, string_view{ attribute.getName() } ) == false )
                    {
                        outUnknown = attribute.getName();
                        return false;
                    }
                }
                for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) == false )
                    {
                        outUnknown = child.getName();
                        return false;
                    }
                }
                return true;
            }

            /** @brief 목록 요소(`<origins><item>…</item></origins>`)를 읽습니다. 요소가 없으면 빈 목록입니다. 원소가 `<item>` 이 아니면 false 입니다. */
            [[nodiscard]] static bool readStringList( const XMLNode& listNode, vector<string>& outList )
            {
                outList.clear();
                if ( listNode.isValid() == false )
                    return true;
                for ( XMLNode element = listNode.findChild(); element.isValid(); element = element.findNextSibling() )
                {
                    if ( string_view{ element.getName() } != kItemTag )
                        return false;
                    outList.push_back( string{ element.getText() } );
                }
                return true;
            }

            static void writeStringList( const XMLNode& parent, const utf8* pName, const vector<string>& listValue )
            {
                const XMLNode listNode = parent.appendChild( pName );
                for ( const string& value : listValue )
                {
                    (void)listNode.appendChild( "item", string_view{ value } );
                }
            }

            /** @brief 문서 머리(루트의 `culture` 속성, `entries` 자식)를 읽습니다. */
            [[nodiscard]] static bool readHeader( XMLDocument& document, string_view xmlText, string_view sourceName, const utf8* pRootName, string& outCulture, XMLNode& outEntries,
                                                  string& outError )
            {
                XMLNode root;
                if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, pRootName, root ) != XMLRootStatus::Found )
                {
                    outError = document.getLastError();
                    if ( outError.empty() )
                        outError = string( sourceName ) + ": root must be <" + pRootName + ">";
                    return false;
                }
                string unknown;
                if ( hasOnlyKnownNames( root, { "culture" }, { kEntriesTag }, unknown ) == false )
                {
                    outError = string( sourceName ) + ": unknown field '" + unknown + "'";
                    return false;
                }
                outCulture = CultureTable::normalizeCode( root.getAttributeText( "culture" ) );
                outEntries = root.findChild( "entries" );
                if ( outCulture.empty() )
                {
                    outError = string( sourceName ) + ": needs a \"culture\" attribute";
                    return false;
                }
                return true;
            }

            /** @brief 항목 요소 하나의 이름 · 키를 확인합니다. 키는 비지 않아야 한다. 틀리면 오류 글을 채우고 false 입니다. */
            [[nodiscard]] static bool readEntryKey( const XMLNode& entryNode, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild,
                                                    string_view sourceName, string& outKey, string& outError )
            {
                string unknown;
                if ( string_view{ entryNode.getName() } != kEntryTag )
                {
                    outError = string( sourceName ) + ": <entries> holds only <entry> elements";
                    return false;
                }
                if ( hasOnlyKnownNames( entryNode, listAttribute, listChild, unknown ) == false )
                {
                    outError = string( sourceName ) + ": entry has unknown field '" + unknown + "'";
                    return false;
                }
                outKey = string{ entryNode.getAttributeText( "key" ) };
                if ( outKey.empty() )
                {
                    outError = string( sourceName ) + ": entry needs a \"key\" attribute";
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
                // 내용이 같으면 쓰지 않는다. 파일 시간이 바뀌면 핫 리로드와 빌드 스탬프가 헛돈다.
                string     existing;
                const bool bContentUnchanged = FileUtil::readOptionalTextFile( absolutePath, existing ) == OptionalFileRead::Read && existing == text;
                if ( bContentUnchanged )
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
        const uint64 hash = HashUtil::computeStringHash64( sourceText, false );
        return hash == 0 ? 1u : hash;
    }

    string LocalizationTextUtil::formatSourceHash( uint64 sourceHash )
    {
        utf8         arrBuffer[constant::kMaxBuffer32]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::kMaxBuffer32, sourceHash, 16 );
        string       text( arrBuffer, length );
        while ( text.size() < 16 )
        {
            text.insert( text.begin(), '0' );
        }
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
    bool SourceStringTable::loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError )
    {
        using Internal = LocalizationDocumentsInternal;
        string      error;
        XMLDocument document;
        string      culture;
        XMLNode     entries;
        if ( Internal::readHeader( document, xmlText, sourceName, "SourceStringTable", culture, entries, error ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        map<string, SourceTextEntry> mapEntry;
        for ( XMLNode entryNode = entries.findChild(); entryNode.isValid(); entryNode = entryNode.findNextSibling() )
        {
            string key;
            if ( Internal::readEntryKey( entryNode, { "key", "source", "context", "comment", "maxLength" }, { "origins" }, sourceName, key, error ) == false )
                break;
            if ( mapEntry.find( key ) != mapEntry.end() )
            {
                error = string( sourceName ) + ": entry '" + key + "' is written twice";
                break;
            }
            if ( entryNode.findAttribute( "source" ) == nullptr )
            {
                error = string( sourceName ) + ": entry '" + key + "' needs a \"source\" attribute";
                break;
            }
            SourceTextEntry entry;
            entry._source  = string{ entryNode.getAttributeText( "source" ) };
            entry._context = string{ entryNode.getAttributeText( "context" ) };
            entry._comment = string{ entryNode.getAttributeText( "comment" ) };
            // 최대 길이는 없으면 항목의 초기값(제한 없음 0)이다.
            std::optional<uint32> maxLength;
            if ( entryNode.readOptionalAttribute( "maxLength", maxLength ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' maxLength must be a non-negative integer";
                break;
            }
            if ( maxLength.has_value() )
                entry._maxLength = *maxLength;
            if ( Internal::readStringList( entryNode.findChild( "origins" ), entry._listOrigin ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' origins must be <item> elements";
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
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromXMLText( text, absolutePath, pOutError );
    }

    string SourceStringTable::toXMLText() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "SourceStringTable" );
        root.appendAttribute( "culture", string_view{ _culture } );
        const XMLNode entries = root.appendChild( "entries" );
        for ( const auto& [key, entry] : _mapEntry )
        {
            const XMLNode value = entries.appendChild( "entry" );
            value.appendAttribute( "key", string_view{ key } );
            value.appendAttribute( "source", string_view{ entry._source } );
            if ( entry._context.empty() == false )
                value.appendAttribute( "context", string_view{ entry._context } );
            if ( entry._comment.empty() == false )
                value.appendAttribute( "comment", string_view{ entry._comment } );
            if ( entry._maxLength != 0 )
                value.appendAttribute( "maxLength", entry._maxLength );
            if ( entry._listOrigin.empty() == false )
                LocalizationDocumentsInternal::writeStringList( value, "origins", entry._listOrigin );
        }
        return document.saveToString();
    }

    bool SourceStringTable::saveToFile( string_view absolutePath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absolutePath, toXMLText() );
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
    bool TranslationTable::loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError )
    {
        using Internal = LocalizationDocumentsInternal;
        string      error;
        XMLDocument document;
        string      culture;
        XMLNode     entries;
        if ( Internal::readHeader( document, xmlText, sourceName, "TranslationTable", culture, entries, error ) == false )
        {
            if ( pOutError != nullptr )
                *pOutError = error;
            return false;
        }
        map<string, TranslationEntry> mapEntry;
        for ( XMLNode entryNode = entries.findChild(); entryNode.isValid(); entryNode = entryNode.findNextSibling() )
        {
            string key;
            if ( Internal::readEntryKey( entryNode, { "key", "text", "sourceHash", "review", "translatorComment" }, {}, sourceName, key, error ) == false )
                break;
            if ( mapEntry.find( key ) != mapEntry.end() )
            {
                error = string( sourceName ) + ": entry '" + key + "' is written twice";
                break;
            }
            if ( entryNode.findAttribute( "text" ) == nullptr )
            {
                error = string( sourceName ) + ": entry '" + key + "' needs a \"text\" attribute";
                break;
            }
            TranslationEntry entry;
            entry._text              = string{ entryNode.getAttributeText( "text" ) };
            entry._translatorComment = string{ entryNode.getAttributeText( "translatorComment" ) };
            std::optional<bool> review;
            if ( entryNode.readOptionalAttribute( "review", review ) == false )
            {
                error = string( sourceName ) + ": entry '" + key + "' review must be true or false";
                break;
            }
            entry._bReview = review.value_or( false );
            if ( entryNode.findAttribute( "sourceHash" ) != nullptr && LocalizationTextUtil::tryParseSourceHash( entryNode.getAttributeText( "sourceHash" ), entry._sourceHash ) == false )
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
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromXMLText( text, absolutePath, pOutError );
    }

    string TranslationTable::toXMLText() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "TranslationTable" );
        root.appendAttribute( "culture", string_view{ _culture } );
        const XMLNode entries = root.appendChild( "entries" );
        for ( const auto& [key, entry] : _mapEntry )
        {
            const XMLNode value = entries.appendChild( "entry" );
            value.appendAttribute( "key", string_view{ key } );
            value.appendAttribute( "text", string_view{ entry._text } );
            if ( entry._sourceHash != 0 )
                value.appendAttribute( "sourceHash", string_view{ LocalizationTextUtil::formatSourceHash( entry._sourceHash ) } );
            if ( entry._bReview )
                value.appendAttribute( "review", string_view{ "true" } );
            if ( entry._translatorComment.empty() == false )
                value.appendAttribute( "translatorComment", string_view{ entry._translatorComment } );
        }
        return document.saveToString();
    }

    bool TranslationTable::saveToFile( string_view absolutePath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absolutePath, toXMLText() );
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
    bool LocalizationProject::loadFromXMLText( string_view xmlText, string_view sourceName, string* pOutError )
    {
        using Internal = LocalizationDocumentsInternal;
        XMLDocument document;
        string      error;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, "LocalizationProject", root ) != XMLRootStatus::Found )
        {
            error = document.getLastError();
            if ( error.empty() )
                error = string( sourceName ) + ": root must be <LocalizationProject>";
        }
        string unknown;
        if ( error.empty() &&
             Internal::hasOnlyKnownNames( root, { "name", "sourceCulture" }, { "cultures", "stagedCultures", "stringTables", "codeRoots", "assetRoots", "assetRules" }, unknown ) == false )
            error = string( sourceName ) + ": unknown field '" + unknown + "'";

        LocalizationProject loaded;
        if ( error.empty() )
        {
            loaded._name          = string{ root.getAttributeText( "name" ) };
            loaded._sourceCulture = CultureTable::normalizeCode( root.getAttributeText( "sourceCulture" ) );
            const bool bListsRead = Internal::readStringList( root.findChild( "cultures" ), loaded._listCulture ) &&
                                    Internal::readStringList( root.findChild( "stagedCultures" ), loaded._listStagedCulture ) &&
                                    Internal::readStringList( root.findChild( "stringTables" ), loaded._listStringTable ) &&
                                    Internal::readStringList( root.findChild( "codeRoots" ), loaded._listCodeRoot ) &&
                                    Internal::readStringList( root.findChild( "assetRoots" ), loaded._listAssetRoot );
            size_t ruleIndex{ 0 };
            for ( XMLNode ruleNode = root.findChild( "assetRules" ).findChild(); error.empty() && ruleNode.isValid(); ruleNode = ruleNode.findNextSibling(), ++ruleIndex )
            {
                LocalizationAssetRule rule;
                const bool            bShape = string_view{ ruleNode.getName() } == "rule" && Internal::hasOnlyKnownNames( ruleNode, { "files", "attribute", "kind", "context" }, { "elements" }, unknown ) &&
                                    Internal::readStringList( ruleNode.findChild( "elements" ), rule._listElement );
                rule._fileSuffix = string{ ruleNode.getAttributeText( "files" ) };
                rule._attribute  = string{ ruleNode.getAttributeText( "attribute" ) };
                rule._context    = string{ ruleNode.getAttributeText( "context" ) };
                const string kind{ ruleNode.getAttributeText( "kind" ) };
                rule._bKeyReference          = kind == "key";
                const bool bKnownKind        = kind == "key" || kind == "text";
                const bool bInvalidAssetRule = bShape == false || bKnownKind == false || rule._fileSuffix.empty() || rule._attribute.empty() || rule._listElement.empty();
                if ( bInvalidAssetRule )
                {
                    error = string( sourceName ) + ": assetRules[" + to_string( static_cast<uint64>( ruleIndex ) ) + "] needs files, elements, attribute and kind (text | key)" +
                            ( unknown.empty() ? string{} : " - unknown field '" + unknown + "'" );
                    break;
                }
                loaded._listAssetRule.push_back( std::move( rule ) );
            }
            const bool bMissingField = loaded._name.empty() || loaded._sourceCulture.empty() || loaded._listStringTable.empty() || bListsRead == false;
            if ( error.empty() && bMissingField )
                error = string( sourceName ) + ": needs \"name\", \"sourceCulture\", \"stringTables\" and string lists";
            for ( string& culture : loaded._listCulture )
            {
                culture = CultureTable::normalizeCode( culture );
            }
            for ( string& culture : loaded._listStagedCulture )
            {
                culture            = CultureTable::normalizeCode( culture );
                const bool bListed = VectorUtil::contains( loaded._listCulture, culture );
                if ( error.empty() && bListed == false )
                    error = string( sourceName ) + ": stagedCultures '" + culture + "' is not in cultures";
            }
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
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text, pOutError ) && loadFromXMLText( text, absolutePath, pOutError );
    }

    string LocalizationProject::makeSiblingPath( string_view projectPath, string_view fileName )
    {
        return FileUtil::joinPath( FileUtil::getDirectoryPart( projectPath ), fileName );
    }

    bool LocalizationProject::isStagedCulture( string_view culture ) const
    {
        const string normalized = CultureTable::normalizeCode( culture );
        return VectorUtil::contains( _listStagedCulture, normalized );
    }

    bool LocalizationProject::collectUnstagedTranslationTables( string_view folder, vector<string>& outListAbsPath, string& outError )
    {
        vector<string> listXML;
        (void)FileUtil::collectFiles( folder, ".xml", listXML, true ); // 폴더가 없으면 프로젝트도 없다
        const size_t tableSuffixLength = string_view( TranslationTable::kExtension ).size();
        for ( const string& projectPath : listXML )
        {
            if ( StringUtil::endsWith( projectPath, kExtension, true ) == false )
                continue;
            LocalizationProject project;
            if ( project.loadFromFile( projectPath, &outError ) == false )
                return false;
            const string projectFolder = FileUtil::getDirectoryPart( projectPath );
            for ( const string& tablePath : listXML )
            {
                const bool bTableOfProject = StringUtil::endsWith( tablePath, TranslationTable::kExtension, true ) && FileUtil::getDirectoryPart( tablePath ) == projectFolder;
                if ( bTableOfProject == false )
                    continue;
                const string fileName = FileUtil::getFileNamePart( tablePath );
                if ( project.isStagedCulture( string_view( fileName ).substr( 0, fileName.size() - tableSuffixLength ) ) == false )
                    outListAbsPath.push_back( FileUtil::normalizePath( tablePath ) );
            }
        }
        return true;
    }

    string LocalizationProject::makeTranslationPath( string_view projectPath, string_view culture )
    {
        return makeSiblingPath( projectPath, CultureTable::normalizeCode( culture ) + TranslationTable::kExtension );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( SourceStringTable, SourceStringTable::kAssetType );
    SW_ASSET_TYPE( TranslationTable, TranslationTable::kAssetType );
    SW_ASSET_TYPE( LocalizationProject, LocalizationProject::kAssetType );
} // namespace sw
