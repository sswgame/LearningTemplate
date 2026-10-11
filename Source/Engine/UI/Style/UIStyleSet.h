/**
 * @file UIStyleSet.h
 * @brief 화면 하나에 적용된 스타일 시트 목록(테마, 문서 순서)과 계산된 스타일 캐시를 정의합니다.
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
    struct UIStyleRule;
    struct UIStyleSheetAsset;

    class Widget;

    /**
     * @class UIStyleSet
     * @brief 규칙을 특정도, 시트 순서, 시트 안 순서로 늘어놓고, 위젯마다 맞는 규칙을 적용해 계산된 스타일을 만듭니다.
     * @details **공유**: 계산된 스타일은 f(맞은 규칙 목록, 부모의 계산된 스타일)이므로, 그 둘이 같은 위젯은 한 객체를 쓴다(같은 버튼 백 개는 하나를 쓴다).
     *          **상속**: 텍스트 필드(`UIStyleFieldTable::kInherited`)는 부모가 정한 값을 먼저 물려받고 규칙이 덮어쓴다. 나머지는 규칙이 정한 필드만 정한 것이다.
     */
    class SW_API UIStyleSet
    {
    public:
        UIStyleSet();
        ~UIStyleSet();
        UIStyleSet( const UIStyleSet& )            = delete;
        UIStyleSet& operator=( const UIStyleSet& ) = delete;

        /** @brief 시트들을 등록합니다(앞의 것이 먼저이며, 같은 특정도에서는 뒤 시트가 이깁니다). 계산된 스타일 캐시를 비웁니다. */
        void                                               setSheets( vector<shared_ptr<const UIStyleSheetAsset>> listSheet );
        const vector<shared_ptr<const UIStyleSheetAsset>>& getSheets() const { return _listSheet; }

        /**
         * @brief 위젯 @p widget 의 계산된 스타일을 구합니다(지금 상태, 클래스, 조상으로 규칙을 맞춥니다).
         * @param pParentStyle 부모의 계산된 스타일입니다(루트면 nullptr). 텍스트 필드를 물려받습니다.
         * @param bNavigationMode 입력 방식이 탐색이면 포커스 위젯이 `:focus-visible` 입니다.
         * @param outAncestorKey 이 위젯이 맞는 "조상 쪽 선택자 조각" 의 해시입니다. 바뀌면 자손의 맞춤이 바뀔 수 있습니다.
         */
        shared_ptr<const UIComputedStyle> computeStyle( const Widget& widget, const UIComputedStyle* pParentStyle, bool bNavigationMode, uint64& outAncestorKey );
        /** @brief 지금 캐시에 있는 계산된 스타일 수입니다(공유 테스트용). */
        uint32 getComputedStyleCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        struct RuleRef
        {
            const UIStyleRule* _pRule;
            uint32             _order; ///< 시트 순서와 시트 안 순서를 합친 번호입니다.
        };

        struct Entry
        {
            vector<uint32>                    _listRuleIndex;
            const UIComputedStyle*            _pParent;
            shared_ptr<const UIComputedStyle> _style;
        };

    private:
        vector<shared_ptr<const UIStyleSheetAsset>> _listSheet;
        vector<RuleRef>                             _listRule; ///< 적용 순서입니다(특정도 오름차순, 순서 오름차순).
        vector<Entry>                               _listEntry;
        unordered_map<uint64, vector<uint32>>       _mapHashToEntry;
        vector<uint32>                              _listMatchScratch;
    };
} // namespace sw
