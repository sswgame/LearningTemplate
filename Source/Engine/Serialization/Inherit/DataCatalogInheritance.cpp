#include "pch.h"

#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/AssetCookRoot.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct DataCatalogInheritanceInternal
        {
            /**
             * @brief 파일 상속을 합치는 동안 항목의 `_base` 를 담아 두는 이름입니다.
             * @details 합치기는 `_base` 를 파일에만 있는 칸으로 보고 지운다. 항목의 부모는 파일을 다 합친 뒤에 찾아야 하므로(자식 파일이 부모 파일의 항목을 고치고, 부모로 삼는다) 그때까지 보통 칸으로 남긴다.
             */
            static constexpr const utf8* kEntryBaseTransit = "_entryBaseTransit";

            static constexpr const utf8* kDocumentElement = "Document"; ///< 규칙 표: 문서 형식 한 줄
            static constexpr const utf8* kCatalogElement  = "Catalog";  ///< 규칙 표: 카탈로그 한 줄
            static constexpr const utf8* kEntryElement    = "Entry";    ///< 규칙 표: 카탈로그의 항목 종류(`key` 필수)
            static constexpr const utf8* kFieldElement    = "Field";    ///< 규칙 표: 칸(`key` 가 있으면 목록 원소의 열쇠). 카탈로그 줄 바로 아래면 항목이 아닌 칸이다(`_base` 를 받지 않는다)

            static bool isElement( const XMLNode& node )
            {
                return node.isValid() && StringUtil::isNullOrEmpty( node.getName() ) == false;
            }

            /** @brief 속성과 글을 옮깁니다. @p pRenameFrom 이름의 속성은 @p pRenameTo 로 적습니다(둘 다 nullptr 이면 그대로). */
            static void copyOwnValues( const XMLNode& src, const XMLNode& out, const utf8* pRenameFrom, const utf8* pRenameTo )
            {
                for ( XMLAttribute attribute = src.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    const bool bRename = pRenameFrom != nullptr && StringUtil::equals( attribute.getName(), pRenameFrom );
                    out.setAttribute( bRename ? pRenameTo : attribute.getName(), attribute.getValue() );
                }
                if ( StringUtil::isNullOrEmpty( src.getText() ) == false )
                    out.setValue( src.getText() );
            }

            /**
             * @brief 같은 이름의 형제를 그 이름의 래퍼 하나로 묶습니다(구조체 칸은 그대로). 규칙에 없는 이름도 묶어 통째로 바뀌게 합니다.
             * @param bOwnBaseIsEntry 이 원소가 파일 상속 단계의 항목이면 true 입니다(`_base` 를 `kEntryBaseTransit` 로 옮긴다).
             * @param bItemsAreEntries 이 원소가 파일의 루트이면 true 입니다. 자식 목록의 원소가 항목입니다.
             */
            [[nodiscard]] static bool wrapElement( const XMLNode& src, const DataInheritanceSchema& schema, const XMLNode& out, bool bOwnBaseIsEntry, bool bItemsAreEntries, string_view sourceName )
            {
                copyOwnValues( src, out, bOwnBaseIsEntry ? DataInheritance::kBaseAttribute : nullptr, bOwnBaseIsEntry ? kEntryBaseTransit : nullptr );
                vector<string> listSeen;
                for ( XMLNode child = src.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    const string name{ child.getName() };
                    if ( VectorUtil::contains( listSeen, name ) )
                        continue;
                    listSeen.push_back( name );
                    const DataInheritanceSchema* pField = schema.findField( name );
                    if ( pField != nullptr && pField->_keyAttribute.empty() )
                    {
                        if ( child.findNextSibling( name.c_str() ).isValid() )
                        {
                            SW_LOG_ERROR_ID( "DataCatalogInheritance.FieldRepeated", "'%#': <%#> is written twice in <%#> - the inheritance rule says it is a single field", sourceName, name, src.getName() );
                            return false;
                        }
                        if ( wrapElement( child, *pField, out.appendChild( name.c_str() ), false, false, sourceName ) == false )
                            return false;
                        continue;
                    }
                    const XMLNode wrapper = out.appendChild( name.c_str() );
                    for ( XMLNode item = child; item.isValid(); item = item.findNextSibling( name.c_str() ) )
                    {
                        if ( pField != nullptr )
                        {
                            if ( wrapElement( item, *pField, wrapper.appendChild( name.c_str() ), bItemsAreEntries, false, sourceName ) == false )
                                return false;
                        }
                        else if ( item.getAttributeText( DataInheritance::kOpAttribute ) != DataCatalogInheritance::kOpClear )
                        {
                            (void)wrapper.appendClone( item );
                        }
                    }
                }
                return true;
            }

            /** @brief `wrapElement` 를 되돌립니다. 합친 원소 아래의 작업 칸은 합치기가 이미 지웠습니다. */
            static void unwrapElement( const XMLNode& src, const DataInheritanceSchema& schema, const XMLNode& out )
            {
                copyOwnValues( src, out, kEntryBaseTransit, DataInheritance::kBaseAttribute );
                for ( XMLNode child = src.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( isElement( child ) == false )
                        continue;
                    const DataInheritanceSchema* pField = schema.findField( child.getName() );
                    if ( pField != nullptr && pField->_keyAttribute.empty() )
                    {
                        unwrapElement( child, *pField, out.appendChild( child.getName() ) );
                        continue;
                    }
                    for ( XMLNode item = child.findChild(); item.isValid(); item = item.findNextSibling() )
                    {
                        if ( isElement( item ) == false )
                            continue;
                        if ( pField != nullptr )
                            unwrapElement( item, *pField, out.appendChild( item.getName() ) );
                        else
                            (void)out.appendClone( item );
                    }
                }
            }

            [[nodiscard]] static bool wrapToText( const XMLNode& root, const DataInheritanceSchema& schema, bool bItemsAreEntries, string_view sourceName, string& outText )
            {
                XMLDocument   doc;
                const XMLNode out = doc.appendRoot( root.getName() );
                if ( wrapElement( root, schema, out, false, bItemsAreEntries, sourceName ) == false )
                    return false;
                outText = doc.saveToString();
                return true;
            }

            /** @brief 부모 파일을 래퍼 꼴의 글로 돌려줍니다(파일 상속 단계). */
            class WrappedFileSource final : public IDataInheritanceSource
            {
            public:
                WrappedFileSource( const DataInheritanceSchema& schema, IDataInheritanceSource& source )
                    : _schema{ schema }
                    , _source{ source }
                {
                }

                [[nodiscard]] bool readText( string_view assetID, string& outText ) override
                {
                    string      text;
                    XMLDocument doc;
                    if ( _source.readText( assetID, text ) == false || doc.parse( text, assetID ) == false )
                        return false;
                    return wrapToText( doc.getRoot(), _schema, true, assetID, outText );
                }

            private:
                const DataInheritanceSchema& _schema;
                IDataInheritanceSource&      _source;
            };

            /** @brief 파일 상속을 펼쳐 @p outDoc 에 둡니다. 항목의 `_base` 는 그대로 남습니다. */
            [[nodiscard]] static bool flattenFile( string_view text, string_view sourceName, const DataInheritanceSchema& schema, IDataInheritanceSource& source, XMLDocument& outDoc )
            {
                if ( outDoc.parse( text, sourceName ) == false )
                    return false;
                const XMLNode root = outDoc.getRoot();
                if ( root.getAttributeText( DataInheritance::kBaseAttribute ).empty() )
                    return true;
                string wrappedText;
                if ( wrapToText( root, schema, true, sourceName, wrappedText ) == false )
                    return false;
                WrappedFileSource wrappedSource{ schema, source };
                string            mergedText;
                if ( DataInheritance::flatten( wrappedText, sourceName, schema, wrappedSource, mergedText ) == false )
                    return false;
                XMLDocument mergedDoc;
                if ( mergedDoc.parse( mergedText, sourceName ) == false )
                    return false;
                XMLDocument unwrappedDoc;
                unwrapElement( mergedDoc.getRoot(), schema, unwrappedDoc.appendRoot( mergedDoc.getRoot().getName() ) );
                outDoc = std::move( unwrappedDoc );
                return true;
            }

            /** @brief 항목의 부모로 읽은 다른 파일들입니다. 한 번의 펼치기 안에서 같은 파일을 다시 합치지 않습니다. */
            class ParentFiles
            {
            public:
                ParentFiles( const DataInheritanceSchema& schema, IDataInheritanceSource& source )
                    : _schema{ schema }
                    , _source{ source }
                    , _listFile{}
                {
                }

                /** @brief @p assetID 의 파일 상속을 펼친 루트입니다. 읽지 못하면 무효입니다. */
                XMLNode findRoot( string_view assetID )
                {
                    for ( const LoadedFile& file : _listFile )
                    {
                        if ( string_view( file._assetID ) == assetID )
                            return file._doc.getRoot();
                    }
                    string     text;
                    LoadedFile file;
                    if ( _source.readText( assetID, text ) == false || flattenFile( text, assetID, _schema, _source, file._doc ) == false )
                        return {};
                    file._assetID = string{ assetID };
                    _listFile.push_back( std::move( file ) );
                    return _listFile.back()._doc.getRoot();
                }

            private:
                struct LoadedFile
                {
                    string      _assetID;
                    XMLDocument _doc;
                };

                const DataInheritanceSchema& _schema;
                IDataInheritanceSource&      _source;
                vector<LoadedFile>           _listFile;
            };

            /** @brief 항목 하나의 부모를 `<id>` 또는 `<리소스 id>#<id>` 로 찾아 래퍼 꼴의 글로 돌려줍니다. */
            class EntrySource final : public IDataInheritanceSource
            {
            public:
                EntrySource( const XMLNode& fileRoot, const utf8* pKindName, const string& keyAttribute, const DataInheritanceSchema& entrySchema, ParentFiles& parentFiles )
                    : _fileRoot{ fileRoot }
                    , _pKindName{ pKindName }
                    , _keyAttribute{ keyAttribute }
                    , _entrySchema{ entrySchema }
                    , _parentFiles{ parentFiles }
                {
                }

                [[nodiscard]] bool readText( string_view baseID, string& outText ) override
                {
                    const size_t      separator  = baseID.find( DataCatalogInheritance::kFileSeparator );
                    const bool        bOtherFile = separator != string_view::npos;
                    const string_view filePart   = bOtherFile ? baseID.substr( 0, separator ) : string_view{};
                    const string_view entryPart  = bOtherFile ? baseID.substr( separator + 1 ) : baseID;
                    const XMLNode     root       = bOtherFile ? _parentFiles.findRoot( filePart ) : _fileRoot;
                    if ( root.isValid() == false )
                        return false;
                    XMLNode entry = root.findChild( _pKindName );
                    while ( entry.isValid() && entry.getAttributeText( _keyAttribute.c_str() ) != entryPart )
                    {
                        entry = entry.findNextSibling( _pKindName );
                    }
                    if ( entry.isValid() == false )
                        return false;
                    XMLDocument   doc;
                    const XMLNode out = doc.appendRoot( _pKindName );
                    if ( wrapElement( entry, _entrySchema, out, false, false, baseID ) == false )
                        return false;
                    // 다른 파일 항목의 `_base` 는 그 파일 기준이다 — 이 문서에서 찾지 않게 파일을 붙인다.
                    const string_view grandParent = out.getAttributeText( DataInheritance::kBaseAttribute );
                    if ( bOtherFile && grandParent.empty() == false && grandParent.find( DataCatalogInheritance::kFileSeparator ) == string_view::npos )
                    {
                        string qualified{ filePart };
                        qualified += DataCatalogInheritance::kFileSeparator;
                        qualified += grandParent;
                        out.setAttribute( DataInheritance::kBaseAttribute, string_view( qualified ) );
                    }
                    outText = doc.saveToString();
                    return true;
                }

            private:
                XMLNode                      _fileRoot;
                const utf8*                  _pKindName;
                const string&                _keyAttribute;
                const DataInheritanceSchema& _entrySchema;
                ParentFiles&                 _parentFiles;
            };

            /** @brief 항목 하나를 펼쳐 @p outRoot 의 자식으로 더합니다. */
            [[nodiscard]] static bool flattenEntry( const XMLNode& entry, const XMLNode& fileRoot, const DataInheritanceSchema& schema, ParentFiles& parentFiles, string_view sourceName, const XMLNode& outRoot )
            {
                const DataInheritanceSchema* pKind = schema.findField( entry.getName() );
                if ( pKind == nullptr || pKind->_keyAttribute.empty() )
                {
                    SW_LOG_ERROR_ID( "DataCatalogInheritance.KindNotListed", "'%#': <%#> has %# but it is not an entry kind of <%#> in %#", sourceName, entry.getName(), DataInheritance::kBaseAttribute, fileRoot.getName(),
                                     DataCatalogInheritance::kTableAssetID );
                    return false;
                }
                DataInheritanceSchema entrySchema = *pKind; // 항목 문서의 맨 위는 목록이 아니라 칸이다
                entrySchema._keyAttribute.clear();
                string entryName{ sourceName };
                entryName += DataCatalogInheritance::kFileSeparator;
                entryName += entry.getAttributeText( pKind->_keyAttribute.c_str() );

                string wrappedText;
                if ( wrapToText( entry, entrySchema, false, entryName, wrappedText ) == false )
                    return false;
                EntrySource entrySource{ fileRoot, entry.getName(), pKind->_keyAttribute, entrySchema, parentFiles };
                string      mergedText;
                if ( DataInheritance::flatten( wrappedText, entryName, entrySchema, entrySource, mergedText ) == false )
                    return false;
                XMLDocument mergedDoc;
                if ( mergedDoc.parse( mergedText, entryName ) == false )
                    return false;
                unwrapElement( mergedDoc.getRoot(), entrySchema, outRoot.appendChild( entry.getName() ) );
                return true;
            }

            /** @brief 규칙 표의 줄 하나(@p node)의 @p pChildName 자식을 @p out 의 칸으로 읽습니다. */
            [[nodiscard]] static bool readSchemaFields( const XMLNode& node, const utf8* pChildName, DataInheritanceSchema& out, bool bKeyRequired )
            {
                bool bValid = true;
                for ( XMLNode element = node.findChild( pChildName ); element.isValid(); element = element.findNextSibling( pChildName ) )
                {
                    const string_view name = element.getAttributeText( "name" );
                    const string_view key  = element.getAttributeText( "key" );
                    if ( name.empty() || ( bKeyRequired && key.empty() ) )
                    {
                        SW_LOG_ERROR_ID( "DataCatalogInheritance.RuleInvalid", "%#: a rule of <%#> has no name or no key", DataCatalogInheritance::kTableAssetID, out._name );
                        bValid = false;
                        continue;
                    }
                    DataInheritanceSchema field{ name };
                    field._keyAttribute = string{ key };
                    bValid              = readSchemaFields( element, kFieldElement, field, false ) && bValid;
                    out._listField.push_back( std::move( field ) );
                }
                return bValid;
            }

            /** @brief 표 글에서 @p pRowElement 줄 중 루트가 @p rootName 인 줄을 규칙으로 읽습니다. */
            [[nodiscard]] static bool parseRow( string_view tableText, const utf8* pRowElement, const utf8* pChildName, bool bKeyRequired, string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed )
            {
                outbListed = false;
                XMLDocument doc;
                if ( doc.parse( tableText, DataCatalogInheritance::kTableAssetID ) == false )
                {
                    SW_LOG_ERROR_ID( "DataCatalogInheritance.TableInvalid", "Inheritance rule table '%#' could not be parsed: %#", DataCatalogInheritance::kTableAssetID, doc.getLastError() );
                    return false;
                }
                for ( XMLNode row = doc.getRoot().findChild( pRowElement ); row.isValid(); row = row.findNextSibling( pRowElement ) )
                {
                    if ( row.getAttributeText( "root" ) != rootName )
                        continue;
                    DataInheritanceSchema schema{ rootName };
                    if ( readSchemaFields( row, pChildName, schema, bKeyRequired ) == false )
                        return false;
                    // 카탈로그 줄의 `<Field>` 는 항목 종류가 아닌 루트 칸이다 — 래퍼 하나에 든 열쇠 목록(입력 맵의 `<layers><layer name>`)처럼 항목 id 가 없는 것.
                    const bool bCatalogRow = StringUtil::equals( pRowElement, kCatalogElement );
                    if ( bCatalogRow && readSchemaFields( row, kFieldElement, schema, false ) == false )
                        return false;
                    outSchema  = std::move( schema );
                    outbListed = true;
                    return true;
                }
                return true;
            }

            /** @brief 표를 읽습니다. 리소스라 쿠킹 작업 안이면 그 작업의 입력이 되고, 배포 구성은 엔진 팩에서 읽습니다. 읽지 못하면 오류입니다. */
            [[nodiscard]] static bool readTable( string& outText )
            {
                if ( AssetIO::readTextAsset( AssetID{ DataCatalogInheritance::kTableAssetID }, outText ) )
                    return true;
                SW_LOG_ERROR_ID( "DataCatalogInheritance.TableUnreadable", "Inheritance rule table '%#' could not be read", DataCatalogInheritance::kTableAssetID );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool DataCatalogInheritance::parseSchema( string_view tableText, string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed )
    {
        using Internal = DataCatalogInheritanceInternal;
        return Internal::parseRow( tableText, Internal::kCatalogElement, Internal::kEntryElement, true, rootName, outSchema, outbListed );
    }

    bool DataCatalogInheritance::loadSchema( string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed )
    {
        outbListed = false;
        string text;
        return DataCatalogInheritanceInternal::readTable( text ) && parseSchema( text, rootName, outSchema, outbListed );
    }

    bool DataCatalogInheritance::flatten( string_view text, string_view sourceName, const DataInheritanceSchema& schema, IDataInheritanceSource& source, string& outText )
    {
        XMLDocument fileDoc;
        if ( DataCatalogInheritanceInternal::flattenFile( text, sourceName, schema, source, fileDoc ) == false )
            return false;
        const XMLNode fileRoot = fileDoc.getRoot();
        XMLDocument   outDoc;
        const XMLNode outRoot = outDoc.appendRoot( fileRoot.getName() );
        for ( XMLAttribute attribute = fileRoot.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            if ( StringUtil::equals( attribute.getName(), DataInheritance::kBaseAttribute ) == false )
                outRoot.setAttribute( attribute.getName(), attribute.getValue() );
        }
        DataCatalogInheritanceInternal::ParentFiles parentFiles{ schema, source };
        for ( XMLNode entry = fileRoot.findChild(); entry.isValid(); entry = entry.findNextSibling() )
        {
            if ( DataCatalogInheritanceInternal::isElement( entry ) == false )
                continue;
            if ( entry.hasAttribute( DataInheritance::kBaseAttribute ) == false )
                (void)outRoot.appendClone( entry );
            else if ( DataCatalogInheritanceInternal::flattenEntry( entry, fileRoot, schema, parentFiles, sourceName, outRoot ) == false )
                return false;
        }
        outText = outDoc.saveToString();
        return true;
    }

    bool DataCatalogInheritance::makeFileDelta( string_view fullText, string_view parentFullText, const DataInheritanceSchema& schema, string_view baseID, string& outText )
    {
        using Internal = DataCatalogInheritanceInternal;
        XMLDocument fullDoc;
        XMLDocument parentDoc;
        if ( fullDoc.parse( fullText, "delta" ) == false || parentDoc.parse( parentFullText, baseID ) == false )
        {
            SW_LOG_ERROR_ID( "DataCatalogInheritance.DeltaNotXML", "A catalog delta of base '%#' could not be made - one side is not XML", baseID );
            return false;
        }
        string wrappedFull;
        string wrappedParent;
        if ( Internal::wrapToText( fullDoc.getRoot(), schema, true, "delta", wrappedFull ) == false || Internal::wrapToText( parentDoc.getRoot(), schema, true, baseID, wrappedParent ) == false )
            return false;
        string      wrappedDelta;
        XMLDocument deltaDoc;
        if ( DataInheritance::makeDelta( wrappedFull, wrappedParent, schema, baseID, wrappedDelta ) == false || deltaDoc.parse( wrappedDelta, baseID ) == false )
            return false;
        XMLDocument outDoc;
        Internal::unwrapElement( deltaDoc.getRoot(), schema, outDoc.appendRoot( deltaDoc.getRoot().getName() ) );
        outText = outDoc.saveToString();
        return true;
    }

    bool DataCatalogInheritance::flattenIfListed( string_view text, string_view sourceName, IDataInheritanceSource& source, bool& outbFlattened, string& outText )
    {
        outbFlattened = false;
        if ( text.find( DataInheritance::kBaseAttribute ) == string_view::npos )
            return true; // 거의 모든 파일이 여기서 끝난다 — 런타임 읽기 · 팩 쿠커가 모든 파일을 지나 보내는 자리다
        string_view body = StringUtil::trim( text );
        if ( StringUtil::startsWith( body, "\xEF\xBB\xBF" ) )
            body.remove_prefix( 3 );
        if ( body.empty() || body.front() != '<' )
            return true;
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
            return true; // 깨진 글은 읽는 쪽이 자기 오류로 알린다
        const XMLNode root      = doc.getRoot();
        bool          bHasBases = root.hasAttribute( DataInheritance::kBaseAttribute );
        for ( XMLNode child = root.findChild(); child.isValid() && bHasBases == false; child = child.findNextSibling() )
        {
            bHasBases = child.hasAttribute( DataInheritance::kBaseAttribute );
        }
        if ( bHasBases == false )
            return true;
        DataInheritanceSchema schema{ root.getName() };
        bool                  bListed = false;
        if ( loadSchema( root.getName(), schema, bListed ) == false )
            return false;
        if ( bListed == false )
            return true; // 파이프라인 · 머티리얼 · 프리팹은 자기 입구가 펼친다
        outbFlattened = true;
        return flatten( text, sourceName, schema, source, outText );
    }

    bool DataCatalogInheritance::parseDocumentRule( string_view tableText, string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed )
    {
        using Internal = DataCatalogInheritanceInternal;
        return Internal::parseRow( tableText, Internal::kDocumentElement, Internal::kFieldElement, false, rootName, outSchema, outbListed );
    }

    bool DataCatalogInheritance::loadDocumentRule( string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed )
    {
        outbListed = false;
        string text;
        return DataCatalogInheritanceInternal::readTable( text ) && parseDocumentRule( text, rootName, outSchema, outbListed );
    }
} // namespace sw

// 표는 코드가 이름으로 읽는다 — 배포 런타임이 덮어쓰기 층을 얹을 때 읽으므로 엔진 팩에 담는다.
SW_ASSET_COOK_ROOT( DataCatalogInheritanceTable, ::sw::DataCatalogInheritance::kTableAssetID );
