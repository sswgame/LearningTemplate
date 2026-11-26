#include "pch.h"

#include "Programs/ReflectionParser/LayoutSchemaEmitState.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/CodeGenerator.h"

namespace sw
{
    LayoutSchemaBound::LayoutSchemaBound() noexcept
        : _text{}
        , _ownerFqn{}
        , _fieldName{}
        , _value{ 0.0 }
        , _scopeDepth{ 0 }
        , _bPresent{ SW_FALSE }
        , _bLiteral{ SW_FALSE }
        , _bNegate{ SW_FALSE }
    {
    }

    LayoutSchemaNode::LayoutSchemaNode() noexcept
        : _call{}
        , _listChild{}
        , _childArgs{ ChildArgs::None }
    {
    }

    LayoutSchemaNode::LayoutSchemaNode( string call, const ChildArgs childArgs ) noexcept
        : _call{ std::move( call ) }
        , _listChild{}
        , _childArgs{ childArgs }
    {
    }
} // namespace sw

namespace sw
{
    LayoutSchemaEmitState::LayoutSchemaEmitState( const ParsedHeader& header, const string& sourcePath, const ParsedTypeInfo& root )
        : _pHeader{ &header }
        , _pSourcePath{ &sourcePath }
        , _pRoot{ &root }
        , _listRootNode{}
        , _listDependency{}
        , _listEnum{}
        , _listScope{}
        , _listInlineFqn{}
        , _listResolvedFqn{}
        , _reason{}
        , _unresolvedName{}
        , _unresolvedMember{}
        , _unresolvedLine{ 0 }
        , _bError{ SW_FALSE }
    {
    }

    string LayoutSchemaEmitState::formatFloat( const float64 value )
    {
        utf8 arrBuffer[constant::buffer::kMax64]{};
        formatstring( arrBuffer, sizeof( arrBuffer ), "%.9g", value );
        return string( arrBuffer );
    }

    string LayoutSchemaEmitState::formatInteger( const int64 value )
    {
        StringBuilder<constant::buffer::kMax64> b;
        b.appendFormat( "%#", value );
        return string( b.view() );
    }

    string LayoutSchemaEmitState::makeEnumValueTableName( const ParsedLayoutEnum& enumInfo )
    {
        StringBuilder<constant::buffer::kMax256> b;
        b.appendFormat( "s_arrLayoutEnum_%#", CodeGenerator::sanitizeIdentifier( enumInfo._fullyQualifiedName ) );
        return string( b.view() );
    }

    void LayoutSchemaEmitState::reportError( const ParsedPropertyInfo& prop, const string_view message )
    {
        SW_LOG_ERROR_ID( "LayoutSchemaGenerator.ERRORLayoutSchema", "ERROR: %#(%#): layout schema of '%#::%#': %#", *_pSourcePath, prop._line, _pRoot->_fullyQualifiedName,
                         prop._memberName, message );
        _bError = SW_TRUE;
    }

    void LayoutSchemaEmitState::markIneligible( const ParsedPropertyInfo& prop, const string_view reason )
    {
        if ( _reason.empty() == false )
            return;
        StringBuilder<constant::buffer::kMax512> b;
        b.appendFormat( "field '%#': %#", prop._memberName, reason );
        _reason = string( b.view() );
    }

    uint32 LayoutSchemaEmitState::addDependency( const string& fieldName, const string& fqn )
    {
        for ( size_t index = 0; index < _listDependency.size(); ++index )
        {
            if ( _listDependency[index].second == fqn )
                return static_cast<uint32>( index );
        }
        _listDependency.emplace_back( fieldName, fqn );
        return static_cast<uint32>( _listDependency.size() - 1 );
    }

    void LayoutSchemaEmitState::addEnum( const ParsedLayoutEnum& enumInfo )
    {
        for ( const ParsedLayoutEnum* pEnum : _listEnum )
        {
            if ( pEnum->_fullyQualifiedName == enumInfo._fullyQualifiedName )
                return;
        }
        _listEnum.push_back( &enumInfo );
    }
} // namespace sw
