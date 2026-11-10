#include "pch.h"

#include "Programs/ReflectionParser/FixedCodecGenerator.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/FixedCodecValueEmitter.h"

namespace sw
{
    namespace
    {
        struct FixedCodecGeneratorInternal
        {
            static constexpr const utf8* kBaseFieldName = "(base)";
        };
    } // namespace
} // namespace sw

namespace sw
{
    FixedCodecGenerator::TypePlan::TypePlan() noexcept
        : _canonical{}
        , _reason{}
        , _listRootNode{}
        , _listEnum{}
        , _listDependency{}
        , _unresolvedName{}
        , _unresolvedMember{}
        , _unresolvedLine{ 0 }
        , _bEligible{ SW_FALSE }
        , _bResolvedByParent{ SW_FALSE }
        , _bHasBase{ SW_FALSE }
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

            string     canonical = "T=" + type._fullyQualifiedName + "|";
            const bool bHasBase  = type._baseCount == 1 && type._bParentReflected == SW_TRUE && type._parentFQN.empty() == false;
            if ( bHasBase )
            {
                canonical += "B=" + type._parentFQN + "|";
                (void)state.addDependency( FixedCodecGeneratorInternal::kBaseFieldName, type._parentFQN ); // 기반은 의존 0 번이다(런타임 `FixedCodec::_pBaseCast`)
            }
            state._listInlineFqn.push_back( type._fullyQualifiedName );
            FixedCodecValueEmitter::emitFields( state, type, canonical, state._listRootNode );

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
            plan._bHasBase       = bHasBase ? SW_TRUE : SW_FALSE;
            plan._canonical      = canonical;
            plan._listDependency = std::move( state._listDependency );
            plan._listRootNode   = std::move( state._listRootNode );
            plan._listEnum       = std::move( state._listEnum );
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

    void FixedCodecGenerator::flattenNodes( const vector<FixedCodecNode>& listNode, vector<string>& outListEntry )
    {
        const size_t first = outListEntry.size();
        outListEntry.resize( first + listNode.size() );
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const FixedCodecNode& node           = listNode[index];
            const size_t          childFirst     = outListEntry.size();
            string                entry          = node._call;
            const string          childFirstText = FixedCodecEmitState::formatInteger( static_cast<int64>( childFirst ) );
            if ( node._childArgs == FixedCodecNode::ChildArgs::Element )
                entry += ", " + childFirstText + "u";
            else if ( node._childArgs == FixedCodecNode::ChildArgs::Group )
                entry += ", " + childFirstText + "u, " + FixedCodecEmitState::formatInteger( static_cast<int64>( node._listChild.size() ) ) + "u";
            entry += " )";
            outListEntry[first + index] = std::move( entry );
            flattenNodes( node._listChild, outListEntry );
        }
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
        const ParsedTypeInfo& type = _header._listType[typeIndex];

        // 플래그가 아닌 열거형의 등록 값(`Count` · 겹친 값 제외)입니다. 읽은 값이 이 안에 없으면 OutOfRange 입니다.
        for ( const ParsedFixedEnum* pEnum : plan._listEnum )
        {
            const string enumType = "::" + pEnum->_fullyQualifiedName;
            emit.linef( "static constexpr int64 %#[] =", FixedCodecEmitState::makeEnumValueTableName( *pEnum ) );
            emit.line( "{" ).push();
            vector<int64> listSeen;
            for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
            {
                const bool bDuplicate = std::find( listSeen.begin(), listSeen.end(), enumerator._value ) != listSeen.end();
                if ( enumerator._name == pEnum->_countEnumerator || bDuplicate )
                    continue;
                listSeen.push_back( enumerator._value );
                emit.linef( "static_cast<int64>( %#::%# ),", enumType, enumerator._name );
            }
            emit.pop().line( "};" );
        }

        // 서술자 표입니다. 최상위 필드가 앞 칸들이고, 묶음의 자식은 그 뒤에 이어진 칸이다(`FixedFieldDesc`).
        vector<string> listEntry;
        flattenNodes( plan._listRootNode, listEntry );
        string fieldArgs = "nullptr, 0u";
        if ( listEntry.empty() == false )
        {
            emit.line( "static constexpr ::sw::FixedFieldDesc s_arrFixedField[] =" );
            emit.line( "{" ).push();
            for ( const string& entry : listEntry )
            {
                emit.linef( "%#,", entry );
            }
            emit.pop().line( "};" );
            fieldArgs = "s_arrFixedField, " + FixedCodecEmitState::formatInteger( static_cast<int64>( plan._listRootNode.size() ) ) + "u";
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

        string baseCast = "nullptr";
        if ( plan._bHasBase == SW_TRUE )
        {
            StringBuilder<constant::buffer::kMax1024> text;
            text.appendFormat( "[]( const void* pObject ) -> const void* { return static_cast<const ::%#*>( static_cast<const ::%#*>( pObject ) ); }", type._parentFQN,
                               type._fullyQualifiedName );
            baseCast = string( text.view() );
        }
        // 검증 함수는 위에서 건 `info._pValidate` 와 같은 것이다(`REFLECT( Validate = fn )` 이 없으면 nullptr).
        emit.linef( "static ::sw::FixedCodec s_fixedCodec( %#, %#, %#, %#, info._pValidate );", fieldArgs, CodeEmit::quoted( plan._canonical ), dependencyArgs, baseCast );
        emit.line( "info._pFixedCodec = &s_fixedCodec;" );
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
