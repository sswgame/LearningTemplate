#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/CodeGenerator.h"

namespace sw
{
    FixedCodecBound::FixedCodecBound() noexcept
        : _text{}
        , _expr{}
        , _value{ 0.0 }
        , _bPresent{ SW_FALSE }
        , _bLiteral{ SW_FALSE }
    {
    }
} // namespace sw

namespace sw
{
    FixedCodecEmitState::FixedCodecEmitState( const ParsedHeader& header, const string& sourcePath, const ParsedTypeInfo& root )
        : _pHeader{ &header }
        , _pSourcePath{ &sourcePath }
        , _pRoot{ &root }
        , _writeBuffer{}
        , _readBuffer{}
        , _equalsBuffer{}
        , _write{ _writeBuffer }
        , _read{ _readBuffer }
        , _equals{ _equalsBuffer }
        , _canonical{}
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

    string FixedCodecEmitState::makeEnumHelperName( const ParsedFixedEnum& enumInfo )
    {
        return "isFixedEnumValue_" + CodeGenerator::sanitizeIdentifier( enumInfo._fullyQualifiedName );
    }

    string FixedCodecEmitState::makeCodecAccessorName( const string& fqn )
    {
        return "getFixedCodec_" + CodeGenerator::sanitizeIdentifier( fqn );
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

    void FixedCodecEmitState::addDependency( const string& fieldName, const string& fqn )
    {
        for ( const pair<string, string>& dependency : _listDependency )
        {
            if ( dependency.second == fqn )
                return;
        }
        _listDependency.emplace_back( fieldName, fqn );
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

    void FixedCodecEmitState::emitReadCheck( const string& call )
    {
        _read.linef( "result = %#;", call );
        _read.linef( "if ( result != %# )", FixedCodecEmitState::kReadOk );
        _read.push().line( "return result;" ).pop();
    }

    void FixedCodecEmitState::emitWriteCheck( const string& call )
    {
        _write.linef( "if ( %# == false )", call );
        _write.push().line( "return false;" ).pop();
    }

    void FixedCodecEmitState::emitEqualsCheck( const string& condition )
    {
        _equals.linef( "if ( %# )", condition );
        _equals.push().line( "return false;" ).pop();
    }
} // namespace sw
