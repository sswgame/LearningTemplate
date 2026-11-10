#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/CodeGenerator.h"

namespace sw
{
    FixedCodecBound::FixedCodecBound() noexcept
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

    FixedCodecNode::FixedCodecNode() noexcept
        : _call{}
        , _listChild{}
        , _childArgs{ ChildArgs::None }
    {
    }

    FixedCodecNode::FixedCodecNode( string call, const ChildArgs childArgs ) noexcept
        : _call{ std::move( call ) }
        , _listChild{}
        , _childArgs{ childArgs }
    {
    }
} // namespace sw

namespace sw
{
    FixedCodecEmitState::FixedCodecEmitState( const ParsedHeader& header, const string& sourcePath, const ParsedTypeInfo& root )
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

    string FixedCodecEmitState::formatFloat( const float64 value )
    {
        utf8 arrBuffer[constant::buffer::kMax64]{};
        formatstring( arrBuffer, sizeof( arrBuffer ), "%.9g", value );
        return string( arrBuffer );
    }

    string FixedCodecEmitState::formatInteger( const int64 value )
    {
        StringBuilder<constant::buffer::kMax64> b;
        b.appendFormat( "%#", value );
        return string( b.view() );
    }

    string FixedCodecEmitState::makeEnumValueTableName( const ParsedFixedEnum& enumInfo )
    {
        return "s_arrFixedEnum_" + CodeGenerator::sanitizeIdentifier( enumInfo._fullyQualifiedName );
    }

    void FixedCodecEmitState::reportError( const ParsedPropertyInfo& prop, const string_view message )
    {
        SW_LOG_ERROR_ID( "FixedCodecGenerator.ERRORFixedLayout", "ERROR: %#(%#): fixed layout of '%#::%#': %#", *_pSourcePath, prop._line, _pRoot->_fullyQualifiedName,
                         prop._memberName, message );
        _bError = SW_TRUE;
    }

    void FixedCodecEmitState::markIneligible( const ParsedPropertyInfo& prop, const string_view reason )
    {
        if ( _reason.empty() )
            _reason = "field '" + prop._memberName + "': " + string( reason );
    }

    uint32 FixedCodecEmitState::addDependency( const string& fieldName, const string& fqn )
    {
        for ( size_t index = 0; index < _listDependency.size(); ++index )
        {
            if ( _listDependency[index].second == fqn )
                return static_cast<uint32>( index );
        }
        _listDependency.emplace_back( fieldName, fqn );
        return static_cast<uint32>( _listDependency.size() - 1 );
    }

    void FixedCodecEmitState::addEnum( const ParsedFixedEnum& enumInfo )
    {
        for ( const ParsedFixedEnum* pEnum : _listEnum )
        {
            if ( pEnum->_fullyQualifiedName == enumInfo._fullyQualifiedName )
                return;
        }
        _listEnum.push_back( &enumInfo );
    }
} // namespace sw
