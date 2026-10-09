#include "pch.h"

#include "Engine/UI/Style/UiStyleSet.h"

#include "Core/Common/HashUtil.h"

#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Style/UiStyleSheet.h"

namespace sw
{
    namespace
    {
        struct UiStyleSetInternal
        {
            /** @brief 적용 순서 — 특정도가 낮은 것이 먼저(뒤가 이긴다), 같으면 앞 시트 · 앞 규칙이 먼저. */
            static bool isAppliedBefore( const UiStyleRule* pLhs, uint32 lhsOrder, const UiStyleRule* pRhs, uint32 rhsOrder )
            {
                if ( pLhs->_selector._specificity != pRhs->_selector._specificity )
                    return pLhs->_selector._specificity < pRhs->_selector._specificity;
                return lhsOrder < rhsOrder;
            }

            static uint64 hashEntry( const vector<uint32>& listRuleIndex, const UiComputedStyle* pParent )
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
    UiStyleSet::UiStyleSet()
        : _listSheet{}
        , _listRule{}
        , _listEntry{}
        , _mapHashToEntry{}
        , _listMatchScratch{}
    {
    }

    UiStyleSet::~UiStyleSet() = default;

    void UiStyleSet::setSheets( vector<shared_ptr<const UiStyleSheetAsset>> listSheet )
    {
        _listSheet = std::move( listSheet );
        _listRule.clear();
        _listEntry.clear();
        _mapHashToEntry.clear();
        uint32 order = 0;
        for ( const shared_ptr<const UiStyleSheetAsset>& sheet : _listSheet )
        {
            if ( sheet == nullptr )
                continue;
            for ( const UiStyleRule& rule : sheet->_listRule )
            {
                _listRule.push_back( RuleRef{ &rule, order++ } );
            }
        }
        // 삽입 정렬 — 규칙 수십 개, 시트를 걸 때 한 번이다(같은 특정도의 순서를 지킨다).
        for ( size_t index = 1; index < _listRule.size(); ++index )
        {
            const RuleRef current = _listRule[index];
            size_t        at      = index;
            while ( at > 0 && UiStyleSetInternal::isAppliedBefore( current._pRule, current._order, _listRule[at - 1]._pRule, _listRule[at - 1]._order ) )
            {
                _listRule[at] = _listRule[at - 1];
                --at;
            }
            _listRule[at] = current;
        }
    }

    shared_ptr<const UiComputedStyle> UiStyleSet::computeStyle( const Widget& widget, const UiComputedStyle* pParentStyle, bool bNavigationMode, uint64& outAncestorKey )
    {
        // 맞는 규칙(적용 순서)과 조상 쪽 조각 열쇠.
        _listMatchScratch.clear();
        uint64 ancestorKey{ 0 };
        for ( uint32 ruleIndex = 0; ruleIndex < static_cast<uint32>( _listRule.size() ); ++ruleIndex )
        {
            const UiStyleSelector& selector = _listRule[ruleIndex]._pRule->_selector;
            if ( selector.matches( widget, bNavigationMode ) )
                _listMatchScratch.push_back( ruleIndex );
            for ( uint32 partIndex = 0; partIndex + 1 < static_cast<uint32>( selector._listPart.size() ); ++partIndex )
            {
                if ( UiStyleSelector::matchesPart( selector._listPart[partIndex], widget, bNavigationMode ) )
                    ancestorKey = HashUtil::combine( ancestorKey, ( static_cast<uint64>( ruleIndex ) << 32 ) | partIndex );
            }
        }
        outAncestorKey = ancestorKey;

        const uint64 hash = UiStyleSetInternal::hashEntry( _listMatchScratch, pParentStyle );
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

        shared_ptr<UiComputedStyle> style = make_shared<UiComputedStyle>();
        if ( pParentStyle != nullptr )
        {
            // 글 칸은 부모가 정한 값을 먼저 물려받는다(CSS 상속 속성).
            for ( uint32 index = 0; index < static_cast<uint32>( UiStyleField::Count ); ++index )
            {
                const UiStyleField field = static_cast<UiStyleField>( index );
                if ( ( UiStyleFieldTable::getEntry( field )._flags & UiStyleFieldTable::kInherited ) == 0 || pParentStyle->has( field ) == false )
                    continue;
                style->_setMask |= UiStyleFieldTable::makeBit( field );
                switch ( field )
                {
                    case UiStyleField::Font:
                    {
                        style->_value._font = pParentStyle->_value._font;
                        break;
                    }
                    case UiStyleField::FontSize:
                    {
                        style->_value._fontSize = pParentStyle->_value._fontSize;
                        break;
                    }
                    case UiStyleField::TextColor:
                    {
                        style->_value._textColor = pParentStyle->_value._textColor;
                        break;
                    }
                    case UiStyleField::TextOutlineColor:
                    {
                        style->_value._textOutlineColor = pParentStyle->_value._textOutlineColor;
                        break;
                    }
                    case UiStyleField::TextOutlineWidth:
                    {
                        style->_value._textOutlineWidth = pParentStyle->_value._textOutlineWidth;
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
        for ( const uint32 ruleIndex : _listMatchScratch )
        {
            for ( const UiStyleAssignment& assignment : _listRule[ruleIndex]._pRule->_listAssignment )
            {
                UiStyleSheetLoader::applyAssignment( assignment, style->_value );
                style->_setMask |= UiStyleFieldTable::makeBit( assignment._field );
            }
        }

        const uint32 entryIndex = static_cast<uint32>( _listEntry.size() );
        _listEntry.push_back( Entry{ _listMatchScratch, pParentStyle, style } );
        _mapHashToEntry[hash].push_back( entryIndex );
        return style;
    }
} // namespace sw
