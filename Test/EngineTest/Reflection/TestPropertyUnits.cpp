#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Base/UI/Marker/HealthBarComponent.h"

#include "TestFramework/TestFramework.h"

// Engine_Reflection — PROPERTY 의 `Units` 메타가 값이 저장된 단위와 맞는지 본다.
// PROPERTY 의 `Units` 메타는 에디터 메타데이터라 Shipping 빌드에는 없다(`findCustomMeta` 가 늘 nullptr) — 그 규칙 시험도 없다.

#if !defined( SW_SHIPPING )
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

    constexpr ConflictingUnitWord kArrConflictingUnitWord[] = {
        {  "m",        "tile"},
        {  "m",       "pixel"},
        {"m/s",        "tile"},
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
 * @brief [PropertyUnitsTest] PROPERTY 의 `Units` 는 값이 저장된 단위를 말한다 — 등록된 모든 타입(엔진 · GameFramework · 킷)
 * @details 인스펙터는 `Units` 로 보이는 값 · 드래그 속도 · 단위 글자를 정한다(`InspectorPropertyLayout::getDisplayUnit`). 그래서 메타가 틀리면 값이
 *          틀리게 보이고 틀린 속도로 움직인다(라디안을 `Units=deg` 로, 0..1 비율을 `Units=%` 로 적는 식). 규칙은 다섯이다.
 *          (1) `deg` 는 없다 — 엔진의 각도는 라디안이다(`setLocalRotation` · FOV · 원뿔 각). (2) 이름이 각도(angle · rotation · fov)인 실수 칸은
 *          `rad` 다. (3) `%` 는 0..100 값이다 — 위 경계가 1 이하면 비율이므로 `ratio` 다. (4) `ratio` 의 위 경계는 1 이하, `rad` 의 위 경계는 2π 이하다
 *          (도 범위를 라디안이라 적은 것을 잡는다). (5) 툴팁이 `Units` 와 다른 단위를 말하지 않는다 — `Units=m/s` 인 이동 속도의 툴팁이
 *          "tiles/sec" 이면 값을 넣는 사람은 어느 단위로 넣을지 모른다(`kArrConflictingUnitWord`).
 */
SW_TEST_CASE( PropertyUnitsTest, UnitsMatchHowValuesAreStored )
{
    // GameFramework 타입이 등록부에 있어야 이 시험이 HP 바를 본다(EngineTest 는 GameFramework 를 링크한다).
    SW_ASSERT_NOT_NULL( sw::HealthBarComponent::StaticType() );

    sw::vector<const sw::TypeInfo*> listType;
    sw::engine::getTypeRegistry().forEachType( [&listType]( const sw::TypeInfo& typeInfo )
    { listType.push_back( &typeInfo ); } );
    SW_ASSERT_TRUE( listType.size() > 50 );

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
            const bool        bMax   = prop._metadata._bHasMaxRange != SW_FALSE;
            const float32     maxAt  = prop._metadata._maxRange;
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
            if ( units == "%" && ( bMax == false || maxAt <= 1.0f ) )
                offenders += where + "a 0..1 value is a ratio — write Units=ratio (percent means 0..100)\n";
            if ( units == "ratio" )
            {
                ++ratioCount;
                if ( bMax && maxAt > 1.0f )
                    offenders += where + "a ratio is 0..1 but Max is above 1\n";
            }
            if ( units == "rad" && bMax && maxAt > kMaxRadian )
                offenders += where + "Max is above 2*pi — the range looks like degrees\n";
            for ( const ConflictingUnitWord& conflict : kArrConflictingUnitWord )
            {
                if ( units == conflict._pUnits && sw::StringUtil::stristr( prop._metadata._tooltip.c_str(), conflict._pWord ) != nullptr )
                    offenders += where + "the tooltip says '" + conflict._pWord + "' — tooltip and Units must name the same unit\n";
            }
        }
    }
    // 규칙이 아무것도 보지 않으면 이 시험은 아무것도 지키지 않는다 — 회전 · FOV · 원뿔 각 둘 · HP 바 비율 셋이 있다.
    SW_EXPECT_TRUE( unitCount >= 10 );
    SW_EXPECT_TRUE( angleCount >= 4 );
    SW_EXPECT_TRUE( ratioCount >= 3 );
    SW_EXPECT_TRUE_MSG( offenders.empty(), offenders.c_str() );
}
#endif // !SW_SHIPPING
