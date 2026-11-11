#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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

            /** @brief `<PeltGrade scales="0,0.3,0.6,1">` — 0..3 성 네 값이 모두 있고 음수가 아니어야 합니다(음수면 가죽 값이 음수다). 아니면 오류이고 표를 바꾸지 않습니다. */
            [[nodiscard]] static bool readGradeScales( const XMLNode& node, string_view sourceName, vector<float32>& outListScale )
            {
                float4 scales{};
                if ( XMLReadUtil::parseFloat4( node.getAttributeText( "scales" ), scales ) == false )
                {
                    SW_LOG_ERROR_ID( "WesternCatalog.PeltGradeScalesNeedsExactly", "%#: <PeltGrade scales> needs exactly four numbers (0..3 stars)", sourceName );
                    return false;
                }
                if ( scales._x < 0.0f || scales._y < 0.0f || scales._z < 0.0f || scales._w < 0.0f )
                {
                    SW_LOG_ERROR_ID( "WesternCatalog.PeltGradeScalesNegative", "%#: <PeltGrade scales> must not be negative", sourceName );
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
                        SW_LOG_ERROR_ID( "WesternCatalog.HuntWeaponSizesUnknownSize", "%#: <HuntWeapon sizes> has an unknown size '%#' (Small, Medium, Large)", sourceName, token );
                        bValid = false;
                    }
                } );
                inoutMask = mask;
                return bValid;
            }
        };
    } // namespace

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
        , _currency{} // 루트 `currency` 가 필수다(WesternCatalogRootDef)
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

    bool WesternCatalog::loadPeltGrade( const XMLNode& node, string_view sourceName )
    {
        return WesternCatalogInternal::readGradeScales( node, sourceName, _listGradeScale );
    }

    bool WesternCatalog::loadHuntWeapon( const XMLNode& node, string_view sourceName )
    {
        WesternHuntWeaponDef weapon;
        if ( XMLFields::readProperties( node, weapon, sourceName, { "sizes" } ) == false || WesternCatalogInternal::readSizeMask( node, sourceName, weapon._sizeMask ) == false )
            return false;
        (void)_huntWeaponCatalog.add( weapon );
        return true;
    }

    uint32 WesternCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        WesternCatalogRootDef rootDef;
        if ( XMLFields::readProperties( root, rootDef, sourceName ) )
        {
            _currency        = rootDef._currency;
            _extraHitPenalty = rootDef._extraHitPenalty;
        }
        using Element                          = XMLCatalogElement<WesternCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&WesternCatalog::_listPursuit>( "Pursuit" ),
            Element::of<&WesternCatalog::loadHonor>( "Honor" ),
            Element::of<&WesternCatalog::_listBondLevel>( "Bond" ),
            Element::of<&WesternCatalog::_bondExperience>( "BondExperience" ),
            Element::of<&WesternCatalog::_survival>( "Survival" ),
            Element::of<&WesternCatalog::_listDeadEyeLevel>( "DeadEye" ),
            Element::of<&WesternCatalog::loadPeltGrade>( "PeltGrade" ),
            Element::of<&WesternCatalog::_crimeCatalog>( "Crime" ),
            Element::of<&WesternCatalog::_regionCatalog>( "Region" ),
            Element::of<&WesternCatalog::_horseCatalog>( "Horse" ),
            Element::of<&WesternCatalog::_foodCatalog>( "Food" ),
            Element::of<&WesternCatalog::_clothingCatalog>( "Clothing" ),
            Element::of<&WesternCatalog::_animalCatalog>( "Animal" ),
            Element::of<&WesternCatalog::_hitZoneCatalog>( "HitZone" ),
            Element::of<&WesternCatalog::loadHuntWeapon>( "HuntWeapon" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );

        VectorUtil::stableSort( _listPursuit, WesternCatalogInternal::isLowerLevel<WesternPursuitDef> );
        VectorUtil::stableSort( _listBondLevel, WesternCatalogInternal::isLowerLevel<WesternBondLevelDef> );
        VectorUtil::stableSort( _listDeadEyeLevel, WesternCatalogInternal::isLowerLevel<WesternDeadEyeLevelDef> );
        // 유대 단계는 기반 레벨 진행으로 센다. 단계 사이의 임계값 차이가 경험치 테이블이 된다(첫 단계가 시작점이다).
        vector<int64> listXPToNext;
        for ( size_t bondIndex = 1; bondIndex < _listBondLevel.size(); ++bondIndex )
        {
            const float32 gap = _listBondLevel[bondIndex]._experience - _listBondLevel[bondIndex - 1]._experience;
            if ( gap <= 0.0f ) // 표는 단계 수를 지켜야 해서 칸을 빼지 않는다 — 1 로 두고 오류를 남긴다
                SW_LOG_ERROR_ID( "WesternCatalog.BondExperienceNotRising", "%#: <Bond level=\"%#\"> xp must be above the previous level", sourceName, _listBondLevel[bondIndex]._level );
            listXPToNext.push_back( MathUtil::max( int64{ 1 }, static_cast<int64>( MathUtil::round( gap ) ) ) );
        }
        _bondCurve.setTable( listXPToNext );
        return result._loadedCount;
    }

    bool WesternCatalog::loadHonor( const XMLNode& node, string_view sourceName )
    {
        WesternHonorRangeDef range;
        if ( XMLFields::readProperties( node, range, sourceName ) == false )
            return false; // 깨진 범위로 세력을 만들면 값을 뒤집힌 범위로 자른다
        bool       bValid = true;
        FactionDef faction;
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
            const bool               bBound  = XMLFields::readProperties( child, bound, sourceName, { "name", "flag", "discount" } );
            const bool               bEffect = XMLFields::readProperties( child, effect, sourceName, { "min" } );
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
        VectorUtil::stableSort( faction._listTier, []( const ReputationTier& lhs, const ReputationTier& rhs )
        { return lhs._minValue < rhs._minValue; } );
        for ( XMLNode child = node.findChild( "Action" ); child; child = child.findNextSibling( "Action" ) )
        {
            WesternHonorActionDef action;
            if ( XMLFields::readProperties( child, action, sourceName ) )
                (void)_honorActionCatalog.add( action );
            else
                bValid = false;
        }
        _honorReputation.add( faction );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** @brief `WesternCatalog` 의 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 어떤 파일을 어떻게 읽는지 압니다). */
    SW_MODULE_SHARED_ASSET_CACHE( Western, WesternCatalog, SW_KIT_API, WesternCatalog::kAssetType.withLoader( AssetTypeLoader::of<WesternCatalog>( MemoryTag::Game ) ) );
} // namespace sw
