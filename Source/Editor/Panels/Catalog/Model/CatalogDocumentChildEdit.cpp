#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @class CatalogChildDraft
         * @brief 자식 원소 편집 한 번의 초안 글입니다. 고칠 때마다 다시 훑고, 다 고친 글 하나를 문서에 넣어 되돌리기 한 단계가 됩니다.
         * @details 원소 번호는 문서 순서라 행의 안쪽을 고쳐도 행과 그 앞 원소의 번호는 그대로다. 경로의 원소는 고칠 때마다 다시 찾는다.
         *          실패 이유는 문서의 상태 글(`CatalogDocument::setError`)에 남긴다 — 편집 함수는 참 · 거짓만 돌려주고 패널이 그 글을 보인다.
         */
        class CatalogChildDraft
        {
        public:
            CatalogChildDraft( string_view text, int32 rowIndex, CatalogDocument& document )
                : _text{ text }
                , _parsed{}
                , _rowIndex{ rowIndex }
                , _document{ document }
            {
                (void)_parsed.parse( _text ); // 문서가 이미 훑은 글이다
            }

            const string&            getText() const { return _text; }
            const CatalogXMLElement& get( int32 index ) const { return _parsed.getElement( index ); }

            /** @brief 경로의 원소입니다. 빈 경로는 행 자신이고, 적지 않았으면 비어 있습니다. */
            [[nodiscard]] optional<int32> find( const CatalogChildPath& path ) const
            {
                optional<int32> index = _rowIndex;
                for ( const CatalogChildStep& step : path )
                {
                    index = CatalogInheritanceView::findWrittenChild( _parsed, index, step );
                }
                return index;
            }

            /** @brief 경로의 원소를 없으면 위부터 열쇠만 적어 만듭니다. 실패하면 비어 있고 이유는 문서의 상태 글에 있습니다. */
            [[nodiscard]] optional<int32> ensure( const CatalogChildPath& path )
            {
                int32 index = _rowIndex;
                for ( const CatalogChildStep& step : path )
                {
                    const optional<int32> written = CatalogInheritanceView::findWrittenChild( _parsed, index, step );
                    if ( written.has_value() )
                    {
                        index = *written;
                        continue;
                    }
                    vector<CatalogXMLNewAttribute> listAttribute;
                    if ( step._keyAttribute.empty() == false )
                        listAttribute.push_back( CatalogXMLNewAttribute{ step._keyAttribute, step._key } );
                    if ( appendChild( index, step._name, listAttribute ) == false )
                        return nullopt;
                    index = get( index )._listChild.back();
                }
                return index;
            }

            [[nodiscard]] bool appendChild( int32 ownerIndex, string_view name, const vector<CatalogXMLNewAttribute>& listAttribute )
            {
                return accept( CatalogXMLText::appendChildElement( _text, get( ownerIndex ), _parsed.getElements(), name, listAttribute ) );
            }

            /** @brief 원소의 원문을 그대로 @p ownerIndex 의 마지막 자식으로 옮깁니다. */
            [[nodiscard]] bool moveToEnd( int32 elementIndex )
            {
                const CatalogXMLElement& element    = get( elementIndex );
                const int32              ownerIndex = element._parent.value(); // 자식 경로의 원소라 루트가 아니다
                if ( get( ownerIndex )._listChild.back() == elementIndex )
                    return true;
                const string raw{ string_view( _text ).substr( element._begin, element._end - element._begin ) };
                if ( accept( CatalogXMLText::removeElement( _text, element ) ) == false )
                    return false;
                return accept( CatalogXMLText::appendChildText( _text, get( ownerIndex ), _parsed.getElements(), raw ) );
            }

            [[nodiscard]] bool setAttribute( int32 elementIndex, string_view name, string_view value ) { return accept( CatalogXMLText::setAttributeValue( _text, get( elementIndex ), name, value ) ); }

            /** @brief 칸이 없으면 할 일이 없습니다. */
            [[nodiscard]] bool removeAttribute( int32 elementIndex, string_view name )
            {
                if ( get( elementIndex ).findAttribute( name ).has_value() == false )
                    return true;
                return accept( CatalogXMLText::removeAttribute( _text, get( elementIndex ), name ) );
            }

            /** @brief 원소를 지웁니다. 담던 원소에 글이 남지 않으면 자기 닫힘으로 되돌립니다 — 더했다 지우면 원문이 처음과 같아야 한다. */
            [[nodiscard]] bool removeElement( int32 elementIndex )
            {
                const optional<int32> ownerIndex = get( elementIndex )._parent;
                if ( accept( CatalogXMLText::removeElement( _text, get( elementIndex ) ) ) == false )
                    return false;
                const CatalogXMLElement& owner = get( *ownerIndex ); // 지웠으니 루트가 아니었다
                if ( owner._bSelfClosing || owner._listChild.empty() == false )
                    return true;
                const string_view text  = _text;
                const string_view inner = text.substr( owner._startTagEnd, owner._closeBegin - owner._startTagEnd );
                if ( inner.find_first_not_of( " \t\r\n" ) != string_view::npos )
                    return true;
                string collapsed{ text.substr( 0, static_cast<size_t>( owner._startTagEnd ) - 1 ) };
                collapsed += "/>";
                const string_view tail = text.substr( owner._end );
                collapsed.append( tail.data(), tail.size() );
                return accept( std::move( collapsed ) );
            }

            /** @brief @p ownerIndex 아래 이름이 @p name 인 원소를 모두 지웁니다. */
            [[nodiscard]] bool removeGroup( int32 ownerIndex, string_view name )
            {
                for ( ;; )
                {
                    optional<int32> found;
                    for ( const int32 childIndex : get( ownerIndex )._listChild )
                    {
                        if ( string_view( get( childIndex )._name ) == name )
                            found = childIndex;
                    }
                    if ( found.has_value() == false )
                        return true;
                    if ( removeElement( *found ) == false )
                        return false;
                }
            }

            /** @brief 합친 원소 @p node 를 자식째 @p ownerIndex 아래에 적습니다. 글 값은 옮기지 못해 오류입니다. */
            [[nodiscard]] bool copyNode( int32 ownerIndex, const XMLNode& node )
            {
                const utf8* pText = node.getText();
                if ( pText != nullptr && pText[0] != '\0' )
                {
                    _document.setError( "an element with text cannot be overridden here - edit the raw text" );
                    return false;
                }
                vector<CatalogXMLNewAttribute> listAttribute;
                for ( XMLAttribute attribute = node.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    listAttribute.push_back( CatalogXMLNewAttribute{ string( attribute.getName() ), string( attribute.getValue() ) } );
                }
                if ( appendChild( ownerIndex, node.getName(), listAttribute ) == false )
                    return false;
                const int32 copied = get( ownerIndex )._listChild.back();
                for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                {
                    if ( child.getName() != nullptr && child.getName()[0] != '\0' && copyNode( copied, child ) == false )
                        return false;
                }
                return true;
            }

        private:
            /** @brief 고친 글을 받아 다시 훑습니다. 고치기나 훑기가 실패하면 문서에 이유를 남기고 false 입니다. */
            [[nodiscard]] bool accept( expected<string>&& edited )
            {
                if ( edited.has_value() == false )
                {
                    _document.setError( edited.error() );
                    return false;
                }
                return accept( std::move( *edited ) );
            }
            [[nodiscard]] bool accept( string&& edited )
            {
                _text                       = std::move( edited );
                const expected<void> parsed = _parsed.parse( _text );
                if ( parsed.has_value() == false )
                    _document.setError( parsed.error() );
                return parsed.has_value();
            }

        private:
            string           _text;
            CatalogXMLText   _parsed;
            int32            _rowIndex;
            CatalogDocument& _document;
        };

        struct CatalogDocumentChildEditInternal
        {
            static CatalogChildPath makeOwnerPath( const CatalogChildPath& path )
            {
                CatalogChildPath ownerPath = path;
                ownerPath.pop_back();
                return ownerPath;
            }

            static CatalogChildPath appendStep( const CatalogChildPath& path, const CatalogChildStep& step )
            {
                CatalogChildPath childPath = path;
                childPath.push_back( step );
                return childPath;
            }

            /** @brief 원소에 열쇠 말고 적은 것이 없으면 true 입니다. 작업 칸(`_before` 등)도 적은 것이다. */
            static bool isEmptyWritten( const CatalogXMLElement& element, const string& keyAttribute )
            {
                if ( element._listChild.empty() == false )
                    return false;
                for ( const CatalogXMLAttribute& attribute : element._listAttribute )
                {
                    if ( attribute._name != keyAttribute )
                        return false;
                }
                return true;
            }

            /** @brief 부모에 있는 원소가 적은 것 없이 비었으면 위로 올라가며 지웁니다 — 저장은 부모와 다른 칸만 쓴다. 이 항목이 더한 원소는 비어도 남긴다. */
            [[nodiscard]] static bool pruneEmpty( CatalogChildDraft& inoutDraft, const XMLNode& parentEntry, const CatalogChildPath& path )
            {
                CatalogChildPath prefix = path;
                while ( prefix.empty() == false )
                {
                    const optional<int32> index = inoutDraft.find( prefix );
                    if ( index.has_value() )
                    {
                        if ( isEmptyWritten( inoutDraft.get( *index ), prefix.back()._keyAttribute ) == false || parentEntry.isValid() == false || CatalogInheritanceView::walkPath( parentEntry, prefix ).isValid() == false )
                            return true;
                        if ( inoutDraft.removeElement( *index ) == false )
                            return false;
                    }
                    prefix.pop_back();
                }
                return true;
            }

            /** @brief 규칙에 없는 칸에 적은 원소가 부모와 같으면 true 입니다(칸 값만 본다. 자식이 있으면 같다고 보지 않는다). */
            static bool isGroupSameAsParent( const CatalogChildDraft& draft, int32 ownerIndex, const XMLNode& parentOwner, const string& name )
            {
                if ( parentOwner.isValid() == false )
                    return false;
                XMLNode parentChild = parentOwner.findChild( name.c_str() );
                for ( const int32 childIndex : draft.get( ownerIndex )._listChild )
                {
                    const CatalogXMLElement& written = draft.get( childIndex );
                    if ( written._name != name )
                        continue;
                    if ( parentChild.isValid() == false || written._listChild.empty() == false || parentChild.findChild().isValid() )
                        return false;
                    size_t parentCount = 0;
                    for ( XMLAttribute attribute = parentChild.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                    {
                        ++parentCount;
                        const optional<uint32> writtenAttribute = written.findAttribute( attribute.getName() );
                        if ( writtenAttribute.has_value() == false || string_view( written._listAttribute[*writtenAttribute]._value ) != string_view( attribute.getValue() ) )
                            return false;
                    }
                    if ( parentCount != written._listAttribute.size() )
                        return false;
                    parentChild = parentChild.findNextSibling( name.c_str() );
                }
                return parentChild.isValid() == false;
            }
        };
    } // namespace

    bool CatalogDocument::commitChildEdit( string_view text )
    {
        const bool bHadError = _inheritance.getError().empty() == false;
        if ( replaceTextChecked( text ) == false )
            return false;
        if ( bHadError || _inheritance.getError().empty() )
            return true;
        const string reason = _inheritance.getError();
        undo();
        _state.discardLastRedo();
        _error = reason;
        return false;
    }

    bool CatalogDocument::checkChildRow( int32 rowIndex )
    {
        if ( canCreateVariant( rowIndex ) )
            return true;
        _error = "only a row of a catalog listed in the inheritance rules has child fields";
        return false;
    }

    bool CatalogDocument::setChildCell( int32 rowIndex, const CatalogChildPath& path, string_view column, string_view value )
    {
        using Internal = CatalogDocumentChildEditInternal;
        if ( checkChildRow( rowIndex ) == false || path.empty() )
            return false;
        const CatalogXMLElement& row         = _parsed.getElement( rowIndex );
        const XMLNode            parentEntry = _inheritance.findParentEntry( row );
        CatalogChildDraft        draft{ _text, rowIndex, *this };
        const CatalogChildStep&  leaf    = path.back();
        const optional<int32>    written = draft.find( path );

        if ( _inheritance.findChildSchema( row, path ) == nullptr )
        {
            // 규칙에 없는 칸은 그 이름 전체가 한 값이다 — 부모와 같아지면 전체를 지운다.
            if ( written.has_value() == false )
            {
                _error = "override the inherited elements before editing them";
                return false;
            }
            if ( draft.setAttribute( *written, column, value ) == false )
                return false;
            const CatalogChildPath ownerPath  = Internal::makeOwnerPath( path );
            const int32            ownerIndex = draft.find( ownerPath ).value(); // 적은 원소의 담는 원소다
            if ( Internal::isGroupSameAsParent( draft, ownerIndex, parentEntry.isValid() ? CatalogInheritanceView::walkPath( parentEntry, ownerPath ) : XMLNode{}, leaf._name ) )
            {
                if ( draft.removeGroup( ownerIndex, leaf._name ) == false || Internal::pruneEmpty( draft, parentEntry, ownerPath ) == false )
                    return false;
            }
            return commitChildEdit( draft.getText() );
        }

        const XMLNode parentNode = parentEntry.isValid() ? CatalogInheritanceView::walkPath( parentEntry, path ) : XMLNode{};
        const bool    bKey       = leaf._keyAttribute.empty() == false && column == string_view( leaf._keyAttribute );
        if ( bKey && parentNode.isValid() )
        {
            _error = "the key of an inherited element cannot be changed";
            return false;
        }
        const optional<uint32> opIndex   = written.has_value() ? draft.get( *written ).findAttribute( DataInheritance::kOpAttribute ) : nullopt;
        const bool             bReplaced = opIndex.has_value() && draft.get( *written )._listAttribute[*opIndex]._value == DataInheritance::kOpReplace;
        const string           columnName{ column };
        const utf8*            pInherited = parentNode.isValid() && bReplaced == false && bKey == false ? parentNode.findAttribute( columnName.c_str() ) : nullptr;
        if ( pInherited != nullptr && value == string_view( pInherited ) )
        {
            if ( written.has_value() == false || draft.get( *written ).findAttribute( column ).has_value() == false )
                return true;
            if ( draft.removeAttribute( *written, column ) == false || Internal::pruneEmpty( draft, parentEntry, path ) == false )
                return false;
            return commitChildEdit( draft.getText() );
        }
        const optional<int32> target = draft.ensure( path );
        if ( target.has_value() == false || draft.setAttribute( *target, column, value ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::unsetChildCell( int32 rowIndex, const CatalogChildPath& path, string_view column )
    {
        if ( checkChildRow( rowIndex ) == false || path.empty() )
            return false;
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> written = draft.find( path );
        if ( written.has_value() == false || draft.get( *written ).findAttribute( column ).has_value() == false )
        {
            _error = "the element has no such attribute";
            return false;
        }
        if ( draft.removeAttribute( *written, column ) == false || CatalogDocumentChildEditInternal::pruneEmpty( draft, _inheritance.findParentEntry( _parsed.getElement( rowIndex ) ), path ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::addChildNode( int32 rowIndex, const CatalogChildPath& ownerPath, string_view name, string_view keyAttribute )
    {
        if ( checkChildRow( rowIndex ) == false )
            return false;
        const CatalogXMLElement& row = _parsed.getElement( rowIndex );
        CatalogChildDraft        draft{ _text, rowIndex, *this };
        const string             nameText{ name };
        if ( keyAttribute.empty() == false )
        {
            const string  keyName{ keyAttribute };
            const XMLNode flatOwner = CatalogInheritanceView::walkPath( _inheritance.findFlatEntry( row ), ownerPath );
            const auto    isTaken   = [&]( const string& candidate )
            {
                for ( XMLNode child = flatOwner.isValid() ? flatOwner.findChild( nameText.c_str() ) : XMLNode{}; child.isValid(); child = child.findNextSibling( nameText.c_str() ) )
                {
                    if ( child.getAttributeText( keyName.c_str() ) == string_view( candidate ) )
                        return true;
                }
                return false;
            };
            string key = "new";
            for ( uint64 suffix = 2; isTaken( key ); ++suffix )
            {
                key = "new_" + to_string( suffix );
            }
            const optional<int32> ownerIndex = draft.ensure( ownerPath );
            if ( ownerIndex.has_value() == false || draft.appendChild( *ownerIndex, name, {
                                                                                              CatalogXMLNewAttribute{ keyName, key }
            } ) == false )
                return false;
            return commitChildEdit( draft.getText() );
        }

        // 규칙에 없는 칸: 적으면 그 이름 전체가 바뀌므로 물려받은 원소를 먼저 옮기고 더한다.
        const XMLNode         parentEntry  = _inheritance.findParentEntry( row );
        const XMLNode         parentOwner  = parentEntry.isValid() ? CatalogInheritanceView::walkPath( parentEntry, ownerPath ) : XMLNode{};
        const optional<int32> ensuredOwner = draft.ensure( ownerPath );
        if ( ensuredOwner.has_value() == false )
            return false;
        const int32                    ownerIndex = *ensuredOwner;
        vector<CatalogXMLNewAttribute> listAttribute;
        bool                           bWrote   = false;
        bool                           bCleared = false;
        for ( const int32 childIndex : draft.get( ownerIndex )._listChild )
        {
            const CatalogXMLElement& child = draft.get( childIndex );
            if ( child._name != nameText )
                continue;
            if ( child.findAttribute( DataInheritance::kOpAttribute ).has_value() )
            {
                bCleared = true;
                continue;
            }
            bWrote = true;
            listAttribute.clear();
            for ( const CatalogXMLAttribute& attribute : child._listAttribute )
            {
                listAttribute.push_back( CatalogXMLNewAttribute{ attribute._name, attribute._value } );
            }
        }
        if ( bCleared && draft.removeGroup( ownerIndex, name ) == false )
            return false;
        if ( bWrote == false && bCleared == false && parentOwner.isValid() )
        {
            for ( XMLNode child = parentOwner.findChild( nameText.c_str() ); child.isValid(); child = child.findNextSibling( nameText.c_str() ) )
            {
                if ( draft.copyNode( ownerIndex, child ) == false )
                    return false;
                listAttribute.clear();
                for ( XMLAttribute attribute = child.getFirstAttribute(); attribute; attribute = attribute.getNext() )
                {
                    listAttribute.push_back( CatalogXMLNewAttribute{ string( attribute.getName() ), string( attribute.getValue() ) } );
                }
            }
        }
        if ( draft.appendChild( ownerIndex, name, listAttribute ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::removeChildNode( int32 rowIndex, const CatalogChildPath& path )
    {
        using Internal = CatalogDocumentChildEditInternal;
        if ( checkChildRow( rowIndex ) == false || path.empty() )
            return false;
        const CatalogXMLElement& row         = _parsed.getElement( rowIndex );
        const XMLNode            parentEntry = _inheritance.findParentEntry( row );
        CatalogChildDraft        draft{ _text, rowIndex, *this };
        const CatalogChildPath   ownerPath = Internal::makeOwnerPath( path );
        const optional<int32>    written   = draft.find( path );

        if ( _inheritance.findChildSchema( row, path ) == nullptr )
        {
            if ( written.has_value() == false )
            {
                _error = "override the inherited elements before removing one";
                return false;
            }
            if ( draft.removeElement( *written ) == false )
                return false;
            // 마지막 원소를 지우면 다시 물려받게 된다 - 지운 뜻대로 비운다.
            const int32   ownerIndex  = draft.find( ownerPath ).value(); // 지운 원소를 담던 원소다
            const XMLNode parentOwner = parentEntry.isValid() ? CatalogInheritanceView::walkPath( parentEntry, ownerPath ) : XMLNode{};
            const bool    bAnyLeft    = VectorUtil::containsIf( draft.get( ownerIndex )._listChild, [&]( int32 childIndex )
                  { return draft.get( childIndex )._name == path.back()._name; } );
            if ( bAnyLeft == false && parentOwner.isValid() && parentOwner.findChild( path.back()._name.c_str() ).isValid() &&
                 draft.appendChild( ownerIndex, path.back()._name, {
                                                                       CatalogXMLNewAttribute{ DataInheritance::kOpAttribute, DataCatalogInheritance::kOpClear }
            } ) == false )
                return false;
            return commitChildEdit( draft.getText() );
        }

        const bool bInherited = parentEntry.isValid() && CatalogInheritanceView::walkPath( parentEntry, path ).isValid();
        if ( written.has_value() && draft.removeElement( *written ) == false )
            return false;
        if ( bInherited )
        {
            const optional<int32> target = draft.ensure( path );
            if ( target.has_value() == false || draft.setAttribute( *target, DataInheritance::kOpAttribute, DataInheritance::kOpRemove ) == false )
                return false;
        }
        else if ( Internal::pruneEmpty( draft, parentEntry, ownerPath ) == false )
        {
            return false;
        }
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::restoreChildNode( int32 rowIndex, const CatalogChildPath& path )
    {
        if ( checkChildRow( rowIndex ) == false || path.empty() )
            return false;
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> written = draft.find( path );
        if ( written.has_value() == false || draft.removeElement( *written ) == false )
            return false;
        if ( CatalogDocumentChildEditInternal::pruneEmpty( draft, _inheritance.findParentEntry( _parsed.getElement( rowIndex ) ), CatalogDocumentChildEditInternal::makeOwnerPath( path ) ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::moveChildNode( int32 rowIndex, const CatalogChildPath& path, bool bEarlier )
    {
        if ( checkChildRow( rowIndex ) == false || path.empty() || path.back()._keyAttribute.empty() )
            return false;
        const CatalogChildStep& leaf      = path.back();
        const XMLNode           flatOwner = CatalogInheritanceView::walkPath( _inheritance.findFlatEntry( _parsed.getElement( rowIndex ) ), CatalogDocumentChildEditInternal::makeOwnerPath( path ) );
        vector<string>          listKey;
        for ( XMLNode child = flatOwner.isValid() ? flatOwner.findChild( leaf._name.c_str() ) : XMLNode{}; child.isValid(); child = child.findNextSibling( leaf._name.c_str() ) )
        {
            listKey.push_back( string( child.getAttributeText( leaf._keyAttribute.c_str() ) ) );
        }
        size_t position = 0;
        while ( position < listKey.size() && listKey[position] != leaf._key )
        {
            ++position;
        }
        if ( position >= listKey.size() )
        {
            _error = "the element to move is not in the merged list";
            return false;
        }
        if ( ( bEarlier && position == 0 ) || ( bEarlier == false && position + 1 == listKey.size() ) )
            return true;

        CatalogChildDraft draft{ _text, rowIndex, *this };
        // 아래 `find( path ).value()` 는 방금 만든(있는) 원소를 고칠 때마다 다시 짚는다.
        const optional<int32> ensured = draft.ensure( path );
        if ( ensured.has_value() == false || draft.removeAttribute( *ensured, DataInheritance::kBeforeAttribute ) == false || draft.removeAttribute( draft.find( path ).value(), DataInheritance::kAfterAttribute ) == false )
            return false;
        // 위에서 아래로 차례로 얹으므로 맨 뒤로 옮겨 적어야 기준 열쇠가 그때 이미 있다(이 항목이 더한 원소여도).
        if ( draft.moveToEnd( draft.find( path ).value() ) == false )
            return false;
        const int32 target = draft.find( path ).value();
        const bool  bSet   = bEarlier ? draft.setAttribute( target, DataInheritance::kBeforeAttribute, listKey[position - 1] ) : draft.setAttribute( target, DataInheritance::kAfterAttribute, listKey[position + 1] );
        return bSet && commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::resetChildOrder( int32 rowIndex, const CatalogChildPath& path )
    {
        if ( checkChildRow( rowIndex ) == false || path.empty() )
            return false;
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> written = draft.find( path );
        if ( written.has_value() == false )
            return true;
        if ( draft.removeAttribute( *written, DataInheritance::kBeforeAttribute ) == false || draft.removeAttribute( draft.find( path ).value(), DataInheritance::kAfterAttribute ) == false )
            return false;
        if ( CatalogDocumentChildEditInternal::pruneEmpty( draft, _inheritance.findParentEntry( _parsed.getElement( rowIndex ) ), path ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::clearChildGroup( int32 rowIndex, const CatalogChildPath& ownerPath, string_view name )
    {
        if ( checkChildRow( rowIndex ) == false )
            return false;
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> ownerIndex = draft.ensure( ownerPath );
        if ( ownerIndex.has_value() == false || draft.removeGroup( *ownerIndex, name ) == false )
            return false;
        if ( draft.appendChild( *ownerIndex, name, {
                                                       CatalogXMLNewAttribute{ DataInheritance::kOpAttribute, DataCatalogInheritance::kOpClear }
        } ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::inheritChildGroup( int32 rowIndex, const CatalogChildPath& ownerPath, string_view name )
    {
        if ( checkChildRow( rowIndex ) == false )
            return false;
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> ownerIndex = draft.find( ownerPath );
        if ( ownerIndex.has_value() == false )
            return true;
        if ( draft.removeGroup( *ownerIndex, name ) == false || CatalogDocumentChildEditInternal::pruneEmpty( draft, _inheritance.findParentEntry( _parsed.getElement( rowIndex ) ), ownerPath ) == false )
            return false;
        return commitChildEdit( draft.getText() );
    }

    bool CatalogDocument::overrideChildGroup( int32 rowIndex, const CatalogChildPath& ownerPath, string_view name )
    {
        if ( checkChildRow( rowIndex ) == false )
            return false;
        const string  nameText{ name };
        const XMLNode parentEntry = _inheritance.findParentEntry( _parsed.getElement( rowIndex ) );
        const XMLNode parentOwner = parentEntry.isValid() ? CatalogInheritanceView::walkPath( parentEntry, ownerPath ) : XMLNode{};
        if ( parentOwner.isValid() == false || parentOwner.findChild( nameText.c_str() ).isValid() == false )
        {
            _error = "there is nothing to override";
            return false;
        }
        CatalogChildDraft     draft{ _text, rowIndex, *this };
        const optional<int32> ownerIndex = draft.ensure( ownerPath );
        if ( ownerIndex.has_value() == false || draft.removeGroup( *ownerIndex, name ) == false )
            return false;
        for ( XMLNode child = parentOwner.findChild( nameText.c_str() ); child.isValid(); child = child.findNextSibling( nameText.c_str() ) )
        {
            if ( draft.copyNode( *ownerIndex, child ) == false )
                return false;
        }
        return commitChildEdit( draft.getText() );
    }
} // namespace sw::editor
