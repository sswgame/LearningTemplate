/**
 * @file ContainerTreeBuilder.h
 * @brief 필드 타입에서 컨테이너 트리(Vector/Map 중첩)를 만들고 사용자 별칭을 벗깁니다.
 * @details 타입 선언의 `REFLECT_CONTAINER` 가 이기고, 없으면 ReflectBuiltins 의 표기 규칙(`ContainerTypeMap`)을 봅니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionEnumNames.h"

#include "Programs/ReflectionParser/ParsedReflection.h"
#include "Programs/ReflectionParser/ParserSession.h"

#include <clang-c/Index.h>

namespace sw
{
    /** @brief 필드 타입에서 컨테이너 트리(Vector/Map 중첩)를 만들고 사용자 별칭을 벗깁니다. */
    struct ContainerTreeBuilder
    {
        /**
         * @brief 타입을 사용자 별칭을 벗긴 표기로 바꿉니다. 템플릿 인자 안의 별칭도 끝까지 벗깁니다(`vector<AccountID>` 는 `vector<uint64>`).
         * @details 원소 이름만 벗기고 컨테이너 이름을 적힌 대로 두면 프로퍼티의 기록 타입 이름(`vector<AccountID>`)이 별칭을 쓰지 않던 빌드(`vector<uint64>`)와 달라
         *          그 빌드가 쓴 레코드를 읽지 못합니다. 그래서 프로퍼티 이름, 원소 이름, 키 이름이 모두 이 함수를 거칩니다.
         *          인자 중 하나라도 타입이 아니면(`std::array<int32, 4>` 의 4) 인자를 다시 짜지 않습니다.
         * @return 별칭을 하나라도 벗겼으면 true 입니다. 거짓이면 @p outSpelling 은 clang 의 표기 그대로입니다.
         */
        [[nodiscard]] static bool tryResolveAliasSpelling( CXType type, const ParserSession& session, string& outSpelling );

        /**
         * @brief 프로퍼티 타입이 사용자 별칭(`using` · `typedef`)이면 알려진 이름이 나올 때까지 벗겨 그 타입을 돌려줍니다.
         * @details 적힌 이름만 보면 `using ScoreList = sw::vector<int32>;` · `using Health = int32;` 로 적은 프로퍼티가 모르는 타입 이름
         *          (`ScoreList` · `Health`)으로 남는다 — 컨테이너로도 스칼라로도 읽히지 않아 직렬화가 그 값을 쓰지 못한다(JSON 에는 `"null"`).
         *          한 겹씩 벗기다가 컨테이너 · 표에 있는 이름(`int32` · `string` · `float3` …)이 나오면 거기서 멈추고, 별칭이 아닌 타입(구조체 ·
         *          enum)에 닿으면 그것을 쓴다. 그래서 `int32` · `sw::string` 처럼 이미 알려진 별칭은 이름 그대로다.
         */
        static CXType resolvePropertyAlias( CXType type, const ParserSession& session );

        /**
         * @brief 필드 타입의 컨테이너 트리를 프로퍼티에 복사합니다.
         * @return 컨테이너인데 인자를 clang 형식으로 읽지 못했으면(인자 수 부족 · 형식이 아닌 인자) 알리고 false 입니다 — 철자로 이어 가지 않습니다.
         */
        [[nodiscard]] static bool fillContainerDetails( ParsedPropertyInfo& prop, CXType fieldType, const ParserSession& session );
    };
} // namespace sw
