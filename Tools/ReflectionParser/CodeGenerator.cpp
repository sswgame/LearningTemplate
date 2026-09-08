#include "pch.h"

#include "ReflectionParser/CodeGenerator.h"

#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserUtil.h"
#include "ReflectionParser/TypeNameMap.h"

SW_LOG_CALLER( "CodeGenerator" );
namespace sw
{
    namespace
    {
        struct CodeGeneratorInternal
        {
            static constexpr int32 kMaxNestedContainerDepth = 3;

            /**
             * @brief 열거형 전체 FQN(예: "sw::EState::Idle")에서 말단 열거자 이름("Idle")을 추출합니다.
             */
            static string_view enumeratorLeaf( string_view spec )
            {
                return ParserUtil::scopeLeaf( spec );
            }

            /**
             * @brief 컨테이너 필드에 대한 래퍼 타입 문자열(예: `sw::VectorWrapper<decltype(std::declval<MyClass>().myList)>`)을 생성합니다.
             */
            static string makeWrapperType( const string& containerType, const string& fqn, const string& fieldName )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.appendFormat( "sw::%#Wrapper<decltype(std::declval<%#>().%#)>", containerType, fqn, fieldName );
                return string( b.view() );
            }

            /**
             * @brief 2중, 3중 중첩 컨테이너(예: vector<vector<int32>>)에 대한 중첩 래퍼 표기를 생성합니다.
             */
            static string makeNestedWrapperType( const string& containerType, int32 depth )
            {
                StringBuilder<constant::kMaxBuffer128> b;
                b.appendFormat( "sw::%#Wrapper<NestC%#>", containerType, depth );
                return string( b.view() );
            }

            /**
             * @brief 생성자 검색을 위한 고유 식별자 문자열(예: `$ctor(int32,float32)`)을 구성합니다.
             */
            static string makeCtorLookupName( const ParsedFunctionInfo& method )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.append( annotationConstants::kCtorLookupName );
                if ( method._listParameterTypeName.empty() == false )
                {
                    b.append( '(' );
                    for ( size_t paramIndex = 0; paramIndex < method._listParameterTypeName.size(); ++paramIndex )
                    {
                        if ( paramIndex > 0 )
                            b.append( ',' );
                        b.append( normalizeTypeName( method._listParameterTypeName[paramIndex] ) );
                    }
                    b.append( ')' );
                }
                return string( b.view() );
            }

            /**
             * @brief 타입 이름 목록을 C++ 배열 초기화 구문 `{ "int32", "string" }` 형태로 포맷팅합니다.
             */
            static string makeQuotedTypeList( const vector<string>& listType )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                b.append( "{ " );
                for ( size_t typeIndex = 0; typeIndex < listType.size(); ++typeIndex )
                {
                    if ( typeIndex > 0 )
                        b.append( ", " );
                    b.appendFormat( "\"%#\"", normalizeTypeName( listType[typeIndex] ) );
                }
                b.append( " }" );
                return string( b.view() );
            }

            /**
             * @brief 런타임 동적 함수 호출(Invoker)을 위한 인자 추출 구문 `args.get<T>(0), args.get<T>(1)...`을 생성합니다.
             */
            static string makeInvokerCallArgs( const vector<string>& listType )
            {
                StringBuilder<constant::kMaxBuffer1024> b;
                for ( size_t typeIndex = 0; typeIndex < listType.size(); ++typeIndex )
                {
                    if ( typeIndex > 0 )
                        b.append( ", " );
                    b.appendFormat( "args.get<%#>( %# )", normalizeTypeName( listType[typeIndex] ), static_cast<uint32>( typeIndex ) );
                }
                return string( b.view() );
            }

            /** @brief registerTypeAlias / registerEnumAlias 호출 줄을 만듭니다. */
            static string emitAliasRegisterLines( const vector<string>& aliases, const string& canonical,
                                                  const bool bEnum )
            {
                if ( aliases.empty() )
                    return {};

                CodeEmitBuffer buf;
                CodeEmit       emit( buf );
                emit.push( 3 );
                const utf8* pRegisterFunc = bEnum ? "registry.registerEnumAlias" : "registry.registerTypeAlias";
                for ( const string& alias : aliases )
                {
                    if ( alias.empty() || alias == canonical )
                        continue;
                    emit.linef( "%#( \"%#\", \"%#\" );", pRegisterFunc, CodeEmit::escapeCppString( alias ),
                                CodeEmit::escapeCppString( canonical ) );
                }
                return string( buf.view() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CodeGenerator::CodeGenerator(
        const vector<ParsedTypeInfo>& types,
        const vector<ParsedEnumInfo>& enums,
        const string&                 sourceFilePath,
        const string&                 outputDir,
        const string&                 sourceRoot )
        : _listType{ types }
        , _listEnum{ enums }
        , _sourceFilePath{ sourceFilePath }
        , _sourceRoot{ sourceRoot }
        , _outputDir{ outputDir }
        , _outputFilePath{}
        , _outputHeaderPath{}
    {
    }

    void CodeGenerator::appendTemplate( CodeEmitBuffer& out, const string_view name,
                                        const unordered_map<string, string>& vars )
    {
        out.append( EmitTemplateStore::instance().render( name, vars ) );
    }

    void CodeGenerator::appendTemplate( CodeEmitBuffer& out, const string_view name,
                                        std::initializer_list<pair<string_view, string_view>> vars )
    {
        out.append( EmitTemplateStore::instance().render( name, vars ) );
    }

    const utf8* CodeGenerator::containerKindExpr( const ContainerKind kind )
    {
        return toCppExpr( kind );
    }

    const utf8* CodeGenerator::peelMember( const ContainerKind kind )
    {
        return containerPeelMember( kind );
    }

    bool CodeGenerator::generate()
    {
        if ( EmitTemplateStore::instance().isLoaded() == false )
        {
            SW_LOG_ERROR( "Emit templates not loaded (pass --emit-templates <dir>)." );
            return false;
        }

        BLOCK( "Prepare Output Path" )
        {
            FileUtil::ensureDirectoryExists( _outputDir );
            _outputFilePath   = ParserUtil::makeGeneratedPath( _outputDir, _sourceFilePath, ParserContext::getSharedConfig()._emitCppExtension );
            _outputHeaderPath = ParserUtil::makeGeneratedPath( _outputDir, _sourceFilePath, ParserContext::getSharedConfig()._emitHeaderExtension );
        }

        CodeEmitBuffer buffer;

        if ( _listType.empty() && _listEnum.empty() )
        {
            buffer.appendFormat( "// No reflected types found in %#\n", _sourceFilePath );
        }

        if ( _listType.empty() == false || _listEnum.empty() == false )
        {
            BLOCK( "Emit File Header" )
            {
                emitFileHeader( buffer );
                buffer.append( ParserContext::getSharedConfig()._emitGeneratedNsOpen );
            }

            BLOCK( "Emit Registrars" )
            {
                for ( const ParsedTypeInfo& typeInfo : _listType )
                    emitTypeRegistrar( buffer, typeInfo );
                for ( const ParsedEnumInfo& enumInfo : _listEnum )
                    emitEnumRegistrar( buffer, enumInfo );
            }

            buffer.append( ParserContext::getSharedConfig()._emitGeneratedNsClose );

            BLOCK( "Emit Component Factory Registrars" )
            {
                for ( const ParsedTypeInfo& typeInfo : _listType )
                {
                    if ( typeInfo.wantsComponentFactory() )
                        emitComponentFactoryRegistrar( buffer, typeInfo );
                }
            }

            BLOCK( "Emit Type Traits & Accessors" )
            {
                for ( const ParsedTypeInfo& typeInfo : _listType )
                {
                    emitReflectTypeTraits( buffer, typeInfo );
                    if ( typeInfo.wantsTypeApi() )
                        emitTypeInfoAccessors( buffer, typeInfo );
                }
            }
        }

        const string newContent( buffer.view() );

        BLOCK( "Incremental Write" )
        {
            bool bCppUnchanged = false;
            if ( FileUtil::fileExists( _outputFilePath ) )
            {
                string existingContent;
                FileUtil::readTextFile( _outputFilePath, existingContent );
                if ( existingContent.empty() == false && existingContent == newContent )
                {
                    SW_LOG_TRACE( "Incremental check: %# is up-to-date, skipping write.", _outputFilePath );
                    bCppUnchanged = true;
                }
            }

            if ( bCppUnchanged == false )
            {
                if ( FileUtil::writeTextFile( _outputFilePath, newContent ) == false )
                {
                    SW_LOG_ERROR( "Failed to open output: %#", _outputFilePath );
                    return false;
                }
            }
        }

        if ( emitGeneratedHeader() == false )
            return false;

        SW_LOG_TRACE( "Generated: %#", _outputFilePath );
        return true;
    }

    void CodeGenerator::emitFileHeader( CodeEmitBuffer& out ) const
    {
        appendTemplate( out, tplConstants::kFileHeader, {
                                                            { templateKeyConstants::kSourcePath, _sourceFilePath }
        } );
    }

    void CodeGenerator::emitReflectTypeTraits( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kReflectTypeTraits, {
                                                                   { templateKeyConstants::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitTypeInfoAccessors( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kTypeInfoAccessors, {
                                                                   { templateKeyConstants::kFqn, typeInfo._fullyQualifiedName }
        } );
    }

    void CodeGenerator::emitComponentFactoryRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        appendTemplate( out, tplConstants::kComponentFactoryRegistrar, {
                                                                           {        templateKeyConstants::kId, sanitizeIdentifier( typeInfo._fullyQualifiedName )},
                                                                           {       templateKeyConstants::kFqn,                       typeInfo._fullyQualifiedName},
                                                                           {      templateKeyConstants::kName,                                     typeInfo._name},
                                                                           {templateKeyConstants::kModuleName,                                    getModuleName()},
        } );
    }

    /**
     * @brief 세 scope(REFLECT/PROPERTY/FUNCTION)가 공통으로 쓰는 편집기 메타를 출력합니다.
     * @param prefix 대상 접두사. 예: "p._metadata." / "info._metadata."
     * @details Category/DisplayName/Tooltip 은 스코프마다 대상만 다르고 형태가 같아 여기 모읍니다.
     *          (나머지 필드는 스코프별로 구성이 달라 각 emit 함수에 둡니다)
     */
    template <typename TParsed>
    static void emitCommonEditorMeta( CodeEmit& emit, const TParsed& parsed, const string& prefix )
    {
        emit.assignQuotedIf( parsed._category.empty() == false, prefix + "_category", parsed._category );
        emit.assignQuotedIf( parsed._displayName.empty() == false, prefix + "_displayName", parsed._displayName );
        emit.assignQuotedIf( parsed._tooltip.empty() == false, prefix + "_tooltip", parsed._tooltip );
    }

    /**
     * @brief 커스텀 메타 페어 맵을 출력합니다. 세 scope 가 동일한 형태를 씁니다.
     */
    template <typename TParsed>
    static void emitCustomMetaMap( CodeEmit& emit, const TParsed& parsed, const string& prefix )
    {
        if ( parsed._listCustomMeta.empty() )
            return;
        emit.linef( "%#_mapCustomMeta = {", prefix );
        emit.push();
        for ( const auto& [key, val] : parsed._listCustomMeta )
            emit.linef( "{ %#, %# },", CodeEmit::hs( key ), CodeEmit::quoted( val ) );
        emit.pop();
        emit.line( "};" );
    }

    void CodeGenerator::emitPropertyMetadata( CodeEmit& emit, const ParsedPropertyInfo& prop ) const
    {
        emit.line( "#if !defined( SW_SHIPPING )" );
        emitCommonEditorMeta( emit, prop, "p._metadata." );
        emit.flagIf( prop._bHideInInspector != 0, "p._metadata._bHideInInspector", "SW_TRUE" );
        emitCustomMetaMap( emit, prop, "p._metadata." );
        emit.line( "#endif" );

        emit.assignQuotedIf( prop._defaultValue.empty() == false, "p._metadata._defaultValue", prop._defaultValue );
        emit.assignQuotedIf( prop._assetType.empty() == false, "p._metadata._assetType", prop._assetType );
        emit.flagIf( prop._bReadOnly != 0, "p._metadata._bReadOnly", "SW_TRUE" );
        emit.flagIf( prop._bXmlAttribute != 0, "p._metadata._bXmlAttribute", "SW_TRUE" );
        emit.flagIf( prop._bAssetPath != 0, "p._metadata._bAssetPath", "SW_TRUE" );
        emit.flagIf( prop._bPolymorphic != 0, "p._metadata._bPolymorphic", "SW_TRUE" );
        emit.flagIf( prop._bTransient != 0, "p._metadata._bTransient", "SW_TRUE" );
        emit.flagIf( prop._bSkipIfEmpty != 0, "p._metadata._bSkipIfEmpty", "SW_TRUE" );
        if ( prop._bHasRange != 0 )
        {
            // 접미사 f 가 없으면 `0.100000` 은 double 이라, float32 멤버에 넣을 때 정밀도 손실 경고가
            // **생성된 파일마다** 난다. 여기서 한 번 고치면 전부 사라진다.
            emit.linef( "p._metadata._minRange     = %#ff;", prop._minRange );
            emit.linef( "p._metadata._maxRange     = %#ff;", prop._maxRange );
            emit.assign( "p._metadata._bHasRange", "SW_TRUE" );
        }
    }

    void CodeGenerator::emitNestedContainerTree( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                                 const ParsedPropertyInfo& prop ) const
    {
        if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
            return;

        const utf8*  outerKind    = containerKindExpr( prop._containerKind );
        const string outerWrapper = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

        emit.line( "{" );
        emit.push();
        emit.line( "auto nested0 = sw::make_shared<sw::NestedContainerInfo>();" );
        emit.assign( "nested0->_kind", outerKind );
        emit.linef( "nested0->_typeName = %#;", CodeEmit::hs( prop._containerTree->_typeName ) );
        emit.linef( "nested0->_elementTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
        emit.linef( "nested0->_keyTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
        emit.linef( "nested0->_wrapper = sw::make_shared<%#>();", outerWrapper );

        const ParsedContainerNode* node     = prop._containerTree->_elementNested.get();
        ContainerKind              prevKind = prop._containerKind;
        int32                      depth    = 1;

        if ( node != nullptr && node->_bIsContainer )
        {
            emit.linef( "using NestC0 = decltype( std::declval<%#>().%# );", typeInfo._fullyQualifiedName, prop._name );
            while ( node != nullptr && node->_bIsContainer && depth < CodeGeneratorInternal::kMaxNestedContainerDepth )
            {
                const utf8* kind = containerKindExpr( node->_containerKind );
                const utf8* peel = peelMember( prevKind );
                emit.linef( "using NestC%# = typename NestC%#::%#;", depth, depth - 1, peel );

                const string wrapperType = CodeGeneratorInternal::makeNestedWrapperType( node->_containerType, depth );
                emit.line( "{" );
                emit.push();
                emit.linef( "auto nested%# = sw::make_shared<sw::NestedContainerInfo>();", depth );
                emit.linef( "nested%#->_kind = %#;", depth, kind );
                emit.linef( "nested%#->_typeName = %#;", depth, CodeEmit::hs( node->_typeName ) );
                emit.linef( "nested%#->_elementTypeName = %#;", depth,
                            CodeEmit::hs( normalizeTypeName( node->_elementTypeName ) ) );
                emit.linef( "nested%#->_keyTypeName = %#;", depth, CodeEmit::hs( node->_keyTypeName ) );
                emit.linef( "nested%#->_wrapper = sw::make_shared<%#>();", depth, wrapperType );
                emit.linef( "nested%#->_elementNested = nested%#;", depth - 1, depth );
                emit.pop();
                emit.line( "}" );

                prevKind = node->_containerKind;
                node     = ( node->_elementNested != nullptr ) ? node->_elementNested.get() : nullptr;
                ++depth;
            }
        }

        emit.assign( "p._nestedContainer", "nested0" );
        emit.pop();
        emit.line( "}" );
    }

    void CodeGenerator::emitPropertyInfoEntry( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                               const ParsedPropertyInfo& prop ) const
    {
        emit.line( "[]() {" );
        emit.push();
        // PROPERTY() 에 값으로 담으면 안 되는 기반 타입을 컴파일 타임에 막는다.
        // 목록은 parser_config 의 emit.value_forbidden_base_types 에서 온다(비면 생략).
        emit.linef( "using PropDecl = decltype(%#::%#);", typeInfo._fullyQualifiedName, prop._name );
        const ParserClangConfig& cfg = ParserContext::getSharedConfig();
        if ( cfg._listValueForbiddenBaseType.empty() == false )
        {
            string condition;
            for ( const string& baseType : cfg._listValueForbiddenBaseType )
            {
                if ( condition.empty() == false )
                    condition += " || ";
                condition += "std::is_base_of_v<";
                condition += baseType;
                condition += ", std::remove_cv_t<std::remove_reference_t<PropDecl>>>";
            }
            emit.linef( "constexpr bool kIsInvalidValue = std::is_pointer_v<std::remove_cv_t<std::remove_reference_t<PropDecl>>> == false && (%#);",
                        condition );
            emit.linef( "static_assert(!kIsInvalidValue, \"%#\");", cfg._valueForbiddenMessage );
        }
        emit.line( "sw::PropertyInfo p(" );
        emit.push();
        emit.linef( "%#,", CodeEmit::hs( prop._name ) );
        emit.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._typeName ) ) );
        if ( prop._bIsBitField == SW_TRUE )
        {
            emit.linef( "%#u,", prop._byteOffset );
        }
        else
        {
            emit.linef( "offsetof(%#, %#),", typeInfo._fullyQualifiedName, prop._name );
        }

        if ( prop._bIsContainer )
        {
            const utf8*  kindStr     = containerKindExpr( prop._containerKind );
            const string wrapperType = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

            emit.line( "true," );
            emit.linef( "%#,", kindStr );
            emit.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
            emit.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
            emit.linef( "sw::make_shared<%#>() );", wrapperType );

            emitNestedContainerTree( emit, typeInfo, prop );
        }
        else
        {
            emit.line( "false, sw::ContainerKind::None," );
            emit.line( "::sw::hashed_string(), ::sw::hashed_string(), nullptr );" );
        }

        emit.pop(); // 생성자 인자 들여쓰기
        if ( prop._bIsBitField == SW_TRUE )
        {
            emit.line( "p._bIsBitField = SW_TRUE;" );
            emit.linef( "p._bitOffset = %#;", prop._bitOffset );
            emit.linef( "p._bitMask = %#;", prop._bitMask );
        }
        if ( prop._listAlias.empty() == false )
        {
            emit.line( "p._listAlias = {" );
            emit.push();
            for ( const string& alias : prop._listAlias )
                emit.linef( "%#,", CodeEmit::hs( alias ) );
            emit.pop();
            emit.line( "};" );
        }
        emitPropertyMetadata( emit, prop );
        emit.line( "return p;" );
        emit.pop();
        emit.line( "}()," );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& emit, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& retType,
                                           const string& callArgs ) const
    {
        emit.line( "auto invokerCb = []( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
        emit.line( "{" );
        emit.push();
        if ( method._listParameterTypeName.empty() )
            emit.line( "(void)args;" );

        if ( method._bStatic != 0 && method._bConstructor == SW_FALSE )
        {
            emit.line( "(void)objPtr;" );
            if ( retType == annotationConstants::kVoidTypeName )
            {
                emit.linef( "%#::%#(%#);", typeInfo._fullyQualifiedName, method._name, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                emit.linef( "return ::sw::TaskValue{ %#::%#(%#) };", typeInfo._fullyQualifiedName, method._name, callArgs );
            }
        }
        else
        {
            emit.linef( "auto* self = static_cast<%#*>( objPtr );", typeInfo._fullyQualifiedName );
            if ( method._bConstructor != 0 )
            {
                emit.linef( "new ( self ) %#(%#);", typeInfo._fullyQualifiedName, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else if ( retType == annotationConstants::kVoidTypeName )
            {
                emit.linef( "self->%#(%#);", method._name, callArgs );
                emit.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                emit.linef( "return ::sw::TaskValue{ self->%#(%#) };", method._name, callArgs );
            }
        }
        emit.pop();
        emit.line( "};" );
        emit.line( "funcInfo._invoker = SW_DELEGATE_LAMBDA( ::sw::Delegate<::sw::TaskValue( void*, const ::sw::TaskArgs& )>, invokerCb );" );
        emit.line( "info._listMethod.push_back( funcInfo );" );
    }

    void CodeGenerator::emitMethodList( CodeEmit& emit, const ParsedTypeInfo& typeInfo ) const
    {
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            const string retType = normalizeTypeName( method._returnTypeName );

            const string lookupName = ( method._bConstructor != 0 ) ? CodeGeneratorInternal::makeCtorLookupName( method ) : method._name;

            emit.line( "{" );
            emit.push();
            emit.line( "::sw::FunctionInfo funcInfo;" );
            emit.assign( "funcInfo._name", CodeEmit::quoted( ( method._bConstructor != 0 ) ? annotationConstants::kCtorLookupName : method._name ) );
            emit.linef( "funcInfo._hashName       = %#;", CodeEmit::hs( lookupName ) );
            emit.assign( "funcInfo._returnTypeName", CodeEmit::quoted( retType ) );
            emit.assign( "funcInfo._listParameterTypeName", CodeGeneratorInternal::makeQuotedTypeList( method._listParameterTypeName ) );

            emit.line( "#if !defined( SW_SHIPPING )" );
            emitCommonEditorMeta( emit, method, "funcInfo._metadata." );
            emit.flagIf( method._bCallInEditor != 0, "funcInfo._metadata._bCallInEditor", "SW_TRUE" );
            emitCustomMetaMap( emit, method, "funcInfo._metadata." );
            emit.line( "#endif" );

            if ( method._netRole != FunctionNetRole::Local )
                emit.assign( "funcInfo._metadata._netRole", toCppExpr( method._netRole ) );

            emit.flagIf( method._bReliable != 0, "funcInfo._metadata._bReliable", "SW_TRUE" );
            emit.flagIf( method._bValidate != 0, "funcInfo._metadata._bValidate", "SW_TRUE" );
            emit.flagIf( method._bConstructor != 0, "funcInfo._metadata._bConstructor", "SW_TRUE" );
            emit.flagIf( method._bStatic != 0, "funcInfo._metadata._bStatic", "SW_TRUE" );
            emit.flagIf( method._bConst != 0, "funcInfo._metadata._bConst", "SW_TRUE" );

            const string callArgs = CodeGeneratorInternal::makeInvokerCallArgs( method._listParameterTypeName );

            emitMethodInvoker( emit, typeInfo, method, retType, callArgs );
            emit.pop();
            emit.line( "}" );
        }
    }

    string CodeGenerator::getModuleName() const
    {
        const ParserClangConfig& config = ParserContext::getSharedConfig();

        // 절대 경로로 매칭하면 리포지토리를 담은 상위 폴더 이름(예: .../AppData/..., D:/Games/...)이
        // 규칙에 걸려 모든 타입이 엉뚱한 모듈로 등록된다. 소스 루트 기준 상대 경로로만 본다.
        string relativePath = _sourceFilePath;
        if ( _sourceRoot.empty() == false )
        {
            const size_t rootPos = _sourceFilePath.find( _sourceRoot );
            if ( rootPos != string::npos )
                relativePath = _sourceFilePath.substr( rootPos + _sourceRoot.size() );
        }

        for ( const ParserClangConfig::ModuleRule& rule : config._listModuleRule )
        {
            if ( rule._pathContains.empty() == false && relativePath.find( rule._pathContains ) != string::npos )
                return rule._module;
        }
        return config._defaultModule;
    }

    void CodeGenerator::emitTypeRegistrar( CodeEmitBuffer& out, const ParsedTypeInfo& typeInfo ) const
    {
        const string registrarName = sanitizeIdentifier( typeInfo._fullyQualifiedName );

        CodeEmitBuffer flagsBuf;
        {
            CodeEmit fe( flagsBuf );
            fe.push( 3 );
            fe.flagIf( typeInfo._bAbstract, "info._bAbstract" );
            fe.flagIf( typeInfo._bStatic, "info._bStatic" );
        }

        appendTemplate( out, tplConstants::kTypeRegistrarBegin, {
                                                                    {        templateKeyConstants::kId,                registrarName},
                                                                    {       templateKeyConstants::kFqn, typeInfo._fullyQualifiedName},
                                                                    {      templateKeyConstants::kName,               typeInfo._name},
                                                                    { templateKeyConstants::kParentFqn,          typeInfo._parentFQN},
                                                                    {templateKeyConstants::kModuleName,              getModuleName()},
                                                                    {     templateKeyConstants::kFlags,    string( flagsBuf.view() )},
        } );

        CodeEmit emit( out );
        emit.push( 3 );

        emit.line( "#if !defined( SW_SHIPPING )" );
        emitCommonEditorMeta( emit, typeInfo, "info._metadata." );
        emit.flagIf( typeInfo._bHideInMenu != 0, "info._metadata._bHideInMenu", "SW_TRUE" );
        emitCustomMetaMap( emit, typeInfo, "info._metadata." );
        emit.line( "#endif" );

        if ( typeInfo._listProperty.empty() == false )
        {
            emit.line( "info._listProperty =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
                emitPropertyInfoEntry( emit, typeInfo, prop );
            emit.pop();
            emit.line( "};" );
        }

        if ( typeInfo._listMethod.empty() == false )
            emitMethodList( emit, typeInfo );

        appendTemplate( out, tplConstants::kTypeRegistrarEnd,
                        {
                            { templateKeyConstants::kId, registrarName },
                            { templateKeyConstants::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( typeInfo._listAlias, typeInfo._fullyQualifiedName, false ) }
        } );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
    {
        const string registrarName = sanitizeIdentifier( enumInfo._fullyQualifiedName );

        const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = findEnumerator( enumInfo, enumInfo._countEnumerator );

        appendTemplate( out, tplConstants::kEnumRegistrarBegin, {
                                                                    {          templateKeyConstants::kId,                                                         registrarName},
                                                                    {         templateKeyConstants::kFqn,                                          enumInfo._fullyQualifiedName},
                                                                    {        templateKeyConstants::kName,                                                        enumInfo._name},
                                                                    {  templateKeyConstants::kModuleName,                                                       getModuleName()},
                                                                    {   templateKeyConstants::kIsBitFlag,                               enumInfo._bIsBitFlag ? "true" : "false"},
                                                                    {  templateKeyConstants::kHasInvalid,                               invalidEn != nullptr ? "true" : "false"},
                                                                    {templateKeyConstants::kInvalidValue, invalidEn != nullptr ? to_string( invalidEn->_value ) : string( "0" )},
                                                                    {    templateKeyConstants::kHasCount,                                 countEn != nullptr ? "true" : "false"},
                                                                    {  templateKeyConstants::kCountValue,     countEn != nullptr ? to_string( countEn->_value ) : string( "0" )},
        } );

        CodeEmit emit( out );
        emit.push( 3 );

        if ( enumInfo._listCustomMeta.empty() == false )
        {
            emit.line( "#if !defined( SW_SHIPPING )" );
            emit.line( "info._mapCustomMeta = {" );
            emit.push();
            for ( const auto& [key, val] : enumInfo._listCustomMeta )
                emit.linef( "{ %#, %# },", CodeEmit::hs( key ), CodeEmit::quoted( val ) );
            emit.pop();
            emit.line( "};" );
            emit.line( "#endif" );
        }

        if ( enumInfo._listEnumerator.empty() == false )
        {
            emit.line( "info._mapNameToValue =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                emit.linef( "{ %#, %# },", CodeEmit::hs( en._name ), en._value );
            emit.pop();
            emit.line( "};" );

            emit.line( "info._mapValueToName =" );
            emit.line( "{" );
            emit.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                emit.linef( "{ %#, %# },", en._value, CodeEmit::hs( en._name ) );
            emit.pop();
            emit.line( "};" );
        }

        for ( const auto& [alias, canonical] : enumInfo._listValueAlias )
        {
            emit.line( "{" );
            emit.push();
            emit.linef( "const auto it = info._mapNameToValue.find( %# );", CodeEmit::hs( canonical ) );
            emit.line( "if ( it != info._mapNameToValue.end() )" );
            emit.push();
            emit.linef( "info._mapNameToValue.insert_or_assign( %#, it->second );", CodeEmit::hs( alias ) );
            emit.pop();
            emit.pop();
            emit.line( "}" );
        }

        appendTemplate( out, tplConstants::kEnumRegistrarEnd,
                        {
                            { templateKeyConstants::kId, registrarName },
                            { templateKeyConstants::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( enumInfo._listAlias, enumInfo._fullyQualifiedName, true ) }
        } );
    }

    const ParsedEnumeratorInfo* CodeGenerator::findEnumerator( const ParsedEnumInfo& enumInfo, string_view spec )
    {
        if ( spec.empty() )
            return nullptr;
        const string_view leaf = CodeGeneratorInternal::enumeratorLeaf( spec );
        for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
        {
            if ( en._name == spec || en._name == leaf )
                return &en;
        }
        return nullptr;
    }

    bool CodeGenerator::emitGeneratedHeader() const
    {
        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( ParserContext::getSharedConfig()._emitAutoGeneratedBanner );
        emit.line( "#pragma once" );
        emit.blank();

        bool bNeedFlags = false;
        for ( const ParsedEnumInfo& enumInfo : _listEnum )
        {
            if ( enumInfo._bEmitFlagOps )
                bNeedFlags = true;
            if ( enumInfo._invalidEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._invalidEnumerator ) == nullptr )
                SW_LOG_WARNING( "ENUM(Invalid=%#) not found on %#", enumInfo._invalidEnumerator,
                                enumInfo._fullyQualifiedName );
            if ( enumInfo._countEnumerator.empty() == false && findEnumerator( enumInfo, enumInfo._countEnumerator ) == nullptr )
                SW_LOG_WARNING( "ENUM(Count=%#) not found on %#", enumInfo._countEnumerator,
                                enumInfo._fullyQualifiedName );
        }

        if ( bNeedFlags )
        {
            // 비트 연산자(|, &, ^, ~, |=, &=, ^=)와 hasFlag/hasAnyFlag/setFlag/clearFlag는 enum마다
            // 코드젠하지 않고 Core/Common/EnumUtil.h의 제네릭 sw::IsBitFlagEnum<E> 트레이트 + 전역
            // 스코프 SFINAE 연산자로 통일합니다 — 로직이 모든 enum에서 동일해 타입별 코드젠이 필요
            // 없습니다. 여기서는 그 트레이트를 opt-in 하는 한 줄짜리 명시적 특수화만 생성합니다.
            emit.line( "#include \"Core/Common/EnumUtil.h\"" );
            emit.blank();
            for ( const ParsedEnumInfo& enumInfo : _listEnum )
            {
                if ( enumInfo._bEmitFlagOps == 0 )
                    continue;
                emit.linef( "template <> struct sw::IsBitFlagEnum<%#> : std::true_type {};", enumInfo._fullyQualifiedName );
            }
            emit.blank();
        }

        const string newContent( buffer.view() );
        if ( FileUtil::fileExists( _outputHeaderPath ) )
        {
            string existingContent;
            FileUtil::readTextFile( _outputHeaderPath, existingContent );
            if ( existingContent.empty() == false && existingContent == newContent )
                return true;
        }
        if ( FileUtil::writeTextFile( _outputHeaderPath, newContent ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", _outputHeaderPath );
            return false;
        }
        SW_LOG_TRACE( "Generated: %#", _outputHeaderPath );
        return true;
    }

    string CodeGenerator::sanitizeIdentifier( string_view fqn )
    {
        string result( fqn );
        size_t pos = 0;
        while ( ( pos = result.find( "::", pos ) ) != string::npos )
        {
            result.replace( pos, 2, "_" );
            pos += 1;
        }
        for ( utf8& c : result )
        {
            const bool bIsAlphaNum = ( 'a' <= c && c <= 'z' ) || ( 'A' <= c && c <= 'Z' ) || ( '0' <= c && c <= '9' );
            if ( bIsAlphaNum == false && c != '_' )
                c = '_';
        }
        return result;
    }
} // namespace sw
