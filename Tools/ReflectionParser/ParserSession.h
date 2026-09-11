/**
 * @file ParserSession.h
 * @brief 파서 한 번 실행이 들고 가는 표 묶음 — `main` 이 소유하고 아래로 내려줍니다.
 *
 * @details 이 넷은 예전에 각자 `instance()` 싱글턴이었다. 파서가 `main` 하나짜리 단일 실행
 *          파일이라 당장의 해는 없었지만, 표를 채우는 곳(ReflectBuiltinsLoader)과 읽는 곳
 *          (AstVisitor·CodeGenerator·AnnotationApply)이 서로를 모르는 채로 전역을 통해
 *          이어져 있어서, 무엇이 언제 채워지는지가 호출 그래프에 드러나지 않았다.
 *
 *          `TypeNameMap::normalize` 를 감싸던 `normalizeTypeName()` 자유 함수는 없앴다 —
 *          호출부 열아홉 곳이 전역을 쓴다는 사실 자체를 감추고 있었다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "ReflectionParser/AnnotationMeta.h"
#include "ReflectionParser/ContainerTypeMap.h"
#include "ReflectionParser/EmitTemplateStore.h"
#include "ReflectionParser/TypeNameMap.h"

namespace sw
{
    /**
     * @struct ParserSession
     * @brief 파서 실행 한 번 동안 살아 있는 표들. 시작할 때 채우고 그 뒤로는 읽기만 합니다.
     */
    struct ParserSession
    {
        TypeNameMap       _typeNameMap;       ///< clang 표기 → canonical
        ContainerTypeMap  _containerTypeMap;  ///< 컨테이너 판별 규칙
        AnnotationMeta    _annotationMeta;    ///< 어노테이션 별칭 → 정규 필드명
        EmitTemplateStore _emitTemplateStore; ///< 코드 방출 템플릿
    };
} // namespace sw
