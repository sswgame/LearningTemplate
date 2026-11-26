/**
 * @file ParserDefines.h
 * @brief ReflectionParser — 어노테이션/CLI/템플릿 계약 상수
 * @details clang 인자·SDK 상대경로·컴포넌트 베이스 타입 등은
 *          Config/Environment/parser_config.defaults.json 에서 로드합니다. 산출물 확장자 · 스탬프 · 머리 글처럼 CMake 와 함께 지키는 생성 계약은
 *          `sw::config`(Scripts/common/project_constants.py 한 곳)에 있습니다.
 *          여기에는 ReflectionMacros / 생성 코드와 맞춰야 하는 컴파일 타임 계약만 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

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
         * @details `FUNCTION( Server )` 는 키가 아니라 역할 이름을 적습니다(토큰 자체가 값).
         *          그래서 적용할 때는 이 이름의 필드에 역할 이름을 값으로 넘깁니다.
         */
        inline constexpr const utf8* kNetRoleField = "NetRole";

        inline constexpr const utf8* kReflectBodyPrefix   = "REFLECT_BODY";
        inline constexpr const utf8* kReflectBodyMarkerFn = "__sw_reflect_body";
        /** @brief `REFLECT_INTERFACE( I, _port )` 의 애노테이션 머리와 표식 함수 이름 머리입니다(`ReflectionMacros.h`). */
        inline constexpr const utf8* kReflectInterfacePrefix   = "REFLECT_INTERFACE;";
        inline constexpr const utf8* kReflectInterfaceMarkerFn = "__sw_reflect_interface_";
        inline constexpr const utf8* kCtorLookupName           = "$ctor";
        inline constexpr const utf8* kVoidTypeName             = "void";
        inline constexpr const utf8* kDefaultMethodCategory    = "General";
        inline constexpr const utf8* kConstructorCategory      = "Constructor";
        /** @brief 끝에 오면 개수 표식으로 보는 열거자 이름입니다(`ENUM( Count = … )` 을 적지 않아도 된다). */
        inline constexpr const utf8* kCountEnumerator = "Count";

        /**
         * @brief 서버 버스 본문 타입의 표식 멤버(`static constexpr uint32 kBusVersion`)입니다(`ServerBusTopic.h` 의 `HasServerBusVersion`).
         * @details 이 멤버가 있는 `REFLECT` 타입은 다른 모듈의 `ServerBusTopic<T>` 가 `T::StaticType()` 을 부르므로 모듈 내보내기 매크로가 있어야 합니다.
         */
        inline constexpr const utf8* kBusVersionMember = "kBusVersion";

        /**
         * @brief 리플렉션 이벤트가 되는 필드 타입입니다 — `PROPERTY()` 가 붙은 멀티캐스트 델리게이트는 값이 아니라 이벤트로 수집합니다.
         * @details 판정은 형식의 정체(`typeidentity::kMulticastDelegate`)로, 인자 이름은 소스 토큰의 템플릿 이름(`kEventTemplateLeaf`)부터 읽습니다.
         */
        inline constexpr const utf8* kEventTemplateLeaf = "MulticastDelegate";

        /** @brief 검증 함수(`Validate = fn`)의 인자 타입 — 정규 이름(`sw::` 와 참조를 벗긴 것)입니다. */
        inline constexpr const utf8* kValidationContextTypeName = "ValidationContext";

        /** @brief C 고정 배열(`T _arr[N]`)이 타는 `ReflectBuiltins.xxx` CONTAINER 줄의 이름입니다 — 래퍼 줄기 · 종류 · 역할은 그 줄이 정한다(`std::array` 와 같다). */
        inline constexpr const utf8* kFixedArrayTypeName = "array";

        /** @brief 멤버의 상태 선언 `SW_TRANSIENT` · `SW_CODEC_STATE` 의 애노테이션 글입니다(`ReflectionMacros.h`). */
        inline constexpr const utf8* kTransientAnnotation  = "SW_TRANSIENT";
        inline constexpr const utf8* kCodecStateAnnotation = "SW_CODEC_STATE";
        /** @brief `SW_CODEC_STATE` 멤버를 싣는 메서드 이름입니다(상태 코덱 · 디렉터의 본문). */
        inline constexpr const utf8* kSerializeStateMethod     = "serializeState";
        inline constexpr const utf8* kSerializeGameStateMethod = "serializeGameState";
        /** @brief 비트필드의 남는 비트 이름 머리입니다 — 값이 아니라 상태 선언을 묻지 않는다. */
        inline constexpr const utf8* kReservedMemberPrefix = "_reserved";
    } // namespace annotation

    /**
     * @brief 생성기가 정체로 가르는 형식의 FQN 표입니다 — 형식 철자(`find( "AssetRef<" )`)가 아니라 clang 이 준 선언의 FQN(템플릿이면 기본 템플릿)과 견준다(`CursorReader::getTypeIdentity`).
     * @details 철자로 가르면 별칭 · 이름공간 생략 · 템플릿 인자 안의 같은 낱말에 속는다. 인라인 이름공간(`std::__1` · `std::__cxx11`)은 FQN 에 넣지 않는다.
     */
    namespace typeidentity
    {
        inline constexpr const utf8* kAssetRef             = "sw::AssetRef";
        inline constexpr const utf8* kDataPath             = "sw::DataPath";
        inline constexpr const utf8* kCatalogKey           = "sw::CatalogKey";
        inline constexpr const utf8* kTypedComponentHandle = "sw::TComponentHandle";
        inline constexpr const utf8* kComponentHandle      = "sw::ComponentHandle";
        inline constexpr const utf8* kDynamicBitset        = "sw::DynamicBitset";
        inline constexpr const utf8* kEnumArray            = "sw::EnumArray";
        inline constexpr const utf8* kMulticastDelegate    = "sw::MulticastDelegate";
        /** @brief 글(문자 원소가 `char`)로 싣는 문자열 템플릿입니다(`sw::string` 은 자체 클래스). */
        inline constexpr const utf8* kArrStringTemplate[] = { "sw::basic_string", "std::basic_string" };
        /** @brief 가변 길이 시퀀스로 싣는 템플릿입니다. */
        inline constexpr const utf8* kArrVectorTemplate[] = { "sw::vector", "std::vector" };
        /** @brief 고정 길이 시퀀스로 싣는 템플릿입니다. */
        inline constexpr const utf8* kArrArrayTemplate[] = { "sw::array", "std::array" };
        /**
         * @brief 이름 · 경로 칸 형식입니다(글 형식 `kArrStringTemplate` 과 함께) — 다른 데이터를 가리키는 칸이라 C++ 문자열 기본값이 엔진이 정한 기본값인지 묻는다
         *        (`PROPERTY( DefaultName )` · `Required`). 이것들의 `vector` 도 같다.
         */
        inline constexpr const utf8* kArrNameFieldType[] = { "sw::basic_hashed_string", "sw::DataPath", "sw::AssetID", "sw::TagID", "sw::AssetRef" };
        /**
         * @brief 값이 아니라 구독 · 연결 · 공유 에셋 · 잠금인 형식입니다 — 싣고 되살릴 값이 없어 상태 선언(`SW_TRANSIENT` · `SW_CODEC_STATE`)을 묻지 않는다
         *        (포인터 · 참조 · const 값 · `shared_ptr<const T>` 도 같다, `MemberCollector::hasStateValue`).
         */
        inline constexpr const utf8* kArrNonStateType[] = { "sw::Subscription", "sw::SubscriptionList", "sw::ItemSubscriptionList", "sw::Delegate", "sw::MulticastDelegate",
                                                            "sw::DelegateSlot", "sw::SharedAssetRef", "sw::AssetHandle", "sw::BoundAssetRef", "sw::AssetReloadWatch",
                                                            "sw::ConsumerLease", "sw::DevCounter", "sw::IdentityToken", "sw::ThreadRole", "sw::mutex", "std::weak_ptr" };
        /** @brief 나눠 쥐는 포인터 템플릿입니다(인자가 const 면 바꿀 수 없는 공유 데이터라 상태가 아니다). */
        inline constexpr const utf8* kArrSharedPointerTemplate[] = { "std::shared_ptr" };
    } // namespace typeidentity

    namespace fixedlayout
    {
        /** @brief 대상마다 정규 문자열을 모아 두는 진단 파일 이름입니다. 빌드는 읽지 않습니다. */
        inline constexpr const utf8* kSchemaListFile = "FixedSchemas.gen.txt";
    } // namespace fixedlayout

    // ------------------------------------------------------------------------------
    // 3) path — config 파일 이름 (ConfigConstants)
    // ------------------------------------------------------------------------------
    namespace parserpath
    {
        inline constexpr const utf8* kParserConfig         = sw::config::kFileParserConfig;
        inline constexpr const utf8* kParserConfigDefaults = sw::config::kFileParserDefaults;
        /** @brief Environment 툴체인 캐시 Config/Environment/toolchain_config.json */
        inline constexpr const utf8* kToolchainConfig = sw::config::kFileEnvToolchainConfig;
    } // namespace parserpath

    // ------------------------------------------------------------------------------
    // 4) parse — CLI 플래그 (cmake/Modules/Build/ReflectionCodeGen.cmake 계약)
    // ------------------------------------------------------------------------------
    namespace cli
    {
        inline constexpr const utf8* kInput           = "--input";
        inline constexpr const utf8* kOutput          = "--output";
        inline constexpr const utf8* kInclude         = "--include";
        inline constexpr const utf8* kBuiltins        = "--builtins";
        inline constexpr const utf8* kModule          = "--module";
        inline constexpr const utf8* kConfig          = "--config";
        inline constexpr const utf8* kEmitTemplates   = "--emit-templates";
        inline constexpr const utf8* kEmitBuiltinsGen = "--emit-builtins-gen";
        inline constexpr const utf8* kDepfile         = "--depfile";
        inline constexpr const utf8* kModuleLocal     = "--module-local";
        inline constexpr const utf8* kTargetArg       = "--target-arg";
    } // namespace cli

    // 5) emit — *.tpl 골격 이름은 `EmitTemplate`(EmitTemplateStore.h) 열거형 하나다.

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

        // parsing.*
        inline constexpr const utf8* kParsingComponentBaseTypes   = "component_base_types";
        inline constexpr const utf8* kParsingTypeStripPrefixes    = "type_strip_prefixes";
        inline constexpr const utf8* kParsingNameFieldSkipFolders = "name_field_skip_folders";
        inline constexpr const utf8* kParsingStateSkipPaths       = "state_declaration_skip_paths";

        // tuning.*
        inline constexpr const utf8* kMaxParallelParses = "max_parallel_parses";

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
        inline constexpr const utf8* kType           = "SW_REFLECT_BUILTIN_TYPE";
        inline constexpr const utf8* kContainer      = "SW_REFLECT_BUILTIN_CONTAINER";
        inline constexpr const utf8* kSkipNamespace  = "-";
        inline constexpr const utf8* kSkipAlias      = "_";
        inline constexpr const utf8* kTextConvPacked = "packed"; ///< TYPE 줄의 세 번째 칸(글 처리기)이 압축 타입을 뜻하는 값
        inline constexpr const utf8* kWidgetPacked   = "Packed"; ///< TYPE 줄의 Widget 칸 — 성분 실수로 편집한다(`PackedValueTraits` 가 있어야 하니 글 처리기도 packed 여야 한다)
        // Layout 칸(여섯 번째)의 철자 — 엔진 `ReflectedValue.h` 의 `SW_BUILTIN_LAYOUT_KIND_*` 와 같다.
        inline constexpr const utf8* kLayoutNone   = "none";
        inline constexpr const utf8* kLayoutNumber = "num";
        inline constexpr const utf8* kLayoutF32    = "f32";
        inline constexpr const utf8* kLayoutI32    = "i32";
        inline constexpr const utf8* kLayoutAngle  = "angle";
        // Roles 칸의 철자 — `|` 로 잇는다(엔진 `ReflectBuiltinRole` 의 이름과 같다).
        inline constexpr const utf8* kRoleNone      = "None";
        inline constexpr const utf8* kRoleInterp    = "Interp";
        inline constexpr const utf8* kRoleState     = "State";
        inline constexpr const utf8* kRoleFixed     = "Fixed"; ///< CONTAINER 줄만 — 크기가 고정이다
        inline constexpr utf8        kRoleSeparator = '|';
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
        inline constexpr const utf8* kSourcePath    = "SourcePath";
        inline constexpr const utf8* kBanner        = "Banner";
        inline constexpr const utf8* kID            = "ID";
        inline constexpr const utf8* kName          = "Name";
        inline constexpr const utf8* kFqn           = "FQN";
        inline constexpr const utf8* kKey           = "Key"; ///< 등록기 특수화의 열쇠 — 보통 FQN, 숨은 중첩 형식이면 생성한 열쇠 구조체 이름(`CodeGenerator::makeRegistrarKey`)
        inline constexpr const utf8* kModuleName    = "ModuleName";
        inline constexpr const utf8* kCppType       = "CppType";
        inline constexpr const utf8* kAliasTable    = "AliasTable"; ///< 옛 이름 표 줄(없으면 빈 글)
        inline constexpr const utf8* kAliases       = "Aliases";    ///< 서술자에 옛 이름 표를 잇는 조각(없으면 빈 글)
        inline constexpr const utf8* kIsBitFlag     = "IsBitFlag";
        inline constexpr const utf8* kInvalidName   = "InvalidName";
        inline constexpr const utf8* kCountName     = "CountName";
        inline constexpr const utf8* kEntryTable    = "EntryTable";
        inline constexpr const utf8* kRedirectTable = "RedirectTable";
        inline constexpr const utf8* kTables        = "Tables";
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
        /** @brief 생성 등록기를 담는 이름 공간 — C++ 만 아는 계약이라 설정에 두지 않는다. */
        inline constexpr const utf8* kGeneratedNsOpen  = "namespace sw::generated\n{\n";
        inline constexpr const utf8* kGeneratedNsClose = "} // namespace sw::generated\n\n";
    } // namespace emitdirective
} // namespace sw
