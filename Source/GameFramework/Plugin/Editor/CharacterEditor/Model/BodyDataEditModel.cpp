#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyDataEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/AssetWriter.h"

namespace sw::editor
{
    BodyDataEditModel::BodyDataEditModel()
        : _doc{}
        , _text{}
        , _diskPath{}
        , _error{}
        , _state{}
        , _revision{ 0 }
    {
    }

    bool BodyDataEditModel::openText( string_view text, const DiskPath& diskPath )
    {
        if ( _doc.parseKeepingComments( text, diskPath.str() ) == false )
        {
            _error = _doc.getLastError();
            close();
            return false;
        }
        _text     = _doc.saveToString(); // 저장 모양으로 맞춘 글이 기준이다 — 읽기만 해서는 미저장이 아니다
        _diskPath = diskPath;
        _error.clear();
        _state.reset( _text );
        ++_revision;
        return true;
    }

    bool BodyDataEditModel::openFile( const DiskPath& diskPath )
    {
        if ( isOpen() && _diskPath == diskPath && isDirty() == false )
            return true;
        string text;
        if ( FileUtil::readTextFile( diskPath, text ) == false )
        {
            StringBuilder<constant::buffer::kMax512> error;
            error.appendFormat( "%#: cannot read the file", diskPath.str() );
            _error = string( error.view() );
            close();
            return false;
        }
        return openText( text, diskPath );
    }

    bool BodyDataEditModel::save( AssetWriter& writer )
    {
        if ( isOpen() == false || _diskPath.empty() )
            return false;
        if ( writer.writeText( _diskPath, _text ) == false )
        {
            StringBuilder<constant::buffer::kMax512> error;
            error.appendFormat( "%#: cannot write the file", _diskPath.str() );
            _error = string( error.view() );
            return false;
        }
        _state.markSaved( _text );
        _error.clear();
        return true;
    }

    void BodyDataEditModel::close()
    {
        _doc.clear();
        _text.clear();
        _diskPath = {};
        _state.reset( string{} );
        ++_revision;
    }

    XMLNode BodyDataEditModel::findElement( vector_reference<const BodyDataElementKey> path, bool bCreate ) const
    {
        XMLNode node = _doc.getRoot();
        for ( const BodyDataElementKey& key : path )
        {
            if ( node.isValid() == false )
                return XMLNode{};
            XMLNode found = node.findChild( key._element.c_str() );
            while ( found.isValid() && key._keyAttribute.empty() == false && StringUtil::equals( found.getAttributeText( key._keyAttribute.c_str() ), key._keyValue ) == false )
                found = found.findNextSibling( key._element.c_str() );
            if ( found.isValid() == false && bCreate )
            {
                found = node.appendChild( key._element.c_str() );
                if ( key._keyAttribute.empty() == false )
                    found.appendAttribute( key._keyAttribute.c_str(), string_view( key._keyValue ) );
            }
            node = found;
        }
        return node;
    }

    bool BodyDataEditModel::hasElement( vector_reference<const BodyDataElementKey> path ) const
    {
        return isOpen() && findElement( path, false ).isValid();
    }

    string BodyDataEditModel::readAttribute( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute ) const
    {
        const XMLNode node = isOpen() ? findElement( path, false ) : XMLNode{};
        return node.isValid() ? string( node.getAttributeText( pAttribute ) ) : string{};
    }

    bool BodyDataEditModel::setAttribute( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute, string_view value, bool bContinuous )
    {
        if ( isOpen() == false || path.empty() )
            return false;
        const XMLNode existing = findElement( path, false );
        if ( existing.isValid() && existing.hasAttribute( pAttribute ) && StringUtil::equals( existing.getAttributeText( pAttribute ), value ) )
            return false;
        const string  before = _text;
        const XMLNode node   = findElement( path, true );
        node.setAttribute( pAttribute, value );
        commit( before, bContinuous ? string_view( makeCoalesceKey( path, pAttribute ) ) : string_view{} );
        return true;
    }

    bool BodyDataEditModel::setAttributes( vector_reference<const BodyDataElementKey> path, vector_reference<const BodyDataAttribute> listAttribute )
    {
        if ( isOpen() == false || path.empty() )
            return false;
        const XMLNode existing = findElement( path, false );
        bool          bChange  = existing.isValid() == false;
        for ( const BodyDataAttribute& attribute : listAttribute )
            bChange = bChange || existing.hasAttribute( attribute._pName ) == false || StringUtil::equals( existing.getAttributeText( attribute._pName ), attribute._value ) == false;
        if ( bChange == false )
            return false;
        const string  before = _text;
        const XMLNode node   = findElement( path, true );
        for ( const BodyDataAttribute& attribute : listAttribute )
            node.setAttribute( attribute._pName, string_view( attribute._value ) );
        commit( before, string_view{} );
        return true;
    }

    bool BodyDataEditModel::ensureElement( vector_reference<const BodyDataElementKey> path )
    {
        if ( isOpen() == false || path.empty() || findElement( path, false ).isValid() )
            return false;
        const string before = _text;
        (void)findElement( path, true );
        commit( before, string_view{} );
        return true;
    }

    void BodyDataEditModel::commit( const string& before, string_view coalesceKey )
    {
        _state.record( before, coalesceKey );
        _text = _doc.saveToString();
        ++_revision;
    }

    bool BodyDataEditModel::reparse( const string& text )
    {
        if ( _doc.parseKeepingComments( text, _diskPath.str() ) == false )
        {
            _error = _doc.getLastError();
            return false;
        }
        ++_revision;
        return true;
    }

    bool BodyDataEditModel::undo()
    {
        if ( _state.undo( _text ) == false )
            return false;
        return reparse( _text );
    }

    bool BodyDataEditModel::redo()
    {
        if ( _state.redo( _text ) == false )
            return false;
        return reparse( _text );
    }

    string BodyDataEditModel::makeCoalesceKey( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute )
    {
        StringBuilder<constant::buffer::kMax256> key;
        for ( const BodyDataElementKey& part : path )
        {
            key.appendFormat( "%#[%#]/", part._element, part._keyValue );
        }
        key.append( pAttribute );
        return string( key.view() );
    }
} // namespace sw::editor
