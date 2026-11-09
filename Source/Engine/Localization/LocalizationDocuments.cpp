#include "pch.h"

#include "Engine/Localization/LocalizationDocuments.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringUtil.h"

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

            /** @brief 요소의 속성 · 자식 이름 가운데 알려지지 않은 첫 이름입니다(문서가 살아 있는 동안 유효). 모두 알려졌으면 없음입니다. */
            [[nodiscard]] static optional<string_view> findUnknownName( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild )
            {
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                {
                    if ( VectorUtil::contains( listAttribute, string_view{ attribute.getName() } ) == false )
                        return string_view{ attribute.getName() };
                }
                for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) == false )
                        return string_view{ child.getName() };
                }
                return nullopt;
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

            /** @brief 문서 @p sourceName 의 읽기 오류 @p error 의 대상 앞에 원본 이름을 붙입니다(`원본: 대상`). */
            [[nodiscard]] static Error atSource( Error error, string_view sourceName )
            {
                error.prependDetail( ": " ).prependDetail( sourceName );
                return error;
            }

            /** @brief 문서 @p sourceName 의 항목 @p key 가 틀렸다는 오류입니다(대상 `원본: 키`). */
            [[nodiscard]] static Error atEntry( string_view sourceName, string_view key, const utf8* pMessage )
            {
                return atSource( Error{ ErrorKind::InvalidArgument, pMessage, key }, sourceName );
            }

            /** @brief 문서 머리(루트의 `culture` 속성, `entries` 자식)를 읽습니다. 틀리면 그 까닭입니다. */
            [[nodiscard]] static expected<void> readHeader( XMLDocument& document, string_view xmlText, string_view sourceName, const utf8* pRootName, string& outCulture, XMLNode& outEntries )
            {
                XMLNode root;
                if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, pRootName, root ) != XMLRootStatus::Found )
                {
                    if ( document.getLastError().empty() == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "the document is not well-formed XML", document.getLastError() } );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the root element is wrong", pRootName } );
                }
                if ( const optional<string_view> unknown = findUnknownName( root, { "culture" }, { kEntriesTag } ); unknown.has_value() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "unknown field", *unknown } );
                outCulture = CultureTable::normalizeCode( root.getAttributeText( "culture" ) );
                outEntries = root.findChild( "entries" );
                if ( outCulture.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "needs a \"culture\" attribute", pRootName } );
                return {};
            }

            /** @brief 항목 요소 하나의 이름 · 키를 확인합니다. 키는 비지 않아야 한다. 틀리면 그 까닭입니다. */
            [[nodiscard]] static expected<void> readEntryKey( const XMLNode& entryNode, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string& outKey )
            {
                if ( string_view{ entryNode.getName() } != kEntryTag )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "<entries> holds only <entry> elements", entryNode.getName() } );
                if ( const optional<string_view> unknown = findUnknownName( entryNode, listAttribute, listChild ); unknown.has_value() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "entry has an unknown field", *unknown } );
                outKey = string{ entryNode.getAttributeText( "key" ) };
                if ( outKey.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "entry needs a \"key\" attribute" } );
                return {};
            }

            [[nodiscard]] static expected<void> readTextFile( string_view absolutePath, string& outText )
            {
                if ( FileUtil::readTextFile( absolutePath, outText ) )
                    return {};
                return unexpected( Error{ ErrorKind::NotFound, "cannot read the file", absolutePath } );
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
    uint64 SourceTextEntry::computeSourceHash( string_view sourceText )
    {
        const uint64 hash = HashUtil::computeStringHash64( sourceText, false );
        return hash == 0 ? 1u : hash;
    }

    string SourceTextEntry::formatSourceHash( uint64 sourceHash )
    {
        utf8         arrBuffer[constant::buffer::kMax32]{};
        const uint32 length = StringUtil::formatNumber( arrBuffer, constant::buffer::kMax32, sourceHash, 16 );
        string       text( arrBuffer, length );
        while ( text.size() < 16 )
        {
            text.insert( text.begin(), '0' );
        }
        return text;
    }

    bool SourceTextEntry::tryParseSourceHash( string_view text, uint64& outSourceHash )
    {
        return text.size() == 16 && StringUtil::parseUint64( text, outSourceHash, 16 );
    }

    string SourceTextEntry::makeFullKey( string_view textNamespace, string_view key )
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
    expected<void> SourceStringTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        using Internal = LocalizationDocumentsInternal;
        XMLDocument document;
        string      culture;
        XMLNode     entries;
        if ( expected<void> header = Internal::readHeader( document, xmlText, sourceName, "SourceStringTable", culture, entries ); header.has_value() == false )
            return unexpected( Internal::atSource( std::move( header ).error(), sourceName ) );
        map<string, SourceTextEntry> mapEntry;
        for ( XMLNode entryNode = entries.findChild(); entryNode.isValid(); entryNode = entryNode.findNextSibling() )
        {
            string key;
            if ( expected<void> keyRead = Internal::readEntryKey( entryNode, { "key", "source", "context", "comment", "maxLength" }, { "origins" }, key ); keyRead.has_value() == false )
                return unexpected( Internal::atSource( std::move( keyRead ).error(), sourceName ) );
            if ( mapEntry.find( key ) != mapEntry.end() )
                return unexpected( Internal::atEntry( sourceName, key, "an entry is written twice" ) );
            if ( entryNode.findAttribute( "source" ) == nullptr )
                return unexpected( Internal::atEntry( sourceName, key, "an entry needs a \"source\" attribute" ) );
            SourceTextEntry entry;
            entry._source  = string{ entryNode.getAttributeText( "source" ) };
            entry._context = string{ entryNode.getAttributeText( "context" ) };
            entry._comment = string{ entryNode.getAttributeText( "comment" ) };
            // 최대 길이는 없으면 항목의 초기값(제한 없음 0)이다.
            optional<uint32> maxLength;
            if ( entryNode.readOptionalAttribute( "maxLength", maxLength ) == false )
                return unexpected( Internal::atEntry( sourceName, key, "maxLength must be a non-negative integer" ) );
            if ( maxLength.has_value() )
                entry._maxLength = *maxLength;
            if ( Internal::readStringList( entryNode.findChild( "origins" ), entry._listOrigin ) == false )
                return unexpected( Internal::atEntry( sourceName, key, "origins must be <item> elements" ) );
            mapEntry[key] = std::move( entry );
        }
        _culture  = culture;
        _mapEntry = std::move( mapEntry );
        return {};
    }

    expected<void> SourceStringTable::loadFromFile( string_view absolutePath )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text ).and_then( [&]()
        { return loadFromXMLText( text, absolutePath ); } );
    }

    string SourceStringTable::saveToXMLText() const
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

    bool SourceStringTable::saveToFile( const AbsolutePath& absPath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absPath.str(), saveToXMLText() );
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
    expected<void> TranslationTable::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        using Internal = LocalizationDocumentsInternal;
        XMLDocument document;
        string      culture;
        XMLNode     entries;
        if ( expected<void> header = Internal::readHeader( document, xmlText, sourceName, "TranslationTable", culture, entries ); header.has_value() == false )
            return unexpected( Internal::atSource( std::move( header ).error(), sourceName ) );
        map<string, TranslationEntry> mapEntry;
        for ( XMLNode entryNode = entries.findChild(); entryNode.isValid(); entryNode = entryNode.findNextSibling() )
        {
            string key;
            if ( expected<void> keyRead = Internal::readEntryKey( entryNode, { "key", "text", "sourceHash", "review", "translatorComment" }, {}, key ); keyRead.has_value() == false )
                return unexpected( Internal::atSource( std::move( keyRead ).error(), sourceName ) );
            if ( mapEntry.find( key ) != mapEntry.end() )
                return unexpected( Internal::atEntry( sourceName, key, "an entry is written twice" ) );
            if ( entryNode.findAttribute( "text" ) == nullptr )
                return unexpected( Internal::atEntry( sourceName, key, "an entry needs a \"text\" attribute" ) );
            TranslationEntry entry;
            entry._text              = string{ entryNode.getAttributeText( "text" ) };
            entry._translatorComment = string{ entryNode.getAttributeText( "translatorComment" ) };
            optional<bool> review;
            if ( entryNode.readOptionalAttribute( "review", review ) == false )
                return unexpected( Internal::atEntry( sourceName, key, "review must be true or false" ) );
            entry._bReview = review.value_or( false );
            if ( entryNode.findAttribute( "sourceHash" ) != nullptr && SourceTextEntry::tryParseSourceHash( entryNode.getAttributeText( "sourceHash" ), entry._sourceHash ) == false )
                return unexpected( Internal::atEntry( sourceName, key, "sourceHash must be 16 hex digits" ) );
            mapEntry[key] = std::move( entry );
        }
        _culture  = culture;
        _mapEntry = std::move( mapEntry );
        return {};
    }

    expected<void> TranslationTable::loadFromFile( string_view absolutePath )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text ).and_then( [&]()
        { return loadFromXMLText( text, absolutePath ); } );
    }

    string TranslationTable::saveToXMLText() const
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
                value.appendAttribute( "sourceHash", string_view{ SourceTextEntry::formatSourceHash( entry._sourceHash ) } );
            if ( entry._bReview )
                value.appendAttribute( "review", string_view{ "true" } );
            if ( entry._translatorComment.empty() == false )
                value.appendAttribute( "translatorComment", string_view{ entry._translatorComment } );
        }
        return document.saveToString();
    }

    bool TranslationTable::saveToFile( const AbsolutePath& absPath ) const
    {
        return LocalizationDocumentsInternal::writeTextFile( absPath.str(), saveToXMLText() );
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
        const bool bStale = pEntry->_sourceHash != 0 && pEntry->_sourceHash != SourceTextEntry::computeSourceHash( pSource->_source );
        return bStale ? TranslationState::Stale : TranslationState::Current;
    }

    void TranslationTable::setCulture( string_view culture )
    {
        _culture = CultureTable::normalizeCode( culture );
    }

    // ------------------------------------------------------------------------------
    // LocalizationProject
    // ------------------------------------------------------------------------------
    expected<void> LocalizationProject::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        using Internal = LocalizationDocumentsInternal;
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, FileUtil::skipUTF8Bom( xmlText ), sourceName, "LocalizationProject", root ) != XMLRootStatus::Found )
        {
            if ( document.getLastError().empty() == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the document is not well-formed XML", document.getLastError() } );
            return unexpected( Internal::atSource( Error{ ErrorKind::InvalidArgument, "root must be <LocalizationProject>" }, sourceName ) );
        }
        if ( const optional<string_view> unknown = Internal::findUnknownName( root, { "name", "sourceCulture" }, { "cultures", "stagedCultures", "stringTables", "codeRoots", "assetRoots", "assetRules" } );
             unknown.has_value() )
            return unexpected( Internal::atSource( Error{ ErrorKind::InvalidArgument, "unknown field", *unknown }, sourceName ) );

        LocalizationProject loaded;
        loaded._name          = string{ root.getAttributeText( "name" ) };
        loaded._sourceCulture = CultureTable::normalizeCode( root.getAttributeText( "sourceCulture" ) );
        const bool bListsRead = Internal::readStringList( root.findChild( "cultures" ), loaded._listCulture ) &&
                                Internal::readStringList( root.findChild( "stagedCultures" ), loaded._listStagedCulture ) &&
                                Internal::readStringList( root.findChild( "stringTables" ), loaded._listStringTable ) &&
                                Internal::readStringList( root.findChild( "codeRoots" ), loaded._listCodeRoot ) &&
                                Internal::readStringList( root.findChild( "assetRoots" ), loaded._listAssetRoot );
        size_t ruleIndex{ 0 };
        for ( XMLNode ruleNode = root.findChild( "assetRules" ).findChild(); ruleNode.isValid(); ruleNode = ruleNode.findNextSibling(), ++ruleIndex )
        {
            LocalizationAssetRule       rule;
            const optional<string_view> unknown = Internal::findUnknownName( ruleNode, { "files", "attribute", "kind", "context" }, { "elements" } );
            const bool                  bShape  = string_view{ ruleNode.getName() } == "rule" && unknown.has_value() == false && Internal::readStringList( ruleNode.findChild( "elements" ), rule._listElement );
            rule._fileSuffix                    = string{ ruleNode.getAttributeText( "files" ) };
            rule._attribute                     = string{ ruleNode.getAttributeText( "attribute" ) };
            rule._context                       = string{ ruleNode.getAttributeText( "context" ) };
            const string kind{ ruleNode.getAttributeText( "kind" ) };
            rule._bKeyReference          = kind == "key";
            const bool bKnownKind        = kind == "key" || kind == "text";
            const bool bInvalidAssetRule = bShape == false || bKnownKind == false || rule._fileSuffix.empty() || rule._attribute.empty() || rule._listElement.empty();
            if ( bInvalidAssetRule )
            {
                Error error{ ErrorKind::InvalidArgument, "an asset rule needs files, elements, attribute and kind (text | key)", "assetRules[" };
                error.appendDetail( to_string( static_cast<uint64>( ruleIndex ) ) ).appendDetail( "]" );
                if ( unknown.has_value() )
                    error.appendDetail( " - unknown field " ).appendDetail( *unknown );
                return unexpected( Internal::atSource( error, sourceName ) );
            }
            loaded._listAssetRule.push_back( std::move( rule ) );
        }
        const bool bMissingField = loaded._name.empty() || loaded._sourceCulture.empty() || loaded._listStringTable.empty() || bListsRead == false;
        if ( bMissingField )
            return unexpected( Internal::atSource( Error{ ErrorKind::InvalidArgument, "needs \"name\", \"sourceCulture\", \"stringTables\" and string lists" }, sourceName ) );
        for ( string& culture : loaded._listCulture )
        {
            culture = CultureTable::normalizeCode( culture );
        }
        for ( string& culture : loaded._listStagedCulture )
        {
            culture = CultureTable::normalizeCode( culture );
            if ( VectorUtil::contains( loaded._listCulture, culture ) == false )
                return unexpected( Internal::atSource( Error{ ErrorKind::InvalidArgument, "a staged culture is not in cultures", culture }, sourceName ) );
        }
        *this = std::move( loaded );
        return {};
    }

    expected<void> LocalizationProject::loadFromFile( string_view absolutePath )
    {
        string text;
        return LocalizationDocumentsInternal::readTextFile( absolutePath, text ).and_then( [&]()
        { return loadFromXMLText( text, absolutePath ); } );
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

    expected<void> LocalizationProject::collectUnstagedTranslationTables( string_view folder, vector<string>& outListAbsPath )
    {
        vector<string> listXML;
        (void)FileUtil::collectFiles( folder, ".xml", listXML, true ); // 폴더가 없으면 프로젝트도 없다
        const size_t tableSuffixLength = string_view( TranslationTable::kExtension ).size();
        for ( const string& projectPath : listXML )
        {
            if ( StringUtil::endsWith( projectPath, kExtension, true ) == false )
                continue;
            LocalizationProject project;
            if ( expected<void> read = project.loadFromFile( projectPath ); read.has_value() == false )
                return unexpected( std::move( read ).error() );
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
        return {};
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
