#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Base/UI/Marker/HealthBarComponent.h"

// PropertyUnitsTest — PROPERTY 의 `Units` 메타가 값이 저장된 단위와 맞는지 등록된 모든 타입에서 본다(GameFramework 에 둬 HP 바 비율도 늘 본다).

namespace
{
    /** @brief 각도를 담는 이름인가(대소문자 무시) — 회전 · 각 · 시야각. */
    bool isAngleName( const sw::hashed_string& name )
    {
        for ( const utf8* pWord : { "angle", "rotation", "fov" } )
        {
            if ( sw::StringUtil::stristr( name.c_str(), pWord ) != nullptr )
                return true;
        }
        return false;
    }

    /** @brief `Units` 하나와, 툴팁에 있으면 다른 단위를 말하는 낱말입니다 — 툴팁과 `Units` 중 하나가 틀린 것입니다. */
    struct ConflictingUnitWord
    {
        const utf8* _pUnits;
        const utf8* _pWord;
    };

    // 타일 게임의 툴팁은 "타일" 을 말해도 거리는 m 다 — 단위로 쓰인 꼴("in tiles" · "tiles/" · "tiles per")만 본다.
    constexpr ConflictingUnitWord kArrConflictingUnitWord[] = {
        {  "m",    "in tiles"},
        {  "m",       "pixel"},
        {"m/s",      "tiles/"},
        {"m/s",   "tiles per"},
        {"m/s",       "pixel"},
        {"m/s",   "per frame"},
        {  "s", "millisecond"},
        {  "s",   "per frame"},
        {"rad",      "degree"},
    };

    /** @brief 실수 칸인가(배율로 보이는 단위는 실수에만 건다). */
    bool isFloatType( const sw::hashed_string& typeName )
    {
        return typeName == sw::hashed_string( "float32" ) || typeName == sw::hashed_string( "float64" ) || typeName == sw::hashed_string( "float3" );
    }
} // namespace

/**
 * @brief [PropertyUnitsTest] PROPERTY 의 `Units` 는 값이 저장된 단위를 말한다 — 등록된 모든 타입(엔진 · GameFramework · 킷 · 게임)
 * @details 인스펙터는 `Units` 로 보이는 값 · 드래그 속도 · 단위 글자를 정한다(`InspectorPropertyLayout::getDisplayUnit`). 철자는 파서가 단위 표(`ReflectUnits.h`)로
 *          막지만, 표에 있는 단위가 값과 맞는지는 모른다. 규칙은 다섯이다. (1) `deg` 를 쓰지 않는다 — 엔진의 각도는 라디안이다. (2) 이름이 각도인 실수 칸은 `rad` 다.
 *          (3) `percent` 는 0..100 값이다 — 위 경계가 1 이하면 `ratio` 다. (4) `ratio` 의 위 경계는 1 이하, `rad` 의 위 경계는 2π 이하다.
 *          (5) 툴팁이 `Units` 와 다른 단위를 말하지 않는다(`kArrConflictingUnitWord`).
 */
SW_TEST_CASE( PropertyUnitsTest, UnitsMatchHowValuesAreStored )
{
    SW_REQUIRE_NOT_NULL( sw::HealthBarComponent::StaticType() );

    sw::vector<const sw::TypeInfo*> listType;
    sw::engine::getSubsystem<sw::TypeRegistry>().forEachType( [&listType]( const sw::TypeInfo& typeInfo )
    { listType.push_back( &typeInfo ); } );
    SW_REQUIRE_TRUE( listType.size() > 50 );

    constexpr float32 kMaxRadian = sw::MathUtil::kPi * 2.0f + 0.01f;
    sw::string        offenders;
    uint32            unitCount{ 0 };
    uint32            angleCount{ 0 };
    uint32            ratioCount{ 0 };
    for ( const sw::TypeInfo* pType : listType )
    {
        for ( const sw::PropertyInfo& prop : pType->_listProperty )
        {
            const sw::string* pUnits = prop.findCustomMeta( sw::hashed_string( "Units" ) );
            const sw::string  units  = ( pUnits != nullptr ) ? *pUnits : sw::string{};
            const sw::string  where  = sw::string( pType->_fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (Units=" + units + "): ";
            const bool        bMax   = prop._metadata->_bHasMaxRange != SW_FALSE;
            const float32     maxAt  = prop._metadata->_maxRange;
            if ( pUnits != nullptr )
                ++unitCount;
            if ( units == "deg" )
                offenders += where + "angles are stored in radians — write Units=rad\n";
            if ( isAngleName( prop._name ) && isFloatType( prop._typeName ) )
            {
                ++angleCount;
                if ( units != "rad" )
                    offenders += where + "an angle property must say Units=rad\n";
            }
            if ( units == "percent" && ( bMax == false || maxAt <= 1.0f ) )
                offenders += where + "a 0..1 value is a ratio — write Units=ratio (percent means 0..100)\n";
            if ( units == "ratio" )
            {
                ++ratioCount;
                if ( bMax && maxAt > 1.0f )
                    offenders += where + "a ratio is 0..1 but Max is above 1\n";
            }
            const bool bRadianMaxTooLarge = units == "rad" && bMax && maxAt > kMaxRadian;
            if ( bRadianMaxTooLarge )
                offenders += where + "Max is above 2*pi — the range looks like degrees\n";
            for ( const ConflictingUnitWord& conflict : kArrConflictingUnitWord )
            {
                if ( units == conflict._pUnits && sw::StringUtil::stristr( prop._metadata->_tooltip.c_str(), conflict._pWord ) != nullptr )
                    offenders += where + "the tooltip says '" + conflict._pWord + "' — tooltip and Units must name the same unit\n";
            }
        }
    }
    // 규칙이 아무것도 보지 않으면 이 테스트는 아무것도 지키지 않는다 — 회전 · FOV · 원뿔 각 둘 · HP 바 비율 셋이 있다.
    SW_EXPECT_TRUE( unitCount >= 10 );
    SW_EXPECT_TRUE( angleCount >= 4 );
    SW_EXPECT_TRUE( ratioCount >= 3 );
    SW_EXPECT_TRUE_MSG( offenders.empty(), offenders.c_str() );
}
