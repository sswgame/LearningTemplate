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

            static string enclosingNamespaceOf( const string& fqn )
            {
                return string{ ParserUtil::enclosingNamespaceOf( fqn ) };
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
                CodeEmit       e( buf );
                e.push( 3 );
                const utf8* fn = bEnum ? "registry.registerEnumAlias" : "registry.registerTypeAlias";
                for ( const string& alias : aliases )
                {
                    if ( alias.empty() || alias == canonical )
                        continue;
                    e.linef( "%#( \"%#\", \"%#\" );", fn, CodeEmit::escapeCppString( alias ),
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
            FileUtil::createDirectory( _outputDir );
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
    static void emitCommonEditorMeta( CodeEmit& e, const TParsed& parsed, const string& prefix )
    {
        e.assignQuotedIf( parsed._category.empty() == false, prefix + "_category", parsed._category );
        e.assignQuotedIf( parsed._displayName.empty() == false, prefix + "_displayName", parsed._displayName );
        e.assignQuotedIf( parsed._tooltip.empty() == false, prefix + "_tooltip", parsed._tooltip );
    }

    /**
     * @brief 커스텀 메타 페어 맵을 출력합니다. 세 scope 가 동일한 형태를 씁니다.
     */
    template <typename TParsed>
    static void emitCustomMetaMap( CodeEmit& e, const TParsed& parsed, const string& prefix )
    {
        if ( parsed._listCustomMeta.empty() )
            return;
        e.linef( "%#_mapCustomMeta = {", prefix );
        e.push();
        for ( const auto& [key, val] : parsed._listCustomMeta )
            e.linef( "{ %#, %# },", CodeEmit::hs( key ), CodeEmit::quoted( val ) );
        e.pop();
        e.line( "};" );
    }

    void CodeGenerator::emitPropertyMetadata( CodeEmit& e, const ParsedPropertyInfo& prop ) const
    {
        e.line( "#if !defined( SW_SHIPPING )" );
        emitCommonEditorMeta( e, prop, "p._metadata." );
        e.flagIf( prop._bHideInInspector != 0, "p._metadata._bHideInInspector", "SW_TRUE" );
        emitCustomMetaMap( e, prop, "p._metadata." );
        e.line( "#endif" );

        e.assignQuotedIf( prop._defaultValue.empty() == false, "p._metadata._defaultValue", prop._defaultValue );
        e.assignQuotedIf( prop._assetType.empty() == false, "p._metadata._assetType", prop._assetType );
        e.flagIf( prop._bReadOnly != 0, "p._metadata._bReadOnly", "SW_TRUE" );
        e.flagIf( prop._bXmlAttribute != 0, "p._metadata._bXmlAttribute", "SW_TRUE" );
        e.flagIf( prop._bAssetPath != 0, "p._metadata._bAssetPath", "SW_TRUE" );
        e.flagIf( prop._bPolymorphic != 0, "p._metadata._bPolymorphic", "SW_TRUE" );
        e.flagIf( prop._bTransient != 0, "p._metadata._bTransient", "SW_TRUE" );
        if ( prop._bHasRange != 0 )
        {
            e.linef( "p._metadata._minRange     = %#f;", prop._minRange );
            e.linef( "p._metadata._maxRange     = %#f;", prop._maxRange );
            e.assign( "p._metadata._bHasRange", "SW_TRUE" );
        }
    }

    void CodeGenerator::emitNestedContainerTree( CodeEmit& e, const ParsedTypeInfo& typeInfo,
                                                 const ParsedPropertyInfo& prop ) const
    {
        if ( prop._containerTree == nullptr || prop._containerTree->_bIsContainer == SW_FALSE )
            return;

        const utf8*  outerKind    = containerKindExpr( prop._containerKind );
        const string outerWrapper = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

        e.line( "{" );
        e.push();
        e.line( "auto nested0 = sw::make_shared<sw::NestedContainerInfo>();" );
        e.assign( "nested0->_kind", outerKind );
        e.linef( "nested0->_typeName = %#;", CodeEmit::hs( prop._containerTree->_typeName ) );
        e.linef( "nested0->_elementTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
        e.linef( "nested0->_keyTypeName = %#;", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
        e.linef( "nested0->_wrapper = sw::make_shared<%#>();", outerWrapper );

        const ParsedContainerNode* node     = prop._containerTree->_elementNested.get();
        ContainerKind              prevKind = prop._containerKind;
        int32                      depth    = 1;

        if ( node != nullptr && node->_bIsContainer )
        {
            e.linef( "using NestC0 = decltype( std::declval<%#>().%# );", typeInfo._fullyQualifiedName, prop._name );
            while ( node != nullptr && node->_bIsContainer && depth < CodeGeneratorInternal::kMaxNestedContainerDepth )
            {
                const utf8* kind = containerKindExpr( node->_containerKind );
                const utf8* peel = peelMember( prevKind );
                e.linef( "using NestC%# = typename NestC%#::%#;", depth, depth - 1, peel );

                const string wrapperType = CodeGeneratorInternal::makeNestedWrapperType( node->_containerType, depth );
                e.line( "{" );
                e.push();
                e.linef( "auto nested%# = sw::make_shared<sw::NestedContainerInfo>();", depth );
                e.linef( "nested%#->_kind = %#;", depth, kind );
                e.linef( "nested%#->_typeName = %#;", depth, CodeEmit::hs( node->_typeName ) );
                e.linef( "nested%#->_elementTypeName = %#;", depth,
                         CodeEmit::hs( normalizeTypeName( node->_elementTypeName ) ) );
                e.linef( "nested%#->_keyTypeName = %#;", depth, CodeEmit::hs( node->_keyTypeName ) );
                e.linef( "nested%#->_wrapper = sw::make_shared<%#>();", depth, wrapperType );
                e.linef( "nested%#->_elementNested = nested%#;", depth - 1, depth );
                e.pop();
                e.line( "}" );

                prevKind = node->_containerKind;
                node     = ( node->_elementNested != nullptr ) ? node->_elementNested.get() : nullptr;
                ++depth;
            }
        }

        e.assign( "p._nestedContainer", "nested0" );
        e.pop();
        e.line( "}" );
    }

    void CodeGenerator::emitPropertyInfoEntry( CodeEmit& e, const ParsedTypeInfo& typeInfo,
                                               const ParsedPropertyInfo& prop ) const
    {
        e.line( "[]() {" );
        e.push();
        // PROPERTY() 에 값으로 담으면 안 되는 기반 타입을 컴파일 타임에 막는다.
        // 목록은 parser_config 의 emit.value_forbidden_base_types 에서 온다(비면 생략).
        e.linef( "using PropDecl = decltype(%#::%#);", typeInfo._fullyQualifiedName, prop._name );
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
            e.linef( "constexpr bool kIsInvalidValue = std::is_pointer_v<std::remove_cv_t<std::remove_reference_t<PropDecl>>> == false && (%#);",
                     condition );
            e.linef( "static_assert(!kIsInvalidValue, \"%#\");", cfg._valueForbiddenMessage );
        }
        e.line( "sw::PropertyInfo p(" );
        e.push();
        e.linef( "%#,", CodeEmit::hs( prop._name ) );
        e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._typeName ) ) );
        if ( prop._bIsBitField == SW_TRUE )
        {
            e.linef( "%#u,", prop._byteOffset );
        }
        else
        {
            e.linef( "offsetof(%#, %#),", typeInfo._fullyQualifiedName, prop._name );
        }

        if ( prop._bIsContainer )
        {
            const utf8*  kindStr     = containerKindExpr( prop._containerKind );
            const string wrapperType = CodeGeneratorInternal::makeWrapperType( prop._containerType, typeInfo._fullyQualifiedName, prop._name );

            e.line( "true," );
            e.linef( "%#,", kindStr );
            e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._elementTypeName ) ) );
            e.linef( "%#,", CodeEmit::hs( normalizeTypeName( prop._keyTypeName ) ) );
            e.linef( "sw::make_shared<%#>() );", wrapperType );

            emitNestedContainerTree( e, typeInfo, prop );
        }
        else
        {
            e.line( "false, sw::ContainerKind::None," );
            e.line( "::sw::hashed_string(), ::sw::hashed_string(), nullptr );" );
        }

        e.pop(); // 생성자 인자 들여쓰기
        if ( prop._bIsBitField == SW_TRUE )
        {
            e.line( "p._bIsBitField = SW_TRUE;" );
            e.linef( "p._bitOffset = %#;", prop._bitOffset );
            e.linef( "p._bitMask = %#;", prop._bitMask );
        }
        if ( prop._listAlias.empty() == false )
        {
            e.line( "p._listAlias = {" );
            e.push();
            for ( const string& alias : prop._listAlias )
                e.linef( "%#,", CodeEmit::hs( alias ) );
            e.pop();
            e.line( "};" );
        }
        emitPropertyMetadata( e, prop );
        e.line( "return p;" );
        e.pop();
        e.line( "}()," );
    }

    void CodeGenerator::emitMethodInvoker( CodeEmit& e, const ParsedTypeInfo& typeInfo,
                                           const ParsedFunctionInfo& method, const string& retType,
                                           const string& callArgs ) const
    {
        e.line( "auto invokerCb = []( void* objPtr, const ::sw::TaskArgs& args ) -> ::sw::TaskValue" );
        e.line( "{" );
        e.push();
        if ( method._listParameterTypeName.empty() )
            e.line( "(void)args;" );

        if ( method._bStatic != 0 && method._bConstructor == SW_FALSE )
        {
            e.line( "(void)objPtr;" );
            if ( retType == annotationConstants::kVoidTypeName )
            {
                e.linef( "%#::%#(%#);", typeInfo._fullyQualifiedName, method._name, callArgs );
                e.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                e.linef( "return ::sw::TaskValue{ %#::%#(%#) };", typeInfo._fullyQualifiedName, method._name, callArgs );
            }
        }
        else
        {
            e.linef( "auto* self = static_cast<%#*>( objPtr );", typeInfo._fullyQualifiedName );
            if ( method._bConstructor != 0 )
            {
                e.linef( "new ( self ) %#(%#);", typeInfo._fullyQualifiedName, callArgs );
                e.line( "return ::sw::TaskValue{};" );
            }
            else if ( retType == annotationConstants::kVoidTypeName )
            {
                e.linef( "self->%#(%#);", method._name, callArgs );
                e.line( "return ::sw::TaskValue{};" );
            }
            else
            {
                e.linef( "return ::sw::TaskValue{ self->%#(%#) };", method._name, callArgs );
            }
        }
        e.pop();
        e.line( "};" );
        e.line( "funcInfo._invoker = SW_DELEGATE_LAMBDA( ::sw::Delegate<::sw::TaskValue( void*, const ::sw::TaskArgs& )>, invokerCb );" );
        e.line( "info._listMethod.push_back( funcInfo );" );
    }

    void CodeGenerator::emitMethodList( CodeEmit& e, const ParsedTypeInfo& typeInfo ) const
    {
        for ( const ParsedFunctionInfo& method : typeInfo._listMethod )
        {
            const string retType = normalizeTypeName( method._returnTypeName );

            const string lookupName = ( method._bConstructor != 0 ) ? CodeGeneratorInternal::makeCtorLookupName( method ) : method._name;

            e.line( "{" );
            e.push();
            e.line( "::sw::FunctionInfo funcInfo;" );
            e.assign( "funcInfo._name", CodeEmit::quoted( ( method._bConstructor != 0 ) ? annotationConstants::kCtorLookupName : method._name ) );
            e.linef( "funcInfo._hashName       = %#;", CodeEmit::hs( lookupName ) );
            e.assign( "funcInfo._returnTypeName", CodeEmit::quoted( retType ) );
            e.assign( "funcInfo._listParameterTypeName", CodeGeneratorInternal::makeQuotedTypeList( method._listParameterTypeName ) );

            e.line( "#if !defined( SW_SHIPPING )" );
            emitCommonEditorMeta( e, method, "funcInfo._metadata." );
            e.flagIf( method._bCallInEditor != 0, "funcInfo._metadata._bCallInEditor", "SW_TRUE" );
            emitCustomMetaMap( e, method, "funcInfo._metadata." );
            e.line( "#endif" );

            if ( method._netRole != FunctionNetRole::Local )
                e.assign( "funcInfo._metadata._netRole", toCppExpr( method._netRole ) );

            e.flagIf( method._bReliable != 0, "funcInfo._metadata._bReliable", "SW_TRUE" );
            e.flagIf( method._bValidate != 0, "funcInfo._metadata._bValidate", "SW_TRUE" );
            e.flagIf( method._bConstructor != 0, "funcInfo._metadata._bConstructor", "SW_TRUE" );
            e.flagIf( method._bStatic != 0, "funcInfo._metadata._bStatic", "SW_TRUE" );
            e.flagIf( method._bConst != 0, "funcInfo._metadata._bConst", "SW_TRUE" );

            const string callArgs = CodeGeneratorInternal::makeInvokerCallArgs( method._listParameterTypeName );

            emitMethodInvoker( e, typeInfo, method, retType, callArgs );
            e.pop();
            e.line( "}" );
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
        const string id = sanitizeIdentifier( typeInfo._fullyQualifiedName );

        CodeEmitBuffer flagsBuf;
        {
            CodeEmit fe( flagsBuf );
            fe.push( 3 );
            fe.flagIf( typeInfo._bAbstract, "info._bAbstract" );
            fe.flagIf( typeInfo._bStatic, "info._bStatic" );
        }

        appendTemplate( out, tplConstants::kTypeRegistrarBegin, {
                                                                    {        templateKeyConstants::kId,                           id},
                                                                    {       templateKeyConstants::kFqn, typeInfo._fullyQualifiedName},
                                                                    {      templateKeyConstants::kName,               typeInfo._name},
                                                                    { templateKeyConstants::kParentFqn,          typeInfo._parentFQN},
                                                                    {templateKeyConstants::kModuleName,              getModuleName()},
                                                                    {     templateKeyConstants::kFlags,    string( flagsBuf.view() )},
        } );

        CodeEmit e( out );
        e.push( 3 );

        e.line( "#if !defined( SW_SHIPPING )" );
        emitCommonEditorMeta( e, typeInfo, "info._metadata." );
        e.flagIf( typeInfo._bHideInMenu != 0, "info._metadata._bHideInMenu", "SW_TRUE" );
        emitCustomMetaMap( e, typeInfo, "info._metadata." );
        e.line( "#endif" );

        if ( typeInfo._listProperty.empty() == false )
        {
            e.line( "info._listProperty =" );
            e.line( "{" );
            e.push();
            for ( const ParsedPropertyInfo& prop : typeInfo._listProperty )
                emitPropertyInfoEntry( e, typeInfo, prop );
            e.pop();
            e.line( "};" );
        }

        if ( typeInfo._listMethod.empty() == false )
            emitMethodList( e, typeInfo );

        appendTemplate( out, tplConstants::kTypeRegistrarEnd,
                        {
                            { templateKeyConstants::kId, id },
                            { templateKeyConstants::kAliasRegs,
                             CodeGeneratorInternal::emitAliasRegisterLines( typeInfo._listAlias, typeInfo._fullyQualifiedName, false ) }
        } );
    }

    void CodeGenerator::emitEnumRegistrar( CodeEmitBuffer& out, const ParsedEnumInfo& enumInfo ) const
    {
        const string id = sanitizeIdentifier( enumInfo._fullyQualifiedName );

        const ParsedEnumeratorInfo* invalidEn = findEnumerator( enumInfo, enumInfo._invalidEnumerator );
        const ParsedEnumeratorInfo* countEn   = findEnumerator( enumInfo, enumInfo._countEnumerator );

        appendTemplate( out, tplConstants::kEnumRegistrarBegin, {
                                                                    {          templateKeyConstants::kId,                                                                    id},
                                                                    {         templateKeyConstants::kFqn,                                          enumInfo._fullyQualifiedName},
                                                                    {        templateKeyConstants::kName,                                                        enumInfo._name},
                                                                    {  templateKeyConstants::kModuleName,                                                       getModuleName()},
                                                                    {   templateKeyConstants::kIsBitFlag,                               enumInfo._bIsBitFlag ? "true" : "false"},
                                                                    {  templateKeyConstants::kHasInvalid,                               invalidEn != nullptr ? "true" : "false"},
                                                                    {templateKeyConstants::kInvalidValue, invalidEn != nullptr ? to_string( invalidEn->_value ) : string( "0" )},
                                                                    {    templateKeyConstants::kHasCount,                                 countEn != nullptr ? "true" : "false"},
                                                                    {  templateKeyConstants::kCountValue,     countEn != nullptr ? to_string( countEn->_value ) : string( "0" )},
        } );

        CodeEmit e( out );
        e.push( 3 );

        if ( enumInfo._listCustomMeta.empty() == false )
        {
            e.line( "#if !defined( SW_SHIPPING )" );
            e.line( "info._mapCustomMeta = {" );
            e.push();
            for ( const auto& [key, val] : enumInfo._listCustomMeta )
                e.linef( "{ %#, %# },", CodeEmit::hs( key ), CodeEmit::quoted( val ) );
            e.pop();
            e.line( "};" );
            e.line( "#endif" );
        }

        if ( enumInfo._listEnumerator.empty() == false )
        {
            e.line( "info._mapNameToValue =" );
            e.line( "{" );
            e.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                e.linef( "{ %#, %# },", CodeEmit::hs( en._name ), en._value );
            e.pop();
            e.line( "};" );

            e.line( "info._mapValueToName =" );
            e.line( "{" );
            e.push();
            for ( const ParsedEnumeratorInfo& en : enumInfo._listEnumerator )
                e.linef( "{ %#, %# },", en._value, CodeEmit::hs( en._name ) );
            e.pop();
            e.line( "};" );
        }

        for ( const auto& [alias, canonical] : enumInfo._listValueAlias )
        {
            e.line( "{" );
            e.push();
            e.linef( "const auto it = info._mapNameToValue.find( %# );", CodeEmit::hs( canonical ) );
            e.line( "if ( it != info._mapNameToValue.end() )" );
            e.push();
            e.linef( "info._mapNameToValue.insert_or_assign( %#, it->second );", CodeEmit::hs( alias ) );
            e.pop();
            e.pop();
            e.line( "}" );
        }

        appendTemplate( out, tplConstants::kEnumRegistrarEnd,
                        {
                            { templateKeyConstants::kId, id },
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
        CodeEmit       e( buffer );
        e.line( ParserContext::getSharedConfig()._emitAutoGeneratedBanner );
        e.line( "#pragma once" );
        e.blank();

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
            e.line( "#include \"Engine/EngineMinimal.h\"" );
            e.blank();
        }

        for ( const ParsedEnumInfo& enumInfo : _listEnum )
        {
            if ( enumInfo._bEmitFlagOps == 0 )
                continue;

            const string& fqn = enumInfo._fullyQualifiedName;
            const string  ns  = CodeGeneratorInternal::enclosingNamespaceOf( fqn );
            if ( ns.empty() == false )
            {
                e.linef( "namespace %#", ns );
                e.line( "{" );
            }
            e.linef( "SW_INLINE constexpr %# operator|( %# lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
            e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );", fqn );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %# operator&( %# lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
            e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );", fqn );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %# operator^( %# lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
            e.linef( "return static_cast<%#>( static_cast<Underlying>( lhs ) ^ static_cast<Underlying>( rhs ) );", fqn );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %# operator~( %# val )", fqn, fqn );
            e.line( "{" );
            e.push();
            e.linef( "using Underlying = std::underlying_type_t<%#>;", fqn );
            e.linef( "return static_cast<%#>( ~static_cast<Underlying>( val ) );", fqn );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %#& operator|=( %#& lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.line( "return lhs = lhs | rhs;" );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %#& operator&=( %#& lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.line( "return lhs = lhs & rhs;" );
            e.pop();
            e.line( "}" );
            e.linef( "SW_INLINE constexpr %#& operator^=( %#& lhs, %# rhs )", fqn, fqn, fqn );
            e.line( "{" );
            e.push();
            e.line( "return lhs = lhs ^ rhs;" );
            e.pop();
            e.line( "}" );
            e.blank();
            if ( ns.empty() == false )
                e.line( "}" );
            e.blank();
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
