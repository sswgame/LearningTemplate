#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogTableEdit.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Panels/Catalog/Model/CatalogCurve.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

namespace sw::editor
{
    namespace
    {
        struct CatalogTableEditInternal
        {
            static string describe( string_view verb, string_view groupName, string_view column )
            {
                StringBuilder<constant::buffer::kMax256> text;
                if ( column.empty() )
                    text.appendFormat( "%# %#", verb, groupName );
                else
                    text.appendFormat( "%# %#.%#", verb, groupName, column );
                return string( text.view() );
            }
        };
    } // namespace

    bool CatalogTableEdit::apply( CatalogDocument& inoutDocument, string& outDescription ) const
    {
        using Internal = CatalogTableEditInternal;
        // 편집이 원소 인덱스를 바꾸므로 설명에 쓸 이름은 먼저 읽는다.
        string groupName;
        if ( _element >= 0 && static_cast<size_t>( _element ) < inoutDocument.getParsed().getElements().size() )
            groupName = inoutDocument.getParsed().getElement( _element )._name;

        bool bChanged = false;
        switch ( _kind )
        {
            case Kind::None:
            {
                outDescription.clear();
                return false;
            }
            case Kind::SetCell:
            {
                bChanged       = inoutDocument.setCell( _element, _column, _text );
                outDescription = Internal::describe( "Edited", groupName, _column );
                break;
            }
            case Kind::UnsetCell:
            {
                bChanged       = inoutDocument.unsetCell( _element, _column );
                outDescription = Internal::describe( "Unset", groupName, _column );
                break;
            }
            case Kind::AddRow:
            {
                bChanged       = inoutDocument.addRow( _text );
                outDescription = Internal::describe( "Added row to", _text, string_view() );
                break;
            }
            case Kind::RemoveRow:
            {
                bChanged       = inoutDocument.removeRow( _element );
                outDescription = Internal::describe( "Removed row from", groupName, string_view() );
                break;
            }
            case Kind::DuplicateRow:
            {
                bChanged       = inoutDocument.duplicateRow( _element );
                outDescription = Internal::describe( "Duplicated row in", groupName, string_view() );
                break;
            }
            case Kind::CreateVariant:
            {
                bChanged       = inoutDocument.createVariant( _element );
                outDescription = Internal::describe( "Created a variant in", groupName, string_view() );
                break;
            }
            case Kind::AddCurveKey:
            {
                bChanged       = CatalogCurve::addKey( inoutDocument, _element );
                outDescription = Internal::describe( "Added a key to", groupName, string_view() );
                break;
            }
            case Kind::SetCurveKeys:
            {
                bChanged       = CatalogCurve::applyEditedCurve( inoutDocument, _element, _curve );
                outDescription = Internal::describe( "Edited the keys of", groupName, string_view() );
                break;
            }
            case Kind::SetChildCell:
            {
                bChanged       = inoutDocument.setChildCell( _element, _path, _column, _text );
                outDescription = Internal::describe( "Edited child of", groupName, _column );
                break;
            }
            case Kind::UnsetChildCell:
            {
                bChanged       = inoutDocument.unsetChildCell( _element, _path, _column );
                outDescription = Internal::describe( "Unset child of", groupName, _column );
                break;
            }
            case Kind::AddChildNode:
            {
                bChanged       = inoutDocument.addChildNode( _element, _path, _column, _text );
                outDescription = Internal::describe( "Added child to", groupName, _column );
                break;
            }
            case Kind::RemoveChildNode:
            {
                bChanged       = inoutDocument.removeChildNode( _element, _path );
                outDescription = Internal::describe( "Removed child of", groupName, string_view() );
                break;
            }
            case Kind::RestoreChildNode:
            {
                bChanged       = inoutDocument.restoreChildNode( _element, _path );
                outDescription = Internal::describe( "Restored child of", groupName, string_view() );
                break;
            }
            case Kind::MoveChildEarlier:
            case Kind::MoveChildLater:
            {
                bChanged       = inoutDocument.moveChildNode( _element, _path, _kind == Kind::MoveChildEarlier );
                outDescription = Internal::describe( "Moved child of", groupName, string_view() );
                break;
            }
            case Kind::ResetChildOrder:
            {
                bChanged       = inoutDocument.resetChildOrder( _element, _path );
                outDescription = Internal::describe( "Reset the order of child of", groupName, string_view() );
                break;
            }
            case Kind::ClearChildGroup:
            {
                bChanged       = inoutDocument.clearChildGroup( _element, _path, _column );
                outDescription = Internal::describe( "Cleared children of", groupName, _column );
                break;
            }
            case Kind::InheritChildGroup:
            {
                bChanged       = inoutDocument.inheritChildGroup( _element, _path, _column );
                outDescription = Internal::describe( "Inherited children of", groupName, _column );
                break;
            }
            case Kind::OverrideChildGroup:
            {
                bChanged       = inoutDocument.overrideChildGroup( _element, _path, _column );
                outDescription = Internal::describe( "Overrode children of", groupName, _column );
                break;
            }
        }
        if ( bChanged == false )
            outDescription = inoutDocument.getError();
        return bChanged;
    }
} // namespace sw::editor
