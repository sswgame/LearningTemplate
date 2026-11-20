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
        , _replicatedCanonical{}
        , _replicated{}
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

            FixedCanonicalBuilder canonical;
            canonical.appendFormat( "T=%#|", type._fullyQualifiedName );
            const bool bHasBase = type._baseCount == 1 && type._bParentReflected == SW_TRUE && type._parentFQN.empty() == false;
            if ( bHasBase )
            {
                canonical.appendFormat( "B=%#|", type._parentFQN );
                (void)state.addDependency( FixedCodecGeneratorInternal::kBaseFieldName, type._parentFQN ); // 기반은 의존 0 번이다(런타임 `FixedCodec::_pBaseCast`)
            }
            state._listInlineFqn.push_back( type._fullyQualifiedName );
            FixedCodecValueEmitter::emitProperties( state, type, canonical, state._listRootNode );

            if ( state._bError == SW_TRUE )
                bValid = false;
            if ( planReplicated( type, plan ) == false )
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
            plan._canonical      = string( canonical.view() );
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

    bool FixedCodecGenerator::planReplicated( const ParsedTypeInfo& type, TypePlan& outPlan ) const
    {
        // `Replicated` 칸만 남긴 사본을 같은 값 방출기로 돈다 — 칸 표는 하나의 인코딩 규칙이고, 범위 이름은 사본 안(앞선 Replicated 칸)에서만 풀린다.
        ParsedTypeInfo replicatedType = type;
        replicatedType._listProperty.clear();
        for ( const ParsedPropertyInfo& prop : type._listProperty )
        {
            if ( prop._bReplicated == SW_FALSE )
                continue;
            if ( prop._bTransient == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "FixedCodecGenerator.ERRORReplicatedTransient", "ERROR: %#(%#): replicated schema of '%#::%#': a Replicated field cannot be Transient",
                                 _sourceFilePath, prop._line, type._fullyQualifiedName, prop._memberName );
                return false;
            }
            replicatedType._listProperty.push_back( prop );
        }
        if ( replicatedType._listProperty.empty() )
            return true;

        FixedCodecEmitState   state( _header, _sourceFilePath, replicatedType );
        FixedCanonicalBuilder canonical;
        canonical.appendFormat( "R=%#|", type._fullyQualifiedName );
        state._listInlineFqn.push_back( type._fullyQualifiedName );
        FixedCodecValueEmitter::emitProperties( state, replicatedType, canonical, state._listRootNode );
        if ( state._bError == SW_TRUE )
            return false;
        if ( state._reason.empty() == false )
        {
            SW_LOG_ERROR_ID( "FixedCodecGenerator.ERRORReplicatedUnencodable", "ERROR: %#: replicated schema of '%#': Replicated field is not in the fixed layout encoding table - %#",
                             _sourceFilePath, type._fullyQualifiedName, state._reason );
            return false;
        }
        if ( state._unresolvedName.empty() == false )
        {
            SW_LOG_ERROR_ID( "FixedCodecGenerator.ERRORReplicatedRange", "ERROR: %#(%#): replicated schema of '%#::%#': range name '%#' must be a Replicated field declared before it",
                             _sourceFilePath, state._unresolvedLine, type._fullyQualifiedName, state._unresolvedMember, state._unresolvedName );
            return false;
        }
        outPlan._replicatedCanonical        = string( canonical.view() );
        outPlan._replicated._listRootNode   = std::move( state._listRootNode );
        outPlan._replicated._listEnum       = std::move( state._listEnum );
        outPlan._replicated._listDependency = std::move( state._listDependency );
        return true;
    }

    void FixedCodecGenerator::flattenNodes( const vector<FixedCodecNode>& listNode, vector<string>& outListEntry )
    {
        const size_t first = outListEntry.size();
        outListEntry.resize( first + listNode.size() );
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            const FixedCodecNode&                     node       = listNode[index];
            const size_t                              childFirst = outListEntry.size();
            StringBuilder<constant::buffer::kMax2048> entry;
            entry.append( node._call );
            if ( node._childArgs == FixedCodecNode::ChildArgs::Element )
                entry.appendFormat( ", %#u", static_cast<int64>( childFirst ) );
            else if ( node._childArgs == FixedCodecNode::ChildArgs::Group )
                entry.appendFormat( ", %#u, %#u", static_cast<int64>( childFirst ), static_cast<int64>( node._listChild.size() ) );
            entry.append( " )" );
            outListEntry[first + index] = string( entry.view() );
            flattenNodes( node._listChild, outListEntry );
        }
    }

    pair<string, string> FixedCodecGenerator::emitTables( CodeEmit& emit, const vector<FixedCodecNode>& listRootNode, const vector<const ParsedFixedEnum*>& listEnum,
                                                          const vector<pair<string, string>>& listDependency, const string& tablePrefix, vector<string>& listEmittedEnum )
    {
        // 플래그가 아닌 열거형의 등록 값(`Count` · 겹친 값 제외)입니다. 읽은 값이 이 안에 없으면 OutOfRange 입니다.
        for ( const ParsedFixedEnum* pEnum : listEnum )
        {
            const string tableName = FixedCodecEmitState::makeEnumValueTableName( *pEnum );
            if ( std::find( listEmittedEnum.begin(), listEmittedEnum.end(), tableName ) != listEmittedEnum.end() )
                continue;
            listEmittedEnum.push_back( tableName );
            emit.linef( "static constexpr int64 %#[] =", tableName );
            emit.line( "{" ).push();
            vector<int64> listSeen;
            for ( const ParsedEnumeratorInfo& enumerator : pEnum->_listEnumerator )
            {
                const bool bDuplicate = std::find( listSeen.begin(), listSeen.end(), enumerator._value ) != listSeen.end();
                if ( enumerator._name == pEnum->_countEnumerator || bDuplicate )
                    continue;
                listSeen.push_back( enumerator._value );
                emit.linef( "static_cast<int64>( ::%#::%# ),", pEnum->_fullyQualifiedName, enumerator._name );
            }
            emit.pop().line( "};" );
        }

        // 서술자 표입니다. 최상위 필드가 앞 칸들이고, 묶음의 자식은 그 뒤에 이어진 칸이다(`FixedFieldDesc`).
        vector<string> listEntry;
        flattenNodes( listRootNode, listEntry );
        string fieldArgs = "nullptr, 0u";
        if ( listEntry.empty() == false )
        {
            emit.linef( "static constexpr ::sw::FixedFieldDesc %#Field[] =", tablePrefix );
            emit.line( "{" ).push();
            for ( const string& entry : listEntry )
            {
                emit.linef( "%#,", entry );
            }
            emit.pop().line( "};" );
            StringBuilder<constant::buffer::kMax128> args;
            args.appendFormat( "%#Field, %#u", tablePrefix, static_cast<int64>( listRootNode.size() ) );
            fieldArgs = string( args.view() );
        }

        string dependencyArgs = "nullptr, 0u";
        if ( listDependency.empty() == false )
        {
            emit.linef( "static const ::sw::FixedCodec::Dependency %#Dependency[] =", tablePrefix );
            emit.line( "{" ).push();
            for ( const pair<string, string>& dependency : listDependency )
            {
                emit.linef( "{ %#, %# },", CodeEmit::quoted( dependency.first ), CodeEmit::quoted( dependency.second ) );
            }
            emit.pop().line( "};" );
            StringBuilder<constant::buffer::kMax128> args;
            args.appendFormat( "%#Dependency, %#u", tablePrefix, static_cast<int64>( listDependency.size() ) );
            dependencyArgs = string( args.view() );
        }
        return { fieldArgs, dependencyArgs };
    }

    void FixedCodecGenerator::emitRegistration( CodeEmit& emit, const size_t typeIndex ) const
    {
        const TypePlan& plan = _listPlan[typeIndex];
        vector<string>  listEmittedEnum;
        if ( plan._bEligible == SW_FALSE )
        {
            emit.linef( "// FixedLayout: not eligible - %#", plan._reason );
            emit.linef( "static ::sw::FixedCodec s_fixedCodec( %# );", CodeEmit::quoted( plan._reason ) );
        }
        else
        {
            const ParsedTypeInfo&      type      = _header._listType[typeIndex];
            const pair<string, string> tableArgs = emitTables( emit, plan._listRootNode, plan._listEnum, plan._listDependency, "s_arrFixed", listEmittedEnum );

            string baseCast = "nullptr";
            if ( plan._bHasBase == SW_TRUE )
            {
                StringBuilder<constant::buffer::kMax1024> text;
                text.appendFormat( "[]( const void* pObject ) -> const void* { return static_cast<const ::%#*>( static_cast<const ::%#*>( pObject ) ); }", type._parentFQN,
                                   type._fullyQualifiedName );
                baseCast = string( text.view() );
            }
            // 검증 함수는 위에서 건 `info._pValidate` 와 같은 것이다(`REFLECT( Validate = fn )` 이 없으면 nullptr).
            emit.linef( "static ::sw::FixedCodec s_fixedCodec( %#, %#, %#, %#, info._pValidate );", tableArgs.first, CodeEmit::quoted( plan._canonical ), tableArgs.second, baseCast );
        }

        // 복제 스키마 — `Replicated` 칸만 든 두 번째 표다. 기반 · 검증은 없다(기반의 복제 칸은 `ReplicatedLayout` 이 거절한다, 검증은 타입 전체의 것이다).
        if ( plan._replicatedCanonical.empty() == false )
        {
            const pair<string, string> tableArgs =
                emitTables( emit, plan._replicated._listRootNode, plan._replicated._listEnum, plan._replicated._listDependency, "s_arrReplicated", listEmittedEnum );
            emit.linef( "static ::sw::FixedCodec s_replicatedCodec( %#, %#, %#, nullptr, nullptr );", tableArgs.first, CodeEmit::quoted( plan._replicatedCanonical ), tableArgs.second );
            emit.line( "s_fixedCodec._pReplicated = &s_replicatedCodec;" );
        }
        emit.line( "info._pFixedCodec = &s_fixedCodec;" );
    }

    string FixedCodecGenerator::makeSchemaLine( const size_t typeIndex ) const
    {
        const TypePlan&                           plan = _listPlan[typeIndex];
        StringBuilder<constant::buffer::kMax1024> line;
        if ( plan._bEligible == SW_TRUE )
            line.appendFormat( "%# | %#", _header._listType[typeIndex]._fullyQualifiedName, plan._canonical );
        else
            line.appendFormat( "%# | (not eligible) %#", _header._listType[typeIndex]._fullyQualifiedName, plan._reason );
        return string( line.view() );
    }

    string FixedCodecGenerator::makeDumpLine( const size_t typeIndex ) const
    {
        const TypePlan&                           plan = _listPlan[typeIndex];
        StringBuilder<constant::buffer::kMax1024> line;
        if ( plan._bEligible == SW_TRUE )
            line.appendFormat( "  FIXED codec: eligible  %#", plan._canonical );
        else
            line.appendFormat( "  FIXED codec: not eligible - %#", plan._reason );
        return string( line.view() );
    }
} // namespace sw
