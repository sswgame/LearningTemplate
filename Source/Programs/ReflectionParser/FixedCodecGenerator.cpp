#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecGenerator.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/FixedCodecEmitState.h"
#include "Programs/ReflectionParser/FixedCodecValueEmitter.h"

namespace sw
{
    namespace
    {
        struct FixedCodecGeneratorInternal
        {
            static constexpr const utf8* kBaseFieldName = "(base)";

            /** @brief 코덱의 멤버 함수 글입니다. `Registrar<T>` 안에 들어갑니다. */
            static string makeMemberText( FixedCodecEmitState& state, const ParsedTypeInfo& type, const bool bHasBase, const string& canonical )
            {
                const string&  fqn = type._fullyQualifiedName;
                CodeEmitBuffer buffer;
                CodeEmit       emit( buffer );
                emit.push( FixedCodecEmitState::kMemberIndent );
                emit.blank();
                emit.linef( "static constexpr const utf8* kFixedCanonical = %#;", CodeEmit::quoted( canonical ) );
                emit.blank();

                // 쓰기. 필드가 없는 타입도 있으므로 인자를 쓰지 않는 경고를 막습니다
                emit.line( "static bool fixedWrite( ::sw::BitWriter& writer, const void* pObject )" );
                emit.line( "{" ).push();
                emit.linef( "const %#& object = *static_cast<const %#*>( pObject );", fqn, fqn );
                emit.line( "(void)object;" );
                emit.line( "(void)writer;" );
                if ( bHasBase )
                {
                    emit.linef( "if ( %#()._pWrite( writer, static_cast<const ::%#*>( &object ) ) == false )", FixedCodecEmitState::makeCodecAccessorName( type._parentFQN ), type._parentFQN );
                    emit.push().line( "return false;" ).pop();
                }
                emit.raw( state._writeBuffer.view() );
                emit.line( "return true;" );
                emit.pop().line( "}" );
                emit.blank();

                // 읽기
                emit.line( "static ::sw::FixedReadResult fixedRead( ::sw::BitReader& reader, void* pObject )" );
                emit.line( "{" ).push();
                emit.linef( "%#& object = *static_cast<%#*>( pObject );", fqn, fqn );
                emit.line( "(void)object;" );
                emit.line( "(void)reader;" );
                emit.linef( "::sw::FixedReadResult result = %#;", FixedCodecEmitState::kReadOk );
                if ( bHasBase )
                {
                    emit.linef( "result = %#()._pRead( reader, static_cast<::%#*>( &object ) );", FixedCodecEmitState::makeCodecAccessorName( type._parentFQN ), type._parentFQN );
                    emit.linef( "if ( result != %# )", FixedCodecEmitState::kReadOk );
                    emit.push().line( "return result;" ).pop();
                }
                emit.raw( state._readBuffer.view() );
                if ( type._validate.empty() == false )
                {
                    emit.line( "{" ).push();
                    emit.line( "::sw::ValidationContext context;" );
                    emit.linef( "object.%#( context );", type._validate );
                    emit.line( "if ( context.hasError() )" );
                    emit.push().line( "return ::sw::FixedReadResult::ValidateFailed;" ).pop();
                    emit.pop().line( "}" );
                }
                emit.line( "return result;" );
                emit.pop().line( "}" );
                emit.blank();

                // 비교
                emit.line( "static bool fixedEquals( const void* pA, const void* pB )" );
                emit.line( "{" ).push();
                emit.linef( "const %#& a = *static_cast<const %#*>( pA );", fqn, fqn );
                emit.linef( "const %#& b = *static_cast<const %#*>( pB );", fqn, fqn );
                emit.line( "(void)a;" );
                emit.line( "(void)b;" );
                if ( bHasBase )
                {
                    emit.linef( "if ( %#()._pEquals( static_cast<const ::%#*>( &a ), static_cast<const ::%#*>( &b ) ) == false )", FixedCodecEmitState::makeCodecAccessorName( type._parentFQN ),
                                type._parentFQN, type._parentFQN );
                    emit.push().line( "return false;" ).pop();
                }
                emit.raw( state._equalsBuffer.view() );
                emit.line( "return true;" );
                emit.pop().line( "}" );
                emit.blank();

                // 스키마 해시. 다른 타입의 해시는 다른 헤더 · 모듈일 수 있으므로 실행 시간에 그 코덱에서 얻어 섞습니다
                emit.line( "static uint64 computeFixedSchemaHash()" );
                emit.line( "{" ).push();
                StringBuilder<constant::buffer::kMax1024> nested;
                for ( const pair<string, string>& dependency : state._listDependency )
                {
                    if ( nested.size() > 0 )
                        nested.append( ", " );
                    nested.appendFormat( "%#()._pComputeSchemaHash()", FixedCodecEmitState::makeCodecAccessorName( dependency.second ) );
                }
                emit.linef( "static const uint64 s_hash = ::sw::FixedLayout::computeSchemaHash( ::sw::FixedLayout::kVersion, kFixedCanonical, { %# } );", nested.view() );
                emit.line( "return s_hash;" );
                emit.pop().line( "}" );

                // 다른 타입의 코덱
                for ( const pair<string, string>& dependency : state._listDependency )
                {
                    emit.blank();
                    emit.linef( "static const ::sw::FixedCodec& %#()", FixedCodecEmitState::makeCodecAccessorName( dependency.second ) );
                    emit.line( "{" ).push();
                    emit.linef( "static const ::sw::hashed_string s_fqn( %# );", CodeEmit::quoted( dependency.second ) );
                    emit.line( "static ::sw::TypeLookupCache     s_cache;" );
                    emit.line( "return ::sw::FixedLayout::requireNestedCodec( s_cache, s_fqn );" );
                    emit.pop().line( "}" );
                }

                // 열거자 확인. 등록된 열거자(비트 플래그면 등록된 비트)만 받습니다
                for ( const ParsedFixedEnum* pEnum : state._listEnum )
                {
                    const string enumType = "::" + pEnum->_fullyQualifiedName;
                    emit.blank();
                    emit.linef( "static bool %#( const %# value )", FixedCodecEmitState::makeEnumHelperName( *pEnum ), enumType );
                    emit.line( "{" ).push();
                    if ( pEnum->_bIsBitFlag == SW_TRUE )
                    {
                        StringBuilder<constant::buffer::kMax1024> mask;
                        for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
                        {
                            if ( mask.size() > 0 )
                                mask.append( " | " );
                            mask.appendFormat( "static_cast<uint64>( %#::%# )", enumType, enumerator._name );
                        }
                        emit.linef( "constexpr uint64 kMask = %#;", mask.size() > 0 ? mask.view() : string_view( "0" ) );
                        emit.line( "return ( static_cast<uint64>( value ) & ~kMask ) == 0;" );
                    }
                    else
                    {
                        emit.line( "switch ( static_cast<int64>( value ) )" );
                        emit.line( "{" ).push();
                        vector<int64> listSeen;
                        for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
                        {
                            const bool bDuplicate = std::find( listSeen.begin(), listSeen.end(), enumerator._value ) != listSeen.end();
                            if ( enumerator._name == pEnum->_countEnumerator || bDuplicate )
                                continue;
                            listSeen.push_back( enumerator._value );
                            emit.linef( "case static_cast<int64>( %#::%# ):", enumType, enumerator._name );
                        }
                        emit.push().line( "return true;" ).pop();
                        emit.line( "default:" );
                        emit.push().line( "return false;" ).pop();
                        emit.pop().line( "}" );
                    }
                    emit.pop().line( "}" );
                }
                return string( buffer.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FixedCodecGenerator::TypePlan::TypePlan() noexcept
        : _canonical{}
        , _reason{}
        , _memberText{}
        , _listDependency{}
        , _unresolvedName{}
        , _unresolvedMember{}
        , _unresolvedLine{ 0 }
        , _bEligible{ SW_FALSE }
        , _bResolvedByParent{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    FixedCodecGenerator::FixedCodecGenerator( const ParsedHeader& header, const string& sourceFilePath )
        : _header{ header }
        , _sourceFilePath{ sourceFilePath }
        , _listPlan{}
    {
    }

    bool FixedCodecGenerator::makePlan()
    {
        _listPlan.clear();
        _listPlan.resize( _header._listType.size() );

        bool           bValid = true;
        vector<string> listResolvedFqn;
        for ( size_t typeIndex = 0; typeIndex < _header._listType.size(); ++typeIndex )
        {
            const ParsedTypeInfo& type = _header._listType[typeIndex];
            TypePlan&             plan = _listPlan[typeIndex];
            FixedCodecEmitState   state( _header, _sourceFilePath, type );
            // 함수 본문 들여쓰기입니다. 네임스페이스, 구조체, 함수 안입니다.
            state._write.push( FixedCodecEmitState::kMemberIndent + 1 );
            state._read.push( FixedCodecEmitState::kMemberIndent + 1 );
            state._equals.push( FixedCodecEmitState::kMemberIndent + 1 );

            string     canonical = "T=" + type._fullyQualifiedName + "|";
            const bool bHasBase  = type._baseCount == 1 && type._bParentReflected == SW_TRUE && type._parentFQN.empty() == false;
            if ( bHasBase )
            {
                canonical += "B=" + type._parentFQN + "|";
                state.addDependency( FixedCodecGeneratorInternal::kBaseFieldName, type._parentFQN );
            }
            state._listInlineFqn.push_back( type._fullyQualifiedName );
            FixedCodecValueEmitter::emitProperties( state, type, "object", "a", "b", 0, canonical );

            if ( state._bError == SW_TRUE )
                bValid = false;
            listResolvedFqn.insert( listResolvedFqn.end(), state._listResolvedFqn.begin(), state._listResolvedFqn.end() );
            plan._unresolvedName   = state._unresolvedName;
            plan._unresolvedMember = state._unresolvedMember;
            plan._unresolvedLine   = state._unresolvedLine;
            if ( state._reason.empty() == false )
            {
                plan._reason = state._reason;
                continue;
            }
            plan._bEligible      = SW_TRUE;
            plan._canonical      = canonical;
            plan._listDependency = state._listDependency;
            plan._memberText     = FixedCodecGeneratorInternal::makeMemberText( state, type, bHasBase, canonical );
        }

        // 바깥 타입의 필드를 범위로 쓰는 타입은 같은 헤더의 부모가 펼치며 그 이름을 풀어야 합니다. 아무도 풀지 않았으면 선언이 틀렸습니다.
        for ( size_t typeIndex = 0; typeIndex < _listPlan.size(); ++typeIndex )
        {
            TypePlan&             plan = _listPlan[typeIndex];
            const ParsedTypeInfo& type = _header._listType[typeIndex];
            if ( plan._unresolvedName.empty() )
                continue;
            if ( std::find( listResolvedFqn.begin(), listResolvedFqn.end(), type._fullyQualifiedName ) != listResolvedFqn.end() )
            {
                plan._bResolvedByParent = SW_TRUE;
                continue;
            }
            SW_LOG_ERROR_ID( "FixedCodecGenerator.ERRORFixedLayoutRange", "ERROR: %#(%#): fixed layout of '%#::%#': range name '%#' is not a field of this type, and no type in this header "
                                                                          "embeds it with such a field declared before it",
                             _sourceFilePath, plan._unresolvedLine, type._fullyQualifiedName, plan._unresolvedMember, plan._unresolvedName );
            bValid = false;
        }
        return bValid;
    }

    void FixedCodecGenerator::emitRegistration( CodeEmit& emit, const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        if ( plan._bEligible == SW_FALSE )
        {
            emit.linef( "// FixedLayout: not eligible - %#", plan._reason );
            emit.linef( "static ::sw::FixedCodec s_fixedCodec( %# );", CodeEmit::quoted( plan._reason ) );
            emit.line( "info._pFixedCodec = &s_fixedCodec;" );
            return;
        }
        string dependencyArgs = "nullptr, 0u";
        if ( plan._listDependency.empty() == false )
        {
            emit.line( "static const ::sw::FixedCodec::Dependency s_arrFixedDependency[] =" );
            emit.line( "{" ).push();
            for ( const pair<string, string>& dependency : plan._listDependency )
            {
                emit.linef( "{ %#, %# },", CodeEmit::quoted( dependency.first ), CodeEmit::quoted( dependency.second ) );
            }
            emit.pop().line( "};" );
            dependencyArgs = "s_arrFixedDependency, " + FixedCodecEmitState::formatInteger( static_cast<int64>( plan._listDependency.size() ) ) + "u";
        }
        emit.linef( "static ::sw::FixedCodec s_fixedCodec( &fixedWrite, &fixedRead, &fixedEquals, &computeFixedSchemaHash, kFixedCanonical, %# );", dependencyArgs );
        emit.line( "info._pFixedCodec = &s_fixedCodec;" );
    }

    const string& FixedCodecGenerator::getMemberText( const size_t typeIndex ) const
    {
        return _listPlan[typeIndex]._memberText;
    }

    string FixedCodecGenerator::makeSchemaLine( const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        string          line = _header._listType[typeIndex]._fullyQualifiedName;
        line += plan._bEligible == SW_TRUE ? " | " : " | (not eligible) ";
        line += plan._bEligible == SW_TRUE ? plan._canonical : plan._reason;
        return line;
    }

    string FixedCodecGenerator::makeDumpLine( const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        if ( plan._bEligible == SW_TRUE )
            return "  FIXED codec: eligible  " + plan._canonical;
        return "  FIXED codec: not eligible - " + plan._reason;
    }
} // namespace sw
