/**
 * @file RigFileRewrite.h
 * @brief 리그 파일(`*.rig.xml`)의 노드 무게 · 켬과 대상 오프셋만 바꿔 되쓰는 편집기 도구입니다(리그 디버그 창의 "Save to rig" · 대상 오프셋 기즈모).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw::editor
{
    /** @brief 에디터가 리그 파일에 되써 넣는 노드 하나의 값입니다(`RigFileRewrite::rewriteSettings`). 노드는 이름으로 찾습니다(이름은 파일 안에서 하나다). */
    struct RigNodeSaveParams
    {
        hashed_string _name{};
        float32       _weight{ 1.0f };
        bool          _bEnabled{ true };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 에디터가 리그 파일에 되써 넣는 대상 하나의 오프셋입니다(`translation` · `rotation`, 대상의 본 · 소켓 공간). 대상은 이름으로 찾습니다. 스케일은 쓰지 않습니다. */
    struct RigTargetSaveParams
    {
        hashed_string _name{};
        float3        _translation{};
        quaternion    _rotation{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 리그 파일 되쓰기 한 번의 값입니다. 빈 목록은 그 종류를 건드리지 않습니다. */
    struct RigSaveParams
    {
        vector<RigNodeSaveParams>   _listNode{};
        vector<RigTargetSaveParams> _listTarget{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 리그 파일 되쓰기입니다. */
    struct RigFileRewrite
    {
        /**
         * @brief 리그 파일 글(@p xmlText)의 노드 `weight` · `enabled` 와 대상 `translation` · `rotation` 을 @p params 의 값으로 바꾼 글을 @p outXMLText 에 씁니다
         *        (리그 디버그 창의 "Save to rig" 와 대상 오프셋 기즈모).
         * @details 다른 속성 · 요소는 그대로 둔다 — 쓰는 쪽이 노드 · 대상 종류를 몰라도 된다. `enabled` 는 끈 노드이거나 파일에 이미 있을 때만, `translation` · `rotation` 은
         *          0 · 단위가 아니거나 파일에 이미 있을 때만 쓴다(적지 않음 = 기본). `rotation` 은 도 단위 [피치, 요, 롤]이다(읽기와 같은 배치).
         *          이름의 노드 · 대상이 없거나 결과가 리그 규칙에 맞지 않으면(다시 읽어 본다) 오류를 남기고 false 이며 @p outXMLText 는 그대로다.
         */
        [[nodiscard]] static bool rewriteSettings( string_view xmlText, string_view sourceLabel, const RigSaveParams& params, string& outXMLText );
    };
} // namespace sw::editor
