#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct WesternCatalogInternal
        {
            /** @brief `_level` 이 낮은 쪽이 앞인지 반환합니다(추격 · 유대 · 데드아이 단계표 정렬용). */
            template <typename TDef>
            [[nodiscard]] static bool isLowerLevel( const TDef& lhs, const TDef& rhs )
            {
                return lhs._level < rhs._level;
            }

            /** @brief `<PeltGrade scales="0,0.3,0.6,1">` — 0..3 성 네 값이 모두 있어야 합니다. 모자라면 오류이고 표를 바꾸지 않습니다. */
            [[nodiscard]] static bool readGradeScales( const XMLNode& node, string_view sourceName, vector<float32>& outListScale )
            {
                float4 scales{};
                if ( XMLReadUtil::parseFloat4( node.getAttributeText( "scales" ), scales ) == false )
                {
                    SW_LOG_ERROR( "%#: <PeltGrade scales> needs exactly four numbers (0..3 stars)", sourceName );
                    return false;
                }
                outListScale.assign( { scales._x, scales._y, scales._z, scales._w } );
                return true;
            }

            /** @brief `sizes="Small,Medium"` 를 크기 비트로 읽습니다. 적지 않으면 모든 크기(스키마 초기값)이고, 모르는 이름은 오류입니다. */
            [[nodiscard]] static bool readSizeMask( const XMLNode& node, string_view sourceName, uint8& inoutMask )
            {
                const string_view sizes = node.getAttributeText( "sizes" );
                if ( sizes.empty() )
                    return true;
                uint8 mask   = 0;
                bool  bValid = true;
                StringUtil::forEachToken( sizes, ",; ", [&]( string_view token )
                {
                    if ( StringUtil::equals( token, string_view( "Small" ) ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Small );
                    else if ( StringUtil::equals( token, string_view( "Medium" ) ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Medium );
                    else if ( StringUtil::equals( token, string_view( "Large" ) ) )
                        mask |= 1u << static_cast<uint32>( WesternAnimalSize::Large );
                    else
                    {
                        SW_LOG_ERROR( "%#: <HuntWeapon sizes> has an unknown size '%#' (Small, Medium, Large)", sourceName, token );
                        bValid = false;
                    }
                } );
                inoutMask = mask;
                return bValid;
            }
        };
    } // namespace

    SW_LOG_CALLER( "WesternCatalog" );

    WesternCatalog::WesternCatalog()
        : _crimeCatalog{}
        , _regionCatalog{}
        , _horseCatalog{}
        , _foodCatalog{}
        , _clothingCatalog{}
        , _animalCatalog{}
        , _huntWeaponCatalog{}
        , _hitZoneCatalog{}
        , _honorActionCatalog{}
        , _listHonorTier{}
        , _listPursuit{}
        , _listBondLevel{}
        , _listDeadEyeLevel{}
        , _listGradeScale{}
        , _honorReputation{}
        , _bondExperience{}
        , _bondCurve{}
        , _survival{}
        , _currency{ "Dollar" }
        , _extraHitPenalty{ 1 }
    {
        _listGradeScale = { 0.0f, 0.3f, 0.6f, 1.0f };
    }

    const WesternHonorTierDef* WesternCatalog::findHonorTier( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&WesternHonorTierDef::_name>( _listHonorTier, name );
    }

    const WesternPursuitDef* WesternCatalog::findPursuit( int32 wantedLevel ) const
    {
        const WesternPursuitDef* pFound = nullptr;
        for ( const WesternPursuitDef& pursuit : _listPursuit )
        {
            if ( pursuit._level <= wantedLevel )
                pFound = &pursuit;
        }
        return wantedLevel > 0 ? pFound : nullptr;
    }

    const WesternDeadEyeLevelDef* WesternCatalog::findDeadEyeLevel( int32 level ) const
    {
        const WesternDeadEyeLevelDef* pFound = nullptr;
        for ( const WesternDeadEyeLevelDef& deadEye : _listDeadEyeLevel )
        {
            if ( deadEye._level <= level )
                pFound = &deadEye;
        }
        return pFound;
    }

    float32 WesternCatalog::getGradeScale( int32 stars ) const
    {
        if ( stars <= 0 || _listGradeScale.empty() )
            return 0.0f;
        const size_t index = MathUtil::min( static_cast<size_t>( stars ), _listGradeScale.size() - 1 );
        return _listGradeScale[index];
    }

    bool WesternCatalog::loadElement( const XMLNode& node, string_view name, string_view sourceName )
    {
        using Internal = WesternCatalogInternal;
        bool bAdd      = false;
        if ( XMLReadUtil::isElement( name, "Pursuit" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _listPursuit );
        else if ( XMLReadUtil::isElement( name, "Honor" ) )
            bAdd = loadHonor( node, sourceName );
        else if ( XMLReadUtil::isElement( name, "Bond" ) )
        {
            WesternBondLevelDef bond;
            bAdd = GameDataXML::readFields( node, bond, sourceName, { "unlocks" } );
            StringUtil::forEachToken( node.getAttributeText( "unlocks" ), ",; ", [&bond]( string_view token )
            { bond._listUnlock.push_back( hashed_string( token ) ); } );
            if ( bAdd )
                _listBondLevel.push_back( bond );
        }
        else if ( XMLReadUtil::isElement( name, "BondExperience" ) )
            bAdd = GameDataXML::readSetting( node, sourceName, _bondExperience );
        else if ( XMLReadUtil::isElement( name, "Survival" ) )
            bAdd = GameDataXML::readSetting( node, sourceName, _survival );
        else if ( XMLReadUtil::isElement( name, "DeadEye" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _listDeadEyeLevel );
        else if ( XMLReadUtil::isElement( name, "PeltGrade" ) )
            bAdd = Internal::readGradeScales( node, sourceName, _listGradeScale );
        else if ( XMLReadUtil::isElement( name, "Crime" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _crimeCatalog );
        else if ( XMLReadUtil::isElement( name, "Region" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _regionCatalog );
        else if ( XMLReadUtil::isElement( name, "Horse" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _horseCatalog );
        else if ( XMLReadUtil::isElement( name, "Food" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _foodCatalog );
        else if ( XMLReadUtil::isElement( name, "Clothing" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _clothingCatalog );
        else if ( XMLReadUtil::isElement( name, "Animal" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _animalCatalog );
        else if ( XMLReadUtil::isElement( name, "HitZone" ) )
            bAdd = GameDataXML::readInto( node, sourceName, _hitZoneCatalog );
        else if ( XMLReadUtil::isElement( name, "HuntWeapon" ) )
        {
            WesternHuntWeaponDef weapon;
            bAdd = GameDataXML::readFields( node, weapon, sourceName, { "sizes" } ) && Internal::readSizeMask( node, sourceName, weapon._sizeMask );
            if ( bAdd )
                (void)_huntWeaponCatalog.add( weapon );
        }
        else
            SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
        return bAdd;
    }

    uint32 WesternCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        WesternCatalogRootDef rootDef;
        if ( GameDataXML::readFields( root, rootDef, sourceName ) )
        {
            _currency        = rootDef._currency;
            _extraHitPenalty = rootDef._extraHitPenalty;
        }
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( loadElement( node, name, sourceName ) )
                ++loadedCount;
        }

        std::stable_sort( _listPursuit.begin(), _listPursuit.end(), WesternCatalogInternal::isLowerLevel<WesternPursuitDef> );
        std::stable_sort( _listBondLevel.begin(), _listBondLevel.end(), WesternCatalogInternal::isLowerLevel<WesternBondLevelDef> );
        std::stable_sort( _listDeadEyeLevel.begin(), _listDeadEyeLevel.end(), WesternCatalogInternal::isLowerLevel<WesternDeadEyeLevelDef> );
        // 유대 단계는 기반 레벨 진행으로 센다. 단계 사이의 임계값 차이가 경험치 테이블이 된다(첫 단계가 시작점이다).
        vector<int64> listXpToNext;
        for ( size_t bondIndex = 1; bondIndex < _listBondLevel.size(); ++bondIndex )
        {
            const float32 gap = _listBondLevel[bondIndex]._experience - _listBondLevel[bondIndex - 1]._experience;
            listXpToNext.push_back( MathUtil::max( int64{ 1 }, static_cast<int64>( MathUtil::round( gap ) ) ) );
        }
        _bondCurve.setTable( listXpToNext );
        return loadedCount;
    }

    bool WesternCatalog::loadHonor( const XMLNode& node, string_view sourceName )
    {
        WesternHonorRangeDef range;
        bool                 bValid = GameDataXML::readFields( node, range, sourceName );
        FactionDef           faction;
        faction._id         = hashed_string( kHonorFactionID );
        faction._name       = "Honor";
        faction._minValue   = range._min;
        faction._maxValue   = range._max;
        faction._startValue = range._start;
        _listHonorTier.clear();
        for ( XMLNode child = node.findChild( "Tier" ); child; child = child.findNextSibling( "Tier" ) )
        {
            // 한 원소가 두 스키마(경계와 효과)로 읽힌다 — 서로의 칸은 손으로 읽는 칸으로 넘긴다.
            WesternHonorTierBoundDef bound;
            WesternHonorTierDef      effect;
            const bool               bBound  = GameDataXML::readFields( child, bound, sourceName, { "name", "flag", "discount" } );
            const bool               bEffect = GameDataXML::readFields( child, effect, sourceName, { "min" } );
            if ( bBound == false || bEffect == false )
            {
                bValid = false;
                continue;
            }
            ReputationTier tier;
            tier._name     = effect._name;
            tier._minValue = bound._min;
            faction._listTier.push_back( tier );
            _listHonorTier.push_back( effect );
        }
        std::stable_sort( faction._listTier.begin(), faction._listTier.end(), []( const ReputationTier& lhs, const ReputationTier& rhs )
        { return lhs._minValue < rhs._minValue; } );
        for ( XMLNode child = node.findChild( "Action" ); child; child = child.findNextSibling( "Action" ) )
        {
            WesternHonorActionDef action;
            if ( GameDataXML::readFields( child, action, sourceName ) )
                (void)_honorActionCatalog.add( action );
            else
                bValid = false;
        }
        _honorReputation.add( faction );
        return bValid;
    }
} // namespace sw
