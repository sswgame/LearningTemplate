/**
 * @file CatalogChildSection.h
 * @brief 카탈로그 항목의 자식 원소(`<Part>` 등)를 규칙대로 펼친 보기입니다 — 무엇을 적었고 무엇을 물려받았는지 값으로 답합니다(ImGui 없음).
 * @details 줄의 순서와 값은 엔진이 합친 결과(`DataCatalogInheritance`)를 그대로 읽고 여기서 합치기를 다시 적지 않는다. 고치는 일은 `CatalogDocument` 가 원문의 적은 칸만 바꾼다.
 *          언리얼 Details 패널의 배열 칸(물려받은 원소를 보이고 다른 칸만 저장) · 유니티 Prefab Variant 의 Overrides 와 같은 일이다. 다만 원소를 번호가 아니라 열쇠로 짚어 부모가 끼워 넣어도 어긋나지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 항목 아래의 원소를 짚는 경로 한 칸입니다. 열쇠가 있으면 열쇠로, 없으면 같은 이름의 형제 가운데 번호로 짚습니다. 고치면 원소 번호가 바뀌므로 번호 대신 이 값을 들고 있는다. */
    struct CatalogChildStep
    {
        string _name{};
        string _keyAttribute{}; ///< 비면 열쇠 없는 칸이다
        string _key{};
        int32  _index{ 0 }; ///< 열쇠 없는 칸의 같은 이름 형제 번호
    };

    using CatalogChildPath = vector<CatalogChildStep>;
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표의 칸 하나입니다. */
    struct CatalogChildCell
    {
        string _column{};
        string _value{};             ///< 적었으면 적은 값, 아니면 물려받은 값
        bool   _bWritten{ false };   ///< 이 항목이 적었다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool   _bInherited{ false }; ///< 적지 않았고 부모에 있다. NOLINT(sw-bool-flag): 집합체다
        bool   _bKey{ false };       ///< 열쇠 칸이다 — 물려받은 원소의 열쇠는 고칠 수 없다. NOLINT(sw-bool-flag): 집합체다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표의 줄 하나, 곧 자식 원소 하나입니다. */
    struct CatalogChildNode
    {
        CatalogChildPath         _path{};
        vector<CatalogChildCell> _listCell{};          ///< 구역의 `_listColumn` 순서
        bool                     _bInherited{ false }; ///< 부모에 같은 원소가 있다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                     _bRemoved{ false };   ///< `_op="Remove"` 로 뺐다. 합친 결과에는 없고 되돌릴 수 있게 보인다. NOLINT(sw-bool-flag): 집합체다
        bool                     _bMoved{ false };     ///< `_before` · `_after` 를 적었다. NOLINT(sw-bool-flag): 집합체다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 구역이 원소를 합치는 규칙입니다(`Content/engine/settings/datacataloginheritance.xml`). */
    enum class CatalogChildSectionKind : uint8
    {
        List,  ///< 규칙의 `key` 있는 칸: 원소마다 합친다. 더하기 · 지우기 · 순서
        Field, ///< 규칙의 `key` 없는 칸: 구조체 하나다
        Whole, ///< 규칙에 없는 칸: 적으면 그 이름 원소 전체가 바뀐다. 비우기(`_op="Clear"`)
    };

    /** @brief 같은 이름의 자식 원소를 묶은 표 하나입니다. 중첩 칸은 부모 구역 뒤에 자기 구역으로 나옵니다. */
    struct CatalogChildSection
    {
        string                   _title{}; ///< `Part Shell / Variant`
        string                   _name{};  ///< 원소 이름
        string                   _keyAttribute{};
        CatalogChildPath         _ownerPath{}; ///< 이 원소들을 담는 원소. 비면 항목 자신이다
        vector<string>           _listColumn{};
        vector<CatalogChildNode> _listNode{};
        CatalogChildSectionKind  _kind{ CatalogChildSectionKind::List };
        bool                     _bHasParent{ false };  ///< 담는 원소에 부모가 있다 — 순서 · 비우기는 물려받을 때만 뜻이 있다. NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool                     _bCleared{ false };    ///< `Whole`: `_op="Clear"` 로 비웠다. NOLINT(sw-bool-flag): 집합체다
        bool                     _bOverridden{ false }; ///< `Whole`: 이 항목이 원소를 적어 부모 것을 바꿨다. NOLINT(sw-bool-flag): 집합체다
    };
} // namespace sw::editor
