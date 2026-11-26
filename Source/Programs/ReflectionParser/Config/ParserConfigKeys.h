/**
 * @file ParserConfigKeys.h
 * @brief ReflectionParser 의 설정 파일 이름 · CLI 플래그 · 설정 JSON 키입니다(cmake/Modules/Build/ReflectionCodeGen.cmake · parser_config.defaults.json 과 맞춘다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // path — config 파일 이름 (ConfigConstants)
    // ------------------------------------------------------------------------------
    namespace parserpath
    {
        inline constexpr const utf8* kParserConfig         = sw::config::kFileParserConfig;
        inline constexpr const utf8* kParserConfigDefaults = sw::config::kFileParserDefaults;
        /** @brief Environment 툴체인 캐시 Config/Environment/toolchain_config.json */
        inline constexpr const utf8* kToolchainConfig = sw::config::kFileEnvToolchainConfig;
    } // namespace parserpath

    // ------------------------------------------------------------------------------
    // parse — CLI 플래그 (cmake/Modules/Build/ReflectionCodeGen.cmake 계약)
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
        inline constexpr const utf8* kTargetOption    = "--target-option";
    } // namespace cli

    // emit — *.tpl 골격 이름은 `EmitTemplate`(EmitTemplateStore.h) 열거형 하나다.

    // ------------------------------------------------------------------------------
    // parse — parser JSON 섹션/키 (defaults.json 스키마)
    // ------------------------------------------------------------------------------
    namespace jsonkey
    {
        // 중첩 스키마
        inline constexpr const utf8* kParserArgsSection  = "parser_args";
        inline constexpr const utf8* kArgsDefault        = "default";
        inline constexpr const utf8* kArgsPlatform       = "platform";
        inline constexpr const utf8* kArgsExtra          = "extra";
        inline constexpr const utf8* kArgsForceInclude   = "force_include";
        inline constexpr const utf8* kPaths              = "paths";
        inline constexpr const utf8* kClangFlags         = "clang_flags";
        inline constexpr const utf8* kParsing            = "parsing";
        inline constexpr const utf8* kTuning             = "tuning";
        inline constexpr const utf8* kTargetOptionMap    = "target_options";
        inline constexpr const utf8* kTargetOptionPass   = "target_option_pass_prefixes";
        inline constexpr const utf8* kTargetOptionIgnore = "target_option_ignored_prefixes";

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
        inline constexpr const utf8* kMaxParallelParses         = "max_parallel_parses";
        inline constexpr const utf8* kMinTranslationUnitsForPch = "min_translation_units_for_pch";

        // toolchain_config.json
        inline constexpr const utf8* kLlvmPath          = sw::config::kKeyLlvmPath;
        inline constexpr const utf8* kMsvcToolsDir      = sw::config::kKeyMsvcToolsDir;
        inline constexpr const utf8* kWindowsSdkDir     = sw::config::kKeyWindowsSdkDir;
        inline constexpr const utf8* kWindowsSdkVersion = sw::config::kKeyWindowsSdkVersion;
        inline constexpr const utf8* kEnvLlvmDir        = "LLVM_DIR";
        inline constexpr const utf8* kEnvLlvmHome       = "LLVM_HOME";
    } // namespace jsonkey

} // namespace sw
