/**
 * @file InputMapXML.h
 * @brief 입력 맵 정의(`<InputMap>`)와 사용자 바인딩(`<UserBindings>`)이 함께 쓰는 `<action>` 원소 하나의 읽기 · 쓰기입니다.
 * @details 두 파일은 수명이 다르지만(디자인 데이터 · 플레이어 파일) 바인딩 원소는 같다 — 원소마다의 칸은 `InputMapFields.h` 스키마이고 여기 하나가 읽고 쓴다.
 *          예전에는 두 형식이 같은 바인딩을 다른 속성 이름(`negative` · `negKey`, `code` · `key` · `button`, `responseExponent` · `exponent`)으로 따로 읽었다.
 *          언리얼 Enhanced Input 이 매핑 컨텍스트와 플레이어 키 프로필에 같은 `FEnhancedActionKeyMapping` 을 쓰는 것과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class InputMap;
    class XMLNode;

    /**
     * @struct InputMapXML
     * @brief `<action>` 원소 하나를 맵에 읽고, 맵의 액션 하나를 원소로 씁니다.
     */
    struct InputMapXML
    {
        /**
         * @brief 원소 @p actionNode 의 액션과 바인딩을 @p map 에 더합니다. 바인딩이 `layer` 를 적지 않으면 액션의 것, 그것도 없으면 @p inheritedLayer 입니다.
         * @details 모르는 원소 · 속성, 빠진 필수, 모르는 키 이름은 오류이고 그 바인딩은 넣지 않는다(@p inoutbValid 를 내린다 — 조용히 버리지 않는다).
         */
        static void readAction( InputMap& map, const XMLNode& actionNode, const hashed_string& inheritedLayer, string_view sourceName, bool& inoutbValid );
        /**
         * @brief 맵의 액션 @p action 을 @p parent 아래 `<action>` 하나로 씁니다. 바인딩마다 레이어와 트리거를 적어 액션 기본값에 기대지 않는다.
         * @return 형식이 담지 못하는 바인딩이 있으면 오류를 남기고 false 입니다(빼고 쓰면 다시 읽을 때 조용히 사라진다).
         */
        [[nodiscard]] static bool writeAction( const XMLNode& parent, const InputMap& map, const hashed_string& action );
    };
} // namespace sw
