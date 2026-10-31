/**
 * @file ReflectBuiltinsLoader.h
 * @brief ReflectBuiltins.xxx 스캔 → TypeNameMap/ContainerTypeMap + TypeRegistrar .gen.cpp emit.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Programs/ReflectionParser/ParserSession.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) parse — ReflectBuiltins.xxx 스캔 → TypeNameMap / ContainerTypeMap
    // ------------------------------------------------------------------------------
    /** @brief 맵을 비운 뒤 builtins 헤더에서 등록을 채웁니다. */
    [[nodiscard]] bool loadReflectBuiltins( const string_view absPath, ParserSession& outSession );

    // ------------------------------------------------------------------------------
    // 2) emit — primitive TypeRegistrar .gen.cpp
    // ------------------------------------------------------------------------------
    /**
     * @brief builtins 헤더를 스캔해 primitive TypeRegistrar .gen.cpp 를 씁니다.
     * @details EmitTemplateStore 의 Builtin*.tpl 사용 (--emit-templates 선행 로드 필요).
     * @param moduleName 내장 타입이 오르는 런타임 모듈 이름(`--module`)입니다.
     */
    bool emitReflectBuiltinsGen( const string_view builtinsAbsPath, const string_view outCppAbsPath, const string_view moduleName,
                                 const ParserSession& session );
} // namespace sw
