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
		const utf8* _macroName;
		const utf8* _prefix;
		const utf8* _macroOpen;
		const utf8* _scope;
	};

	inline static constexpr ReflectAnnotationDesc kReflectAnnotations[] = {
#define REGISTER_REFLECT_ANNOTATION( Id, MacroName, AnnotatePrefix, ScopeName ) \
	{ MacroName, AnnotatePrefix, MacroName "(", ScopeName },
#include "PredefinedReflectAnnotation.xxx"
#undef REGISTER_REFLECT_ANNOTATION
	};

	// ------------------------------------------------------------------------------
	// 2) parse — REFLECT/PROPERTY/FUNCTION 접두사·마커 (매크로 계약)
	// ------------------------------------------------------------------------------
	struct annotationConstants
	{
#define REGISTER_REFLECT_ANNOTATION( Id, MacroName, AnnotatePrefix, ScopeName ) \
	inline static constexpr const utf8* k##Id##Macro	 = MacroName;           \
	inline static constexpr const utf8* k##Id##Prefix	 = AnnotatePrefix;      \
	inline static constexpr const utf8* k##Id##MacroOpen = MacroName "(";       \
	inline static constexpr const utf8* k##Id##Scope	 = ScopeName;
#include "PredefinedReflectAnnotation.xxx"
#undef REGISTER_REFLECT_ANNOTATION

		inline static constexpr const utf8* kReflectBodyPrefix		  = "REFLECT_BODY";
		inline static constexpr const utf8* kComponentFactoryPrefix	  = "COMPONENT_FACTORY";
		inline static constexpr const utf8* kReflectBodyMarkerFn	  = "__sw_reflect_body";
		inline static constexpr const utf8* kComponentFactoryMarkerFn = "__sw_component_factory";
		inline static constexpr const utf8* kCtorLookupName			  = "$ctor";
		inline static constexpr const utf8* kVoidTypeName			  = "void";
		inline static constexpr const utf8* kDefaultMethodCategory	  = "General";
		inline static constexpr const utf8* kConstructorCategory	  = "Constructor";
	}; // struct annotationConstants

	// ------------------------------------------------------------------------------
	// 2.1) parse — AnnotationMeta 필드 Canonical 상수 계약
	// ------------------------------------------------------------------------------
	struct annotationFieldConstants
	{
		inline static constexpr const utf8* kAbstract		 = "Abstract";
		inline static constexpr const utf8* kStatic			 = "Static";
		inline static constexpr const utf8* kHideInMenu		 = "HideInMenu";
		inline static constexpr const utf8* kAlias			 = "Alias";
		inline static constexpr const utf8* kCategory		 = "Category";
		inline static constexpr const utf8* kDisplayName	 = "DisplayName";
		inline static constexpr const utf8* kTooltip		 = "Tooltip";
		inline static constexpr const utf8* kMeta			 = "Meta";
		inline static constexpr const utf8* kValueAlias		 = "ValueAlias";
		inline static constexpr const utf8* kFlags			 = "Flags";
		inline static constexpr const utf8* kInvalid		 = "Invalid";
		inline static constexpr const utf8* kCount			 = "Count";
		inline static constexpr const utf8* kReadOnly		 = "ReadOnly";
		inline static constexpr const utf8* kXmlAttribute	 = "XmlAttribute";
		inline static constexpr const utf8* kAssetPath		 = "AssetPath";
		inline static constexpr const utf8* kPolymorphic	 = "Polymorphic";
		inline static constexpr const utf8* kTransient		 = "Transient";
		inline static constexpr const utf8* kHideInInspector = "HideInInspector";
		inline static constexpr const utf8* kDefaultValue	 = "DefaultValue";
		inline static constexpr const utf8* kAssetType		 = "AssetType";
		inline static constexpr const utf8* kMinRange		 = "MinRange";
		inline static constexpr const utf8* kMaxRange		 = "MaxRange";
		inline static constexpr const utf8* kReliable		 = "Reliable";
		inline static constexpr const utf8* kValidate		 = "Validate";
		inline static constexpr const utf8* kCallInEditor	 = "CallInEditor";
	}; // struct annotationFieldConstants

	// ------------------------------------------------------------------------------
	// 3) path — config 파일 이름 (ConfigConstants)
	// ------------------------------------------------------------------------------
	struct pathConstants
	{
		inline static constexpr const utf8* kParserConfig		  = sw::config::kFileParserConfig;
		inline static constexpr const utf8* kParserConfigDefaults = sw::config::kFileParserDefaults;
		/** @brief SetupEnvironment 툴체인 캐시 Config/Environment/toolchain_config.json */
		inline static constexpr const utf8* kToolchainConfig = sw::config::kFileEnvToolchainConfig;
	}; // struct pathConstants

	// ------------------------------------------------------------------------------
	// 4) parse — CLI 플래그 (CMake Reflection.cmake 계약)
	// ------------------------------------------------------------------------------
	struct cliConstants
	{
		inline static constexpr const utf8* kInput			 = "--input";
		inline static constexpr const utf8* kOutput			 = "--output";
		inline static constexpr const utf8* kInclude		 = "--include";
		inline static constexpr const utf8* kBuiltins		 = "--builtins";
		inline static constexpr const utf8* kAnnotationMeta	 = "--annotation-meta";
		inline static constexpr const utf8* kEmitTemplates	 = "--emit-templates";
		inline static constexpr const utf8* kEmitBuiltinsGen = "--emit-builtins-gen";
	}; // struct cliConstants

	// ------------------------------------------------------------------------------
	// 5) emit — *.tpl 골격 이름 (Templates/ 파일 stem)
	// ------------------------------------------------------------------------------
	struct tplConstants
	{
		inline static constexpr const utf8* kFileHeader				   = "FileHeader";
		inline static constexpr const utf8* kReflectTypeTraits		   = "ReflectTypeTraits";
		inline static constexpr const utf8* kTypeInfoAccessors		   = "TypeInfoAccessors";
		inline static constexpr const utf8* kComponentFactoryRegistrar = "ComponentFactoryRegistrar";
		inline static constexpr const utf8* kTypeRegistrarBegin		   = "TypeRegistrarBegin";
		inline static constexpr const utf8* kTypeRegistrarEnd		   = "TypeRegistrarEnd";
		inline static constexpr const utf8* kEnumRegistrarBegin		   = "EnumRegistrarBegin";
		inline static constexpr const utf8* kEnumRegistrarEnd		   = "EnumRegistrarEnd";
		inline static constexpr const utf8* kBuiltinFileHeader		   = "BuiltinFileHeader";
		inline static constexpr const utf8* kBuiltinTypeRegistrar	   = "BuiltinTypeRegistrar";
		inline static constexpr const utf8* kBuiltinFileFooter		   = "BuiltinFileFooter";
	}; // struct tplConstants

	// ------------------------------------------------------------------------------
	// 6) parse — parser JSON 섹션/키 (defaults.json 스키마)
	// ------------------------------------------------------------------------------
	struct jsonKeyConstants
	{
		// Nested schema
		inline static constexpr const utf8* kParserArgsSection = "parser_args";
		inline static constexpr const utf8* kArgsDefault	   = "default";
		inline static constexpr const utf8* kArgsPlatform	   = "platform";
		inline static constexpr const utf8* kArgsExtra		   = "extra";
		inline static constexpr const utf8* kArgsForceInclude  = "force_include";
		inline static constexpr const utf8* kPaths			   = "paths";
		inline static constexpr const utf8* kClangFlags		   = "clang_flags";
		inline static constexpr const utf8* kEmit			   = "emit";
		inline static constexpr const utf8* kParsing		   = "parsing";
		inline static constexpr const utf8* kTuning			   = "tuning";

		// paths.*
		inline static constexpr const utf8* kLlvmClangRel	  = "llvm_clang_rel";
		inline static constexpr const utf8* kClangIncludeRel  = "clang_include_rel";
		inline static constexpr const utf8* kMsvcIncludeRel	  = "msvc_include_rel";
		inline static constexpr const utf8* kWinSdkIncludeRel = "winsdk_include_rel";
		inline static constexpr const utf8* kWinSdkUcrtRel	  = "winsdk_ucrt_rel";

		// clang_flags.*
		inline static constexpr const utf8* kFlagIncludePrefix		= "include_prefix";
		inline static constexpr const utf8* kFlagIsystem			= "isystem";
		inline static constexpr const utf8* kFlagResourceDir		= "resource_dir";
		inline static constexpr const utf8* kFlagForceInclude		= "force_include";
		inline static constexpr const utf8* kFlagFmsCompatibility	= "fms_compatibility";
		inline static constexpr const utf8* kFlagFmsExtensions		= "fms_extensions";
		inline static constexpr const utf8* kFlagFmsCompatVerPrefix = "fms_compat_version_prefix";

		// emit.*
		inline static constexpr const utf8* kEmitCppExtension		 = "cpp_extension";
		inline static constexpr const utf8* kEmitHeaderExtension	 = "header_extension";
		inline static constexpr const utf8* kEmitTemplateExtension	 = "template_extension";
		inline static constexpr const utf8* kEmitAutoGeneratedBanner = "auto_generated_banner";
		inline static constexpr const utf8* kEmitPlaceholderMarker	 = "placeholder_marker";
		inline static constexpr const utf8* kEmitRegenMarker		 = "regen_by_parser_marker";
		inline static constexpr const utf8* kEmitGeneratedNsOpen	 = "generated_ns_open";
		inline static constexpr const utf8* kEmitGeneratedNsClose	 = "generated_ns_close";
		inline static constexpr const utf8* kEmitFlagOpsHeader		 = "flag_ops_header";
		inline static constexpr const utf8* kEmitFlagOpsMarker		 = "flag_ops_marker";
		inline static constexpr const utf8* kEmitRegisterTypeMarker	 = "register_type_marker";
		inline static constexpr const utf8* kEmitRegisterEnumMarker	 = "register_enum_marker";

		// parsing.*
		inline static constexpr const utf8* kParsingComponentBaseTypes = "component_base_types";
		inline static constexpr const utf8* kParsingTypeStripPrefixes  = "type_strip_prefixes";

		// tuning.*
		inline static constexpr const utf8* kSourceLookbackBytes = "source_lookback_bytes";

		// toolchain_config.json
		inline static constexpr const utf8* kLlvmPath		   = sw::config::kKeyLlvmPath;
		inline static constexpr const utf8* kMsvcToolsDir	   = sw::config::kKeyMsvcToolsDir;
		inline static constexpr const utf8* kWindowsSdkDir	   = sw::config::kKeyWindowsSdkDir;
		inline static constexpr const utf8* kWindowsSdkVersion = sw::config::kKeyWindowsSdkVersion;
		inline static constexpr const utf8* kEnvLlvmDir		   = "LLVM_DIR";
		inline static constexpr const utf8* kEnvLlvmHome	   = "LLVM_HOME";
	}; // struct jsonKeyConstants

	// ------------------------------------------------------------------------------
	// 7) maps — ReflectBuiltins 매크로·스킵 토큰
	// ------------------------------------------------------------------------------
	struct builtinMacroConstants
	{
		inline static constexpr const utf8* kType		   = "SW_REFLECT_BUILTIN_TYPE";
		inline static constexpr const utf8* kContainer	   = "SW_REFLECT_BUILTIN_CONTAINER";
		inline static constexpr const utf8* kSkipNamespace = "-";
		inline static constexpr const utf8* kSkipAlias	   = "_";
	}; // struct builtinMacroConstants

	// ------------------------------------------------------------------------------
	// 8) parse — 소스 키워드 스캔
	// ------------------------------------------------------------------------------
	inline static constexpr const utf8* kSourceKeywordScan[] = {
		annotationConstants::kReflectMacro,
		annotationConstants::kPropertyMacro,
		annotationConstants::kFunctionMacro,
		annotationConstants::kEnumMacro,
	};

	// ------------------------------------------------------------------------------
	// 9) emit — 템플릿 치환 변수 키
	// ------------------------------------------------------------------------------
	struct templateKeyConstants
	{
		inline static constexpr const utf8* kSourcePath	  = "SourcePath";
		inline static constexpr const utf8* kId			  = "Id";
		inline static constexpr const utf8* kName		  = "Name";
		inline static constexpr const utf8* kFqn		  = "FQN";
		inline static constexpr const utf8* kParentFqn	  = "ParentFQN";
		inline static constexpr const utf8* kModuleName	  = "ModuleName";
		inline static constexpr const utf8* kCppType	  = "CppType";
		inline static constexpr const utf8* kFlags		  = "Flags";
		inline static constexpr const utf8* kAliasRegs	  = "AliasRegs";
		inline static constexpr const utf8* kIsBitFlag	  = "IsBitFlag";
		inline static constexpr const utf8* kHasInvalid	  = "HasInvalid";
		inline static constexpr const utf8* kInvalidValue = "InvalidValue";
		inline static constexpr const utf8* kHasCount	  = "HasCount";
		inline static constexpr const utf8* kCountValue	  = "CountValue";
	}; // struct templateKeyConstants

	// ------------------------------------------------------------------------------
	// 10) emit — 코드 생성 지시문 및 주석
	// ------------------------------------------------------------------------------
	struct emitDirectiveConstants
	{
		inline static constexpr const utf8* kPragmaOnce	  = "#pragma once";
		inline static constexpr const utf8* kIfndefParser = "#if !defined(__REFLECT_PARSER__)";
		inline static constexpr const utf8* kEndif		  = "#endif";
		inline static constexpr const utf8* kNoEnumFlags  = "// no ENUM(Flags) in this target";
	}; // struct emitDirectiveConstants
} // namespace sw
