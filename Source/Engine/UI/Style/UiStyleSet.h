/**
 * @file UiStyleSet.h
 * @brief 화면 하나에 걸린 스타일 시트 묶음(테마 → 문서 순서)과 계산된 스타일 캐시입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    struct UiStyleRule;
    struct UiStyleSheetAsset;

    class Widget;

    /**
     * @class UiStyleSet
     * @brief 규칙을 특정도 → 시트 순서 → 시트 안 순서로 늘어놓고, 위젯마다 맞는 규칙을 적용해 계산된 스타일을 만듭니다.
     * @details **나눠 쓰기**: 계산된 스타일 = f(맞은 규칙 목록, 부모의 계산된 스타일) 이라 그 둘이 같은 위젯은 한 객체를 씁니다(같은 버튼 백 개 → 하나).
     *          **상속**: 글 칸(`UiStyleFieldTable::kInherited`)은 부모가 정한 값을 먼저 물려받고 규칙이 덮습니다. 나머지는 규칙이 정한 칸만 정한 것입니다.
     */
    class SW_API UiStyleSet
    {
    public:
        UiStyleSet();
        ~UiStyleSet();
        UiStyleSet( const UiStyleSet& )            = delete;
        UiStyleSet& operator=( const UiStyleSet& ) = delete;

        /** @brief 시트들을 겁니다(앞이 먼저 — 뒤 시트가 같은 특정도에서 이긴다). 계산된 스타일 캐시를 비웁니다. */
        void                                               setSheets( vector<shared_ptr<const UiStyleSheetAsset>> listSheet );
        const vector<shared_ptr<const UiStyleSheetAsset>>& getSheets() const { return _listSheet; }

        /**
         * @brief 위젯 @p widget 의 계산된 스타일입니다(지금 상태 · 클래스 · 조상으로 규칙을 맞춘다).
         * @param pParentStyle 부모의 계산된 스타일(루트면 nullptr) — 글 칸을 물려받는다.
         * @param bNavigationMode 입력 방식이 탐색이면 포커스 위젯이 `:focus-visible` 이다.
         * @param outAncestorKey 이 위젯이 맞는 "조상 쪽 선택자 조각" 의 해시 — 바뀌면 자손의 맞춤이 바뀔 수 있다.
         */
        shared_ptr<const UiComputedStyle> computeStyle( const Widget& widget, const UiComputedStyle* pParentStyle, bool bNavigationMode, uint64& outAncestorKey );
        /** @brief 지금 캐시에 든 계산된 스타일 수입니다(나눠 쓰기 시험). */
        uint32 getComputedStyleCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        struct RuleRef
        {
            const UiStyleRule* _pRule;
            uint32             _order; ///< 시트 순서 · 시트 안 순서를 이은 번호
        };

        struct Entry
        {
            vector<uint32>                    _listRuleIndex;
            const UiComputedStyle*            _pParent;
            shared_ptr<const UiComputedStyle> _style;
        };

    private:
        vector<shared_ptr<const UiStyleSheetAsset>> _listSheet;
        vector<RuleRef>                             _listRule; ///< 적용 순서(특정도 오름 → 순서 오름)
        vector<Entry>                               _listEntry;
        unordered_map<uint64, vector<uint32>>       _mapHashToEntry;
        vector<uint32>                              _listMatchScratch;
    };
} // namespace sw
