#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    ElementChart::ElementChart()
        : _listElement{}
        , _mapRule{}
        , _listStatusChance{}
    {
    }

    void ElementChart::clear()
    {
        _listElement.clear();
        _mapRule.clear();
        _listStatusChance.clear();
    }

    void ElementChart::addElement( const hashed_string& element )
    {
        if ( element.empty() || hasElement( element ) )
            return;
        _listElement.push_back( element );
    }

    bool ElementChart::hasElement( const hashed_string& element ) const
    {
        return VectorUtil::contains( _listElement, element );
    }

    bool ElementChart::setMultiplier( const hashed_string& attack, const hashed_string& defend, float32 multiplier )
    {
        if ( multiplier < 0.0f )
        {
            SW_LOG_ERROR_ID( "ElementChart.ElementChartRefusedMultiplierMust", "ElementChart: refused multiplier %# for %# -> %# - it must not be negative", multiplier, attack.c_str(), defend.c_str() );
            return false;
        }
        _mapRule[attack][defend] = multiplier;
        return true;
    }

    void ElementChart::addStatusChance( const ElementStatusChance& statusChance )
    {
        ElementStatusChance entry = statusChance;
        entry._chance             = MathUtil::saturate( entry._chance );
        _listStatusChance.push_back( entry );
    }

    float32 ElementChart::getMultiplier( const hashed_string& attack, const hashed_string& defend ) const
    {
        const auto attackIter = _mapRule.find( attack );
        if ( attackIter == _mapRule.end() )
            return 1.0f;
        const auto defendIter = attackIter->second.find( defend );
        return defendIter != attackIter->second.end() ? defendIter->second : 1.0f;
    }

    float32 ElementChart::computeMultiplier( const hashed_string& attack, const vector<hashed_string>& listDefend ) const
    {
        float32 multiplier = 1.0f;
        for ( const hashed_string& defend : listDefend )
        {
            multiplier *= getMultiplier( attack, defend );
        }
        return multiplier;
    }

    bool ElementChart::isImmune( const hashed_string& attack, const vector<hashed_string>& listDefend ) const
    {
        return computeMultiplier( attack, listDefend ) <= 0.0f;
    }

    hashed_string ElementChart::rollStatus( const hashed_string& element, GameRandom& random ) const
    {
        for ( const ElementStatusChance& entry : _listStatusChance )
        {
            if ( entry._element == element && random.nextChance( entry._chance ) )
                return entry._status;
        }
        return hashed_string{};
    }

    uint32 ElementChart::loadRoot( const XMLNode& root, string_view sourceName )
    {
        clear();
        // 규칙 · 상태는 선언한 속성만 가리키므로 속성을 먼저 다 읽는다.
        using Element                          = XMLCatalogElement<ElementChart>;
        static constexpr Element kArrElement[] = {
            Element::of<&ElementChart::loadElement>( "Element" ),
            Element::of<&ElementChart::loadRule>( "Rule" ).uncounted(),
            Element::of<&ElementChart::loadStatus>( "Status" ).uncounted(),
        };
        (void)XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement );
        return static_cast<uint32>( _listElement.size() );
    }

    bool ElementChart::loadElement( const XMLNode& node, string_view sourceName )
    {
        ElementDeclParams element;
        if ( XMLFields::readProperties( node, element, sourceName ) == false )
            return false;
        addElement( element._id );
        return true;
    }

    bool ElementChart::loadRule( const XMLNode& node, string_view sourceName )
    {

        ElementRuleParams rule; // 칸 · 배율 >= 0 은 PROPERTY 가 본다
        if ( XMLFields::readProperties( node, rule, sourceName ) == false )
            return false;
        if ( hasElement( rule._attack ) == false || hasElement( rule._defend ) == false )
        {
            SW_LOG_WARNING_ID( "ElementChart.RuleNamesUndeclaredElement", "%#: rule '%#' -> '%#' names an undeclared element - skipped", sourceName, rule._attack.c_str(), rule._defend.c_str() );
            return false;
        }
        (void)setMultiplier( rule._attack, rule._defend, rule._multiplier ); // 음수는 위에서 막았다
        return true;
    }

    bool ElementChart::loadStatus( const XMLNode& node, string_view sourceName )
    {

        ElementStatusChance entry; // 칸 · 확률 0..1 은 PROPERTY 가 본다
        const bool          bEntryValid = XMLFields::readProperties( node, entry, sourceName ) && hasElement( entry._element );
        if ( bEntryValid == false )
        {
            SW_LOG_WARNING_ID( "ElementChart.StatusInvalidSkipped", "%#: status '%#' on '%#' is invalid - skipped", sourceName, entry._status.c_str(), entry._element.c_str() );
            return false;
        }
        addStatusChance( entry );
        return true;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<ElementChart>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( ElementChart, ElementChart, SW_GF_API, ElementChart::kAssetType.withLoader( AssetTypeLoader::of<ElementChart>( MemoryTag::Game ) ) );
} // namespace sw
