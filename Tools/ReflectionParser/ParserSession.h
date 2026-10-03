/**
 * @file ParserSession.h
 * @brief 파서 실행 한 번이 들고 다니는 표 묶음입니다. `main` 이 소유하고 아래로 내려 줍니다.
 *
 * @details 싱글턴으로 두지 않습니다 — 표를 채우는 곳(ReflectBuiltinsLoader)과 읽는 곳(AstVisitor · CodeGenerator · AnnotationApply)이
 *          전역을 통해 이어지면 무엇이 언제 채워지는지가 호출 그래프에 드러나지 않습니다. 같은 이유로 `TypeNameMap::normalize` 를
 *          감싸는 자유 함수도 두지 않습니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ContainerTypeMap.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/ParserConfig.h"
#include "ReflectionParser/TypeNameMap.h"

namespace sw
{
    /**
     * @struct ParserSession
     * @brief 파서 실행 한 번 동안 살아 있는 표들입니다. 시작할 때 채우고 그 뒤로는 읽기만 합니다.
     */
    struct ParserSession
    {
        ParserConfig      _config;            ///< parser_config · toolchain_config 에서 읽은 설정(clang 인자 · 코드젠 표식 · 규칙)
        TypeNameMap       _typeNameMap;       ///< clang 표기 → canonical
        ContainerTypeMap  _containerTypeMap;  ///< 컨테이너 판별 규칙
        AnnotationMeta    _annotationMeta;    ///< 어노테이션 별칭 → 정규 필드명
        EmitTemplateStore _emitTemplateStore; ///< 코드 방출 템플릿
    };
} // namespace sw
