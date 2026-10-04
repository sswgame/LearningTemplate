/**
 * @file ReflectionDocWriter.h
 * @brief 등록된 리플렉션 타입 · 열거형으로 Markdown API 문서를 씁니다(`App --write-reflection-docs=<폴더>`). 빌드 산출물이며 커밋하지 않습니다.
 * @details 모듈(엔진 · GameFramework · 킷 · 게임)마다 한 장과 `index.md` 를 씁니다. 타입은 이름 순, 프로퍼티 · 함수 · 이벤트는 선언 순이라
 *          같은 등록이면 바이트까지 같은 문서가 나옵니다(학습용 문서가 이것을 끌어다 쓴다).
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct EnumInfo;
    struct TypeInfo;

    class TypeRegistry;
} // namespace sw

namespace sw
{
    /** @brief 리플렉션 → Markdown 입니다. */
    struct SW_API ReflectionDocWriter
    {
        /**
         * @brief @p registry 의 살아 있는 타입 · 열거형을 @p outputDir 에 씁니다(폴더가 없으면 만든다).
         * @return 쓴 파일 수. 하나도 쓰지 못했으면 0
         */
        static uint32 writeMarkdown( const TypeRegistry& registry, string_view outputDir );
        /** @brief 타입 한 절(제목 · 부모 · 프로퍼티 표 · 함수 · 이벤트)입니다. */
        static string makeTypeMarkdown( const TypeInfo& type );
        /** @brief 열거형 한 절(값 표)입니다. */
        static string makeEnumMarkdown( const EnumInfo& info );
        /** @brief 모듈 이름을 파일 이름으로 씁니다(`Engine` → `Engine.md`, 이름이 없으면 `Unknown.md`). */
        static string makeModuleFileName( const hashed_string& moduleName );
    };
} // namespace sw
