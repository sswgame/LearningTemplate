#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogInheritanceView.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogInheritanceViewInternal
        {
            /** @brief 합치기 작업 칸이면 true 입니다. 값 칸이 아니다. */
            static bool isMetaAttribute( string_view name )
            {
                return name == DataInheritance::kBaseAttribute || name == DataInheritance::kOpAttribute || name == DataInheritance::kBeforeAttribute || name == DataInheritance::kAfterAttribute;
            }

            static void addUnique( vector<string>& inoutList, string_view name )
            {
                const string text{ name };
                if ( VectorUtil::contains( inoutList, text ) == false )
                    inoutList.push_back( text );
            }

            static bool isElement( const XMLNode& node ) { return node.isValid() && StringUtil::isNullOrEmpty( node.getName() ) == false; }

            /** @brief 원소의 글 값 칸들을 @p inoutListColumn 에 더합니다. */
            static void addNodeColumns( const XMLNode& node, vector<string>& inoutListColumn )
            {
                if ( node.isValid() == false )
                    return;
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    if ( isMetaAttribute( attribute.getName() ) == false )
                        addUnique( inoutListColumn, attribute.getName() );
                }
            }

            static void addWrittenColumns( const CatalogXMLElement* pWritten, vector<string>& inoutListColumn )
            {
                if ( pWritten == nullptr )
                    return;
                for ( const CatalogXMLAttribute& attribute : pWritten->_listAttribute )
                {
                    if ( isMetaAttribute( attribute._name ) == false )
                        addUnique( inoutListColumn, attribute._name );
                }
            }

            static const CatalogXMLElement* findWritten( const CatalogXMLText& parsed, optional<int32> ownerIndex, const CatalogChildStep& step )
            {
                const optional<int32> index = CatalogInheritanceView::findWrittenChild( parsed, ownerIndex, step );
                return index.has_value() ? &parsed.getElement( *index ) : nullptr;
            }

            static string_view readWrittenAttribute( const CatalogXMLElement* pWritten, string_view name )
            {
                const optional<uint32> index = pWritten != nullptr ? pWritten->findAttribute( name ) : nullopt;
                return index.has_value() ? string_view( pWritten->_listAttribute[*index]._value ) : string_view{};
            }

            /** @brief 줄의 칸을 구역의 열 순서로 채웁니다. 합친 값이 없는 칸(뺀 원소)은 적은 값 · 빈 글입니다. */
            static void fillCells( const CatalogChildSection& section, const CatalogXMLElement* pWritten, const XMLNode& flat, const XMLNode& parent, CatalogChildNode& inoutNode )
            {
                inoutNode._listCell.clear();
                for ( const string& column : section._listColumn )
                {
                    CatalogChildCell cell;
                    cell._column                            = column;
                    cell._bKey                              = column == section._keyAttribute;
                    const optional<uint32> writtenAttribute = pWritten != nullptr ? pWritten->findAttribute( column ) : nullopt;
                    if ( writtenAttribute.has_value() )
                    {
                        cell._value    = pWritten->_listAttribute[*writtenAttribute]._value;
                        cell._bWritten = true;
                    }
                    else
                    {
                        const utf8* pFlat = flat.isValid() ? flat.findAttribute( column.c_str() ) : nullptr;
                        cell._value       = pFlat != nullptr ? string( pFlat ) : string();
                        cell._bInherited  = parent.isValid() && parent.findAttribute( column.c_str() ) != nullptr;
                    }
                    inoutNode._listCell.push_back( std::move( cell ) );
                }
            }

            /** @brief 칸 이름을 모읍니다. 규칙의 칸이 먼저라 원소가 없어도 구역이 선다. */
            static void collectNames( const DataInheritanceSchema& schema, const CatalogXMLText& parsed, optional<int32> ownerIndex, const XMLNode& flat, const XMLNode& parent, vector<string>& outListName )
            {
                for ( const DataInheritanceSchema& field : schema._listField )
                {
                    addUnique( outListName, field._name );
                }
                for ( const XMLNode* pNode : { &flat, &parent } )
                {
                    if ( pNode->isValid() == false )
                        continue;
                    for ( XMLNode child = pNode->findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        if ( isElement( child ) )
                            addUnique( outListName, child.getName() );
                    }
                }
                if ( ownerIndex.has_value() == false )
                    return;
                for ( const int32 childIndex : parsed.getElement( *ownerIndex )._listChild )
                {
                    addUnique( outListName, parsed.getElement( childIndex )._name );
                }
            }

            static void appendList( const DataInheritanceSchema& field, const CatalogXMLText& parsed, optional<int32> ownerIndex, const XMLNode& flat, const XMLNode& parent, CatalogChildSection&& section, vector<CatalogChildSection>& outListSection )
            {
                const utf8* pKey      = field._keyAttribute.c_str();
                section._kind         = CatalogChildSectionKind::List;
                section._keyAttribute = field._keyAttribute;
                addUnique( section._listColumn, field._keyAttribute );

                struct Row
                {
                    CatalogChildStep         _step;
                    const CatalogXMLElement* _pWritten;
                    XMLNode                  _flat;
                    XMLNode                  _parent;
                };
                vector<Row> listRow;
                const utf8* pName = section._name.c_str();
                for ( XMLNode child = flat.isValid() ? flat.findChild( pName ) : XMLNode{}; child.isValid(); child = child.findNextSibling( pName ) )
                {
                    CatalogChildStep step{ section._name, field._keyAttribute, string( child.getAttributeText( pKey ) ), 0 };
                    Row              row{ step, findWritten( parsed, ownerIndex, step ), child, parent.isValid() ? CatalogInheritanceView::walkPath( parent, CatalogChildPath{ step } ) : XMLNode{} };
                    // `_op="Replace"` 는 부모 칸을 물려받지 않는다.
                    if ( readWrittenAttribute( row._pWritten, DataInheritance::kOpAttribute ) == DataInheritance::kOpReplace )
                        row._parent = XMLNode{};
                    addNodeColumns( row._flat, section._listColumn );
                    addWrittenColumns( row._pWritten, section._listColumn );
                    listRow.push_back( std::move( row ) );
                }
                // 합친 결과에 없는 원소는 뺀 원소다 — 되돌릴 수 있게 끝에 보인다.
                vector<Row> listRemoved;
                if ( ownerIndex.has_value() )
                {
                    for ( const int32 childIndex : parsed.getElement( *ownerIndex )._listChild )
                    {
                        const CatalogXMLElement& written = parsed.getElement( childIndex );
                        if ( written._name != section._name || readWrittenAttribute( &written, DataInheritance::kOpAttribute ) != DataInheritance::kOpRemove )
                            continue;
                        CatalogChildStep step{ section._name, field._keyAttribute, string( readWrittenAttribute( &written, field._keyAttribute ) ), 0 };
                        listRemoved.push_back( Row{ step, &written, XMLNode{}, parent.isValid() ? CatalogInheritanceView::walkPath( parent, CatalogChildPath{ step } ) : XMLNode{} } );
                    }
                }

                const size_t sectionIndex = outListSection.size();
                const string ownerTitle   = section._title;
                outListSection.push_back( std::move( section ) );
                for ( const Row& row : listRow )
                {
                    CatalogChildSection& target = outListSection[sectionIndex];
                    CatalogChildNode     node;
                    node._path = target._ownerPath;
                    node._path.push_back( row._step );
                    node._bInherited = row._parent.isValid();
                    node._bMoved     = readWrittenAttribute( row._pWritten, DataInheritance::kBeforeAttribute ).empty() == false || readWrittenAttribute( row._pWritten, DataInheritance::kAfterAttribute ).empty() == false;
                    fillCells( target, row._pWritten, row._flat, row._parent, node );
                    target._listNode.push_back( std::move( node ) );
                }
                for ( const Row& row : listRemoved )
                {
                    CatalogChildSection& target = outListSection[sectionIndex];
                    CatalogChildNode     node;
                    node._path = target._ownerPath;
                    node._path.push_back( row._step );
                    node._bInherited = true;
                    node._bRemoved   = true;
                    fillCells( target, nullptr, row._parent, row._parent, node );
                    // 뺀 원소의 열쇠는 뺀 표시에 적혀 있다.
                    for ( CatalogChildCell& cell : node._listCell )
                    {
                        if ( cell._bKey )
                            cell._value = row._step._key;
                    }
                    target._listNode.push_back( std::move( node ) );
                }
                if ( field._listField.empty() )
                    return;
                for ( const Row& row : listRow )
                {
                    CatalogChildPath path = outListSection[sectionIndex]._ownerPath;
                    path.push_back( row._step );
                    appendSections( field, parsed, CatalogInheritanceView::findWrittenChild( parsed, ownerIndex, row._step ), row._flat, row._parent, path, ownerTitle + " " + row._step._key, outListSection );
                }
            }

            static void appendField( const DataInheritanceSchema& field, const CatalogXMLText& parsed, optional<int32> ownerIndex, const XMLNode& flat, const XMLNode& parent, CatalogChildSection&& section, vector<CatalogChildSection>& outListSection )
            {
                section._kind = CatalogChildSectionKind::Field;
                const CatalogChildStep   step{ section._name, string(), string(), 0 };
                const CatalogChildPath   stepPath{ step };
                const XMLNode            flatChild   = flat.isValid() ? CatalogInheritanceView::walkPath( flat, stepPath ) : XMLNode{};
                const XMLNode            parentChild = parent.isValid() ? CatalogInheritanceView::walkPath( parent, stepPath ) : XMLNode{};
                const CatalogXMLElement* pWritten    = findWritten( parsed, ownerIndex, step );
                addNodeColumns( flatChild, section._listColumn );
                addWrittenColumns( pWritten, section._listColumn );

                CatalogChildNode node;
                node._path = section._ownerPath;
                node._path.push_back( step );
                node._bInherited = parentChild.isValid();
                fillCells( section, pWritten, flatChild, parentChild, node );
                CatalogChildPath path  = node._path;
                const string     title = section._title;
                section._listNode.push_back( std::move( node ) );
                outListSection.push_back( std::move( section ) );
                appendSections( field, parsed, CatalogInheritanceView::findWrittenChild( parsed, ownerIndex, step ), flatChild, parentChild, path, title, outListSection );
            }

            static void appendWhole( const CatalogXMLText& parsed, optional<int32> ownerIndex, const XMLNode& flat, CatalogChildSection&& section, vector<CatalogChildSection>& outListSection )
            {
                section._kind = CatalogChildSectionKind::Whole;
                if ( ownerIndex.has_value() )
                {
                    for ( const int32 childIndex : parsed.getElement( *ownerIndex )._listChild )
                    {
                        const CatalogXMLElement& written = parsed.getElement( childIndex );
                        if ( written._name != section._name )
                            continue;
                        if ( readWrittenAttribute( &written, DataInheritance::kOpAttribute ) == DataCatalogInheritance::kOpClear )
                            section._bCleared = true;
                        else
                            section._bOverridden = true;
                    }
                }
                const utf8* pName = section._name.c_str();
                for ( XMLNode child = flat.isValid() ? flat.findChild( pName ) : XMLNode{}; child.isValid(); child = child.findNextSibling( pName ) )
                {
                    addNodeColumns( child, section._listColumn );
                }
                int32 index = 0;
                for ( XMLNode child = flat.isValid() ? flat.findChild( pName ) : XMLNode{}; child.isValid(); child = child.findNextSibling( pName ) )
                {
                    const CatalogChildStep step{ section._name, string(), string(), index };
                    CatalogChildNode       node;
                    node._path = section._ownerPath;
                    node._path.push_back( step );
                    node._bInherited = section._bOverridden == false;
                    fillCells( section, section._bOverridden ? findWritten( parsed, ownerIndex, step ) : nullptr, child, section._bOverridden ? XMLNode{} : child, node );
                    section._listNode.push_back( std::move( node ) );
                    ++index;
                }
                outListSection.push_back( std::move( section ) );
            }

            static void appendSections( const DataInheritanceSchema& schema, const CatalogXMLText& parsed, optional<int32> ownerIndex, const XMLNode& flat, const XMLNode& parent, const CatalogChildPath& ownerPath, const string& ownerTitle,
                                        vector<CatalogChildSection>& outListSection )
            {
                vector<string> listName;
                collectNames( schema, parsed, ownerIndex, flat, parent, listName );
                for ( const string& name : listName )
                {
                    CatalogChildSection section;
                    section._name                       = name;
                    section._title                      = ownerTitle.empty() ? name : string{ ownerTitle + " / " + name };
                    section._ownerPath                  = ownerPath;
                    section._bHasParent                 = parent.isValid();
                    const DataInheritanceSchema* pField = schema.findField( name );
                    if ( pField == nullptr )
                        appendWhole( parsed, ownerIndex, flat, std::move( section ), outListSection );
                    else if ( pField->_keyAttribute.empty() )
                        appendField( *pField, parsed, ownerIndex, flat, parent, std::move( section ), outListSection );
                    else
                        appendList( *pField, parsed, ownerIndex, flat, parent, std::move( section ), outListSection );
                }
            }
        };
    } // namespace

    CatalogInheritanceView::CatalogInheritanceView()
        : _schema{ string_view{} }
        , _flatDoc{}
        , _listParentFile{}
        , _rootBase{}
        , _error{}
        , _pSource{ nullptr }
        , _bActive{ SW_FALSE }
    {
    }

    void CatalogInheritanceView::rebuild( string_view text, const CatalogXMLText& parsed )
    {
        _bActive = SW_FALSE;
        _error.clear();
        _rootBase.clear();
        _listParentFile.clear();
        _flatDoc.clear();
        if ( _pSource == nullptr || parsed.isParsed() == false )
            return;
        const CatalogXMLElement& root    = parsed.getElement( parsed.getRootIndex() );
        bool                     bListed = false;
        if ( DataCatalogInheritance::loadSchema( root._name, _schema, bListed ) == false || bListed == false )
            return;
        _bActive = SW_TRUE;
        string flatText;
        if ( DataCatalogInheritance::flatten( text, root._name, _schema, *_pSource, flatText ) == false || _flatDoc.parse( flatText ) == false )
        {
            _error = "The inheritance could not be flattened - see the log";
            return;
        }
        const optional<uint32> rootBaseIndex = root.findAttribute( DataInheritance::kBaseAttribute );
        if ( rootBaseIndex.has_value() )
        {
            _rootBase = root._listAttribute[*rootBaseIndex]._value;
            addParentFile( _rootBase );
        }
        for ( const int32 rowIndex : parsed.getRowIndices() )
        {
            const CatalogXMLElement& row       = parsed.getElement( rowIndex );
            const optional<uint32>   baseIndex = row.findAttribute( DataInheritance::kBaseAttribute );
            if ( baseIndex.has_value() == false )
                continue;
            const string_view base      = row._listAttribute[*baseIndex]._value;
            const size_t      separator = base.find( DataCatalogInheritance::kFileSeparator );
            if ( separator != string_view::npos )
                addParentFile( base.substr( 0, separator ) );
        }
    }

    void CatalogInheritanceView::addParentFile( string_view assetID )
    {
        if ( VectorUtil::containsIf( _listParentFile, [assetID]( const ParentFile& file )
        { return string_view( file._assetID ) == assetID; } ) )
            return;
        string     text;
        string     flatText;
        ParentFile file;
        if ( _pSource->readText( assetID, text ) == false || DataCatalogInheritance::flatten( text, assetID, _schema, *_pSource, flatText ) == false || file._doc.parse( flatText, assetID ) == false )
        {
            _error = "A base file could not be read: ";
            _error += assetID;
            return;
        }
        file._assetID = string{ assetID };
        _listParentFile.push_back( std::move( file ) );
    }

    const string* CatalogInheritanceView::findKeyAttribute( string_view kind ) const
    {
        const DataInheritanceSchema* pKind = _bActive == SW_TRUE ? _schema.findField( kind ) : nullptr;
        return pKind != nullptr && pKind->_keyAttribute.empty() == false ? &pKind->_keyAttribute : nullptr;
    }

    XMLNode CatalogInheritanceView::findParentEntry( const CatalogXMLElement& row ) const
    {
        const string* pKey = findKeyAttribute( row._name );
        if ( pKey == nullptr )
            return {};
        const optional<uint32> baseIndex = row.findAttribute( DataInheritance::kBaseAttribute );
        string_view            filePart;
        string_view            entryPart;
        if ( baseIndex.has_value() )
        {
            const string_view base      = row._listAttribute[*baseIndex]._value;
            const size_t      separator = base.find( DataCatalogInheritance::kFileSeparator );
            filePart                    = separator != string_view::npos ? base.substr( 0, separator ) : string_view{};
            entryPart                   = separator != string_view::npos ? base.substr( separator + 1 ) : base;
        }
        else
        {
            // 항목이 `_base` 를 적지 않았으면 파일 상속의 같은 id 가 부모다.
            const optional<uint32> keyIndex = row.findAttribute( *pKey );
            if ( _rootBase.empty() || keyIndex.has_value() == false )
                return {};
            filePart  = _rootBase;
            entryPart = row._listAttribute[*keyIndex]._value;
        }
        XMLNode root;
        if ( filePart.empty() )
        {
            root = _flatDoc.getRoot();
        }
        else
        {
            const ParentFile* pFile = VectorUtil::findIf( _listParentFile, [filePart]( const ParentFile& file )
            { return string_view( file._assetID ) == filePart; } );
            root                    = pFile != nullptr ? pFile->_doc.getRoot() : XMLNode{};
        }
        if ( root.isValid() == false )
            return {};
        for ( XMLNode entry = root.findChild( row._name.c_str() ); entry.isValid(); entry = entry.findNextSibling( row._name.c_str() ) )
        {
            if ( entry.getAttributeText( pKey->c_str() ) == entryPart )
                return entry;
        }
        return {};
    }

    XMLNode CatalogInheritanceView::findFlatEntry( const CatalogXMLElement& row ) const
    {
        const string*          pKey     = findKeyAttribute( row._name );
        const optional<uint32> keyIndex = pKey != nullptr ? row.findAttribute( *pKey ) : nullopt;
        if ( keyIndex.has_value() == false || _flatDoc.getRoot().isValid() == false )
            return {};
        const string_view id = row._listAttribute[*keyIndex]._value;
        for ( XMLNode entry = _flatDoc.getRoot().findChild( row._name.c_str() ); entry.isValid(); entry = entry.findNextSibling( row._name.c_str() ) )
        {
            if ( entry.getAttributeText( pKey->c_str() ) == id )
                return entry;
        }
        return {};
    }

    const DataInheritanceSchema* CatalogInheritanceView::findChildSchema( const CatalogXMLElement& row, const CatalogChildPath& path ) const
    {
        const DataInheritanceSchema* pField = _bActive == SW_TRUE ? _schema.findField( row._name ) : nullptr;
        for ( const CatalogChildStep& step : path )
        {
            if ( pField == nullptr )
                return nullptr;
            pField = pField->findField( step._name );
        }
        return pField;
    }

    optional<int32> CatalogInheritanceView::findWrittenChild( const CatalogXMLText& parsed, optional<int32> ownerIndex, const CatalogChildStep& step )
    {
        if ( ownerIndex.has_value() == false )
            return nullopt;
        int32 occurrence = 0;
        for ( const int32 childIndex : parsed.getElement( *ownerIndex )._listChild )
        {
            const CatalogXMLElement& child = parsed.getElement( childIndex );
            if ( child._name != step._name )
                continue;
            if ( step._keyAttribute.empty() )
            {
                if ( occurrence == step._index )
                    return childIndex;
                ++occurrence;
                continue;
            }
            const optional<uint32> keyIndex = child.findAttribute( step._keyAttribute );
            if ( keyIndex.has_value() && child._listAttribute[*keyIndex]._value == step._key )
                return childIndex;
        }
        return nullopt;
    }

    XMLNode CatalogInheritanceView::walkPath( const XMLNode& start, const CatalogChildPath& path )
    {
        XMLNode node = start;
        for ( const CatalogChildStep& step : path )
        {
            if ( node.isValid() == false )
                return {};
            int32   occurrence = 0;
            XMLNode found;
            for ( XMLNode child = node.findChild( step._name.c_str() ); child.isValid(); child = child.findNextSibling( step._name.c_str() ) )
            {
                const bool bMatch = step._keyAttribute.empty() ? occurrence == step._index : child.getAttributeText( step._keyAttribute.c_str() ) == string_view( step._key );
                if ( bMatch )
                {
                    found = child;
                    break;
                }
                ++occurrence;
            }
            node = found;
        }
        return node;
    }

    void CatalogInheritanceView::collectChildSections( const CatalogXMLText& parsed, int32 rowIndex, vector<CatalogChildSection>& outListSection ) const
    {
        outListSection.clear();
        if ( _bActive == SW_FALSE || _error.empty() == false )
            return;
        const CatalogXMLElement&     row   = parsed.getElement( rowIndex );
        const DataInheritanceSchema* pKind = _schema.findField( row._name );
        const XMLNode                flat  = findFlatEntry( row );
        if ( pKind == nullptr || flat.isValid() == false )
            return;
        CatalogInheritanceViewInternal::appendSections( *pKind, parsed, rowIndex, flat, findParentEntry( row ), CatalogChildPath{}, string(), outListSection );
    }

    const utf8* CatalogInheritanceView::findInherited( const CatalogXMLElement& row, string_view column ) const
    {
        const XMLNode parent = findParentEntry( row );
        if ( parent.isValid() == false )
            return nullptr;
        const string columnName{ column };
        return parent.findAttribute( columnName.c_str() );
    }

    void CatalogInheritanceView::collectInheritedColumns( const CatalogXMLElement& row, vector<string>& inoutListColumn ) const
    {
        const XMLNode parent = findParentEntry( row );
        if ( parent.isValid() == false )
            return;
        for ( XMLAttribute attribute = parent.getFirstAttribute(); attribute; attribute = attribute.getNext() )
        {
            const string name{ attribute.getName() };
            if ( VectorUtil::contains( inoutListColumn, name ) == false )
                inoutListColumn.push_back( name );
        }
    }
} // namespace sw::editor
