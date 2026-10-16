/**
 * @file ParserDefines.h
 * @brief ReflectionParser — 어노테이션/CLI/템플릿 계약 상수
 * @details clang 인자·SDK 상대경로·emit 확장자·lookback·컴포넌트 베이스 타입 등은
 *          Config/Environment/parser_config.defaults.json 에서 로드합니다.
 *          여기에는 ReflectionMacros / 생성 코드와 맞춰야 하는 컴파일 타임 계약만 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) parse — clang annotate 매크로 (PredefinedReflectAnnotation.xxx)
    // ------------------------------------------------------------------------------
    /** @brief clang annotate 매크로 (PredefinedReflectAnnotation.xxx) */
    struct ReflectAnnotationDesc
    {
        const utf8* _pMacroName;
        const utf8* _pPrefix;
        const utf8* _pMacroOpen;
        const utf8* _pScope;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) parse — REFLECT/PROPERTY/FUNCTION 접두사·마커 (매크로 계약)
    // ------------------------------------------------------------------------------
    namespace annotation
    {
        /** @brief `k<ID>` 는 그 애노테이션의 설명자 한 벌입니다. 접두사와 매크로 철자를 따로 들고 다니지 않게 합니다. */
#define REGISTER_REFLECT_ANNOTATION( ID, MacroName, AnnotatePrefix, ScopeName )                          \
    inline constexpr ReflectAnnotationDesc k##ID{ MacroName, AnnotatePrefix, MacroName "(", ScopeName }; \
    inline constexpr const utf8*           k##ID##Macro     = MacroName;                                 \
    inline constexpr const utf8*           k##ID##Prefix    = AnnotatePrefix;                            \
    inline constexpr const utf8*           k##ID##MacroOpen = MacroName "(";                             \
    inline constexpr const utf8*           k##ID##Scope     = ScopeName;
#include "PredefinedReflectAnnotation.xxx"
#undef REGISTER_REFLECT_ANNOTATION

        /**
         * @brief 넷 역할 필드의 정규 이름입니다.
         * @details AnnotationMeta.txt 의 `netrole.Server = Server` 는 필드 이름 자리에 **역할**을 적습니다(토큰 자체가 값).
         *          그래서 적용할 때는 이 이름의 필드에 역할 이름을 값으로 넘깁니다.
         */
        inline constexpr const utf8* kNetRoleField = "NetRole";

        inline constexpr const utf8* kReflectBodyPrefix     = "REFLECT_BODY";
        inline constexpr const utf8* kReflectBodyMarkerFn   = "__sw_reflect_body";
        inline constexpr const utf8* kCtorLookupName        = "$ctor";
        inline constexpr const utf8* kVoidTypeName          = "void";
        inline constexpr const utf8* kDefaultMethodCategory = "General";
        inline constexpr const utf8* kConstructorCategory   = "Constructor";

        /**
         * @brief 리플렉션 이벤트가 되는 필드 타입입니다 — `PROPERTY()` 가 붙은 멀티캐스트 델리게이트는 값이 아니라 이벤트로 수집합니다.
         * @details 판정은 정규 타입 철자의 머리(`kEventTemplatePrefix`)로, 인자 이름은 소스 토큰의 템플릿 이름(`kEventTemplateLeaf`)부터 읽습니다.
         */
        inline constexpr const utf8* kEventTemplatePrefix = "sw::MulticastDelegate<";
        inline constexpr const utf8* kEventTemplateLeaf   = "MulticastDelegate";

        /** @brief 검증 함수(`Validate = fn`)의 인자 타입 — 정규 이름(`sw::` 와 참조를 벗긴 것)입니다. */
        inline constexpr const utf8* kValidationContextTypeName = "ValidationContext";

        /** @brief C 고정 배열(`T _arr[N]`)의 컨테이너 이름 · 래퍼 줄기입니다 — `std::array` 와 같은 `ArrayWrapper` 를 쓴다. */
        inline constexpr const utf8* kFixedArrayTypeName    = "array";
        inline constexpr const utf8* kFixedArrayWrapperStem = "Array";

        /** @brief `PROPERTY( Interp )` 를 받는 정규 타입 이름입니다 — 시퀀서 값 트랙이 섞을 수 있는 것(`PropertyRoleUtil::isInterpolatable` 과 같은 목록). */
        inline constexpr const utf8* kArrInterpTypeName[] = { "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32",
                                                              "uint64", "float32", "float64", "float2", "float3", "float4", "quaternion" };
    } // namespace annotation

    /** @brief 고정 배치형 코덱이 원소마다 실수 규칙으로 쓰는 수학 타입 한 줄입니다 — 정규 이름과 float32 멤버(선언 순서, 쉼표로 나눔). */
    struct FixedMathTypeDesc
    {
        const utf8* _pName;
        const utf8* _pMemberList;
    };

    namespace fixedlayout
    {
        /**
         * @brief 원소가 모두 float32 인 내장 수학 타입입니다(`ReflectBuiltins.xxx` 의 이름). 멤버 이름은 `Core/Math` 의 선언과 같아야 하고,
         *        생성 코드가 그 이름으로 멤버를 읽으므로 어긋나면 컴파일이 멈춥니다.
         * @details 핸들(`ComponentHandle` 등)처럼 모양이 같아도 뜻이 다른 타입이 섞이지 않도록 구조에서 추측하지 않고 목록으로 둡니다.
         */
        inline constexpr FixedMathTypeDesc kArrMathType[] = {
            {    "float2",                                                           "_x,_y"},
            {    "float3",                                                        "_x,_y,_z"},
            {    "float4",                                                     "_x,_y,_z,_w"},
            {"quaternion",                                                     "_x,_y,_z,_w"},
            {  "float4x4", "_11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44"},
        };
        /** @brief `.gen.cpp` 에 타입마다 적는 정규 문자열(또는 적격이 아닌 이유) 줄의 머리입니다. `FixedSchemas.gen.txt` 가 이 줄을 모읍니다. */
        inline constexpr const utf8* kSchemaLineMarker = "// FixedSchema: ";
        /** @brief 대상마다 정규 문자열을 모아 두는 진단 파일 이름입니다(빌드는 읽지 않는다). */
        inline constexpr const utf8* kSchemaListFile = "FixedSchemas.gen.txt";
        /** @brief 정수 범위의 상한입니다 — `Min` · `Max` 는 float32 로 읽으므로 그 너머의 정수는 정확하지 않습니다. */
        inline constexpr float64 kMaxExactIntegerRange = 16777216.0;
    } // namespace fixedlayout

    // ------------------------------------------------------------------------------
    // 3) path — config 파일 이름 (ConfigConstants)
    // ------------------------------------------------------------------------------
    namespace parserpath
    {
        inline constexpr const utf8* kParserConfig         = sw::config::kFileParserConfig;
        inline constexpr const utf8* kParserConfigDefaults = sw::config::kFileParserDefaults;
        /** @brief SetupEnvironment 툴체인 캐시 Config/Environment/toolchain_config.json */
        inline constexpr const utf8* kToolchainConfig = sw::config::kFileEnvToolchainConfig;
    } // namespace parserpath

    // ------------------------------------------------------------------------------
    // 4) parse — CLI 플래그 (cmake/Engine/ReflectionCodeGen.cmake 계약)
    // ------------------------------------------------------------------------------
    namespace cli
    {
        inline constexpr const utf8* kInput           = "--input";
        inline constexpr const utf8* kOutput          = "--output";
        inline constexpr const utf8* kInclude         = "--include";
        inline constexpr const utf8* kBuiltins        = "--builtins";
        inline constexpr const utf8* kAnnotationMeta  = "--annotation-meta";
        inline constexpr const utf8* kSourceRoot      = "--source-root";
        inline constexpr const utf8* kEmitTemplates   = "--emit-templates";
        inline constexpr const utf8* kEmitBuiltinsGen = "--emit-builtins-gen";
        inline constexpr const utf8* kDepfile         = "--depfile";
    } // namespace cli

    // ------------------------------------------------------------------------------
    // 5) emit — *.tpl 골격 이름 (Templates/ 파일 stem)
    // ------------------------------------------------------------------------------
    namespace templatefile
    {
        inline constexpr const utf8* kFileHeader           = "FileHeader";
        inline constexpr const utf8* kReflectTypeTraits    = "ReflectTypeTraits";
        inline constexpr const utf8* kTypeInfoAccessors    = "TypeInfoAccessors";
        inline constexpr const utf8* kTypeRegistrarBegin   = "TypeRegistrarBegin";
        inline constexpr const utf8* kTypeRegistrarEnd     = "TypeRegistrarEnd";
        inline constexpr const utf8* kEnumRegistrarBegin   = "EnumRegistrarBegin";
        inline constexpr const utf8* kEnumRegistrarEnd     = "EnumRegistrarEnd";
        inline constexpr const utf8* kBuiltinFileHeader    = "BuiltinFileHeader";
        inline constexpr const utf8* kBuiltinTypeRegistrar = "BuiltinTypeRegistrar";
        inline constexpr const utf8* kBuiltinFileFooter    = "BuiltinFileFooter";
    } // namespace templatefile

    // ------------------------------------------------------------------------------
    // 6) parse — parser JSON 섹션/키 (defaults.json 스키마)
    // ------------------------------------------------------------------------------
    namespace jsonkey
    {
        // 중첩 스키마
        inline constexpr const utf8* kParserArgsSection = "parser_args";
        inline constexpr const utf8* kArgsDefault       = "default";
        inline constexpr const utf8* kArgsPlatform      = "platform";
        inline constexpr const utf8* kArgsExtra         = "extra";
        inline constexpr const utf8* kArgsForceInclude  = "force_include";
        inline constexpr const utf8* kPaths             = "paths";
        inline constexpr const utf8* kClangFlags        = "clang_flags";
        inline constexpr const utf8* kEmit              = "emit";
        inline constexpr const utf8* kParsing           = "parsing";
        inline constexpr const utf8* kTuning            = "tuning";

        // paths.*
        inline constexpr const utf8* kLlvmClangRel     = "llvm_clang_rel";
        inline constexpr const utf8* kClangIncludeRel  = "clang_include_rel";
        inline constexpr const utf8* kMsvcIncludeRel   = "msvc_include_rel";
        inline constexpr const utf8* kWinSdkIncludeRel = "winsdk_include_rel";
        inline constexpr const utf8* kWinSdkUcrtRel    = "winsdk_ucrt_rel";

        // clang_flags.*
        inline constexpr const utf8* kFlagIncludePrefix      = "include_prefix";
        inline constexpr const utf8* kFlagIsystem            = "isystem";
        inline constexpr const utf8* kFlagResourceDir        = "resource_dir";
        inline constexpr const utf8* kFlagForceInclude       = "force_include";
        inline constexpr const utf8* kFlagFmsCompatibility   = "fms_compatibility";
        inline constexpr const utf8* kFlagFmsExtensions      = "fms_extensions";
        inline constexpr const utf8* kFlagFmsCompatVerPrefix = "fms_compat_version_prefix";

        // emit.*
        inline constexpr const utf8* kEmitCppExtension        = "cpp_extension";
        inline constexpr const utf8* kEmitHeaderExtension     = "header_extension";
        inline constexpr const utf8* kEmitTemplateExtension   = "template_extension";
        inline constexpr const utf8* kEmitAutoGeneratedBanner = "auto_generated_banner";
        inline constexpr const utf8* kEmitPlaceholderMarker   = "placeholder_marker";
        inline constexpr const utf8* kEmitRegenMarker         = "regen_by_parser_marker";
        inline constexpr const utf8* kEmitGeneratedNsOpen     = "generated_ns_open";
        inline constexpr const utf8* kEmitGeneratedNsClose    = "generated_ns_close";
        inline constexpr const utf8* kEmitFlagOpsHeader       = "flag_ops_header";
        inline constexpr const utf8* kEmitFlagOpsMarker       = "flag_ops_marker";
        inline constexpr const utf8* kEmitRegisterTypeMarker  = "register_type_marker";
        inline constexpr const utf8* kEmitRegisterEnumMarker  = "register_enum_marker";
        inline constexpr const utf8* kEmitSourcePathMarker    = "source_path_marker";

        // parsing.*
        inline constexpr const utf8* kParsingComponentBaseTypes = "component_base_types";
        inline constexpr const utf8* kParsingTypeStripPrefixes  = "type_strip_prefixes";
        inline constexpr const utf8* kParsingModuleRules        = "module_rules";
        inline constexpr const utf8* kParsingDefaultModule      = "default_module";
        inline constexpr const utf8* kModuleRulePathContains    = "path_contains";
        inline constexpr const utf8* kModuleRuleModule          = "module";
        inline constexpr const utf8* kEmitValueForbiddenBases   = "value_forbidden_base_types";
        inline constexpr const utf8* kEmitValueForbiddenMessage = "value_forbidden_message";

        // tuning.*
        inline constexpr const utf8* kSourceLookbackBytes = "source_lookback_bytes";

        // toolchain_config.json
        inline constexpr const utf8* kLlvmPath          = sw::config::kKeyLlvmPath;
        inline constexpr const utf8* kMsvcToolsDir      = sw::config::kKeyMsvcToolsDir;
        inline constexpr const utf8* kWindowsSdkDir     = sw::config::kKeyWindowsSdkDir;
        inline constexpr const utf8* kWindowsSdkVersion = sw::config::kKeyWindowsSdkVersion;
        inline constexpr const utf8* kEnvLlvmDir        = "LLVM_DIR";
        inline constexpr const utf8* kEnvLlvmHome       = "LLVM_HOME";
    } // namespace jsonkey

    // ------------------------------------------------------------------------------
    // 7) maps — ReflectBuiltins 매크로·스킵 토큰
    // ------------------------------------------------------------------------------
    namespace builtinmacro
    {
        inline constexpr const utf8* kType          = "SW_REFLECT_BUILTIN_TYPE";
        inline constexpr const utf8* kContainer     = "SW_REFLECT_BUILTIN_CONTAINER";
        inline constexpr const utf8* kSkipNamespace = "-";
        inline constexpr const utf8* kSkipAlias     = "_";
    } // namespace builtinmacro

    // ------------------------------------------------------------------------------
    // 8) parse — 소스 키워드 스캔
    // ------------------------------------------------------------------------------
    inline static constexpr const utf8* kSourceKeywordScan[] = {
        annotation::kReflectMacro,
        annotation::kPropertyMacro,
        annotation::kFunctionMacro,
        annotation::kEnumMacro,
    };

    // ------------------------------------------------------------------------------
    // 9) emit — 템플릿 치환 변수 키
    // ------------------------------------------------------------------------------
    namespace templatekey
    {
        inline constexpr const utf8* kSourcePath   = "SourcePath";
        inline constexpr const utf8* kID           = "ID";
        inline constexpr const utf8* kName         = "Name";
        inline constexpr const utf8* kFqn          = "FQN";
        inline constexpr const utf8* kParentFqn    = "ParentFQN";
        inline constexpr const utf8* kModuleName   = "ModuleName";
        inline constexpr const utf8* kCppType      = "CppType";
        inline constexpr const utf8* kFlags        = "Flags";
        inline constexpr const utf8* kAliasRegs    = "AliasRegs";
        inline constexpr const utf8* kIsBitFlag    = "IsBitFlag";
        inline constexpr const utf8* kHasInvalid   = "HasInvalid";
        inline constexpr const utf8* kInvalidValue = "InvalidValue";
        inline constexpr const utf8* kHasCount     = "HasCount";
        inline constexpr const utf8* kCountValue   = "CountValue";
        inline constexpr const utf8* kFixedCodec   = "FixedCodec";
    } // namespace templatekey

    // ------------------------------------------------------------------------------
    // 10) emit — 코드 생성 지시문 및 주석
    // ------------------------------------------------------------------------------
    namespace emitdirective
    {
        inline constexpr const utf8* kPragmaOnce   = "#pragma once";
        inline constexpr const utf8* kIfndefParser = "#if !defined(__REFLECT_PARSER__)";
        inline constexpr const utf8* kEndif        = "#endif";
        inline constexpr const utf8* kNoEnumFlags  = "// no ENUM(Flags) in this target";
    } // namespace emitdirective
} // namespace sw
