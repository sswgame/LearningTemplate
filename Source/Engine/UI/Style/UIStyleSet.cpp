#include "pch.h"

#include "Engine/UI/Style/UIStyleSet.h"

#include "Core/Common/HashUtil.h"

#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/WidgetTree/Widget.h"

namespace sw
{
    namespace
    {
        struct UIStyleSetInternal
        {
            /** @brief 적용 순서입니다. 특정도가 낮은 것이 먼저이고(뒤가 이깁니다), 같으면 앞 시트와 앞 규칙이 먼저입니다. */
            static bool isAppliedBefore( const UIStyleRule* pLhs, uint32 lhsOrder, const UIStyleRule* pRhs, uint32 rhsOrder )
            {
                if ( pLhs->_selector._specificity != pRhs->_selector._specificity )
                    return pLhs->_selector._specificity < pRhs->_selector._specificity;
                return lhsOrder < rhsOrder;
            }

            static uint64 hashEntry( const vector<uint32>& listRuleIndex, const UIComputedStyle* pParent )
            {
                uint64 hash = HashUtil::mix64( reinterpret_cast<uintptr_t>( pParent ) );
                for ( const uint32 ruleIndex : listRuleIndex )
                {
                    hash = HashUtil::combine( hash, ruleIndex );
                }
                return hash;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIStyleSet::UIStyleSet()
        : _listSheet{}
        , _listRule{}
        , _listEntry{}
        , _mapHashToEntry{}
        , _listMatchScratch{}
    {
    }

    UIStyleSet::~UIStyleSet() = default;

    void UIStyleSet::setSheets( vector<shared_ptr<const UIStyleSheetAsset>> listSheet )
    {
        _listSheet = std::move( listSheet );
        _listRule.clear();
        _listEntry.clear();
        _mapHashToEntry.clear();
        uint32 order = 0;
        for ( const shared_ptr<const UIStyleSheetAsset>& sheet : _listSheet )
        {
            if ( sheet == nullptr )
                continue;
            for ( const UIStyleRule& rule : sheet->_listRule )
            {
                _listRule.push_back( RuleRef{ &rule, order++ } );
            }
        }
        // 삽입 정렬이다. 규칙은 수십 개이고 시트를 적용할 때 한 번만 한다(같은 특정도의 순서를 지킨다).
        for ( size_t index = 1; index < _listRule.size(); ++index )
        {
            const RuleRef current = _listRule[index];
            size_t        at      = index;
            while ( at > 0 && UIStyleSetInternal::isAppliedBefore( current._pRule, current._order, _listRule[at - 1]._pRule, _listRule[at - 1]._order ) )
            {
                _listRule[at] = _listRule[at - 1];
                --at;
            }
            _listRule[at] = current;
        }
    }

    shared_ptr<const UIComputedStyle> UIStyleSet::computeStyle( const Widget& widget, const UIComputedStyle* pParentStyle, bool bNavigationMode, uint64& outAncestorKey )
    {
        // 맞는 규칙(적용 순서)과 조상 쪽 조각 열쇠.
        _listMatchScratch.clear();
        uint64 ancestorKey{ 0 };
        for ( uint32 ruleIndex = 0; ruleIndex < static_cast<uint32>( _listRule.size() ); ++ruleIndex )
        {
            const UIStyleSelector& selector = _listRule[ruleIndex]._pRule->_selector;
            if ( selector.matches( widget, bNavigationMode ) )
                _listMatchScratch.push_back( ruleIndex );
            for ( uint32 partIndex = 0; partIndex + 1 < static_cast<uint32>( selector._listPart.size() ); ++partIndex )
            {
                if ( UIStyleSelector::matchesPart( selector._listPart[partIndex], widget, bNavigationMode ) )
                    ancestorKey = HashUtil::combine( ancestorKey, ( static_cast<uint64>( ruleIndex ) << 32 ) | partIndex );
            }
        }
        outAncestorKey = ancestorKey;

        const uint64 hash = UIStyleSetInternal::hashEntry( _listMatchScratch, pParentStyle );
        auto         iter = _mapHashToEntry.find( hash );
        if ( iter != _mapHashToEntry.end() )
        {
            for ( const uint32 entryIndex : iter->second )
            {
                const Entry& entry = _listEntry[entryIndex];
                if ( entry._pParent == pParentStyle && entry._listRuleIndex == _listMatchScratch )
                    return entry._style;
            }
        }

        shared_ptr<UIComputedStyle> style = make_shared<UIComputedStyle>();
        if ( pParentStyle != nullptr )
        {
            // 텍스트 필드는 부모가 정한 값을 먼저 물려받는다(CSS 상속 속성).
            for ( uint32 index = 0; index < static_cast<uint32>( UIStyleField::Count ); ++index )
            {
                const UIStyleField field = static_cast<UIStyleField>( index );
                if ( ( UIStyleFieldTable::getFlags( field ) & UIStyleFieldTable::kInherited ) == 0 || pParentStyle->has( field ) == false )
                    continue;
                style->_setMask |= UIStyleFieldTable::makeBit( field );
                UIStyleFieldTable::copyField( pParentStyle->_value, field, style->_value ); // 어느 필드를 물려받는지는 표의 kInherited 하나가 정한다
            }
        }
        for ( const uint32 ruleIndex : _listMatchScratch )
        {
            for ( const UIStyleAssignment& assignment : _listRule[ruleIndex]._pRule->_listAssignment )
            {
                UIStyleSheetLoader::applyAssignment( assignment, style->_value );
                style->_setMask |= UIStyleFieldTable::makeBit( assignment._field );
            }
        }

        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        _listEntry.push_back( Entry{ _listMatchScratch, pParentStyle, style } );
        _mapHashToEntry[hash].push_back( entryIndex );
        return style;
    }
} // namespace sw
