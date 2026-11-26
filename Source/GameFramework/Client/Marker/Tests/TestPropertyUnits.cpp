#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionUnits.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/Client/Marker/HealthBarComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

// PropertyUnitsTest — PROPERTY 의 `Units` 메타가 값이 저장된 단위와 맞는지 등록된 모든 타입에서 본다(GameFramework 에 둬 HP 바 비율도 늘 본다).

namespace
{
    /**
     * @brief 각도를 담는 이름인가 — 회전 · 각 · 시야각이 **낱말로** 들어 있다(이름의 처음, `_` 뒤, 낙타 표기의 대문자 시작).
     * @details 낱말 경계를 보지 않으면 `triangle_ratio` 의 "angle" 을 각도로 읽었다(캐릭터 LOD 규칙).
     */
    bool isAngleName( const sw::hashed_string& name )
    {
        const sw::string_view text = name.view();
        for ( const sw::string_view word : { sw::string_view{ "angle" }, sw::string_view{ "rotation" }, sw::string_view{ "fov" } } )
        {
            for ( size_t at = 0; at + word.size() <= text.size(); ++at )
            {
                if ( sw::StringUtil::equals( text.substr( at, word.size() ), word, true ) == false )
                    continue;
                const bool bWordStart = at == 0 || text[at - 1] == '_' || ( 'A' <= text[at] && text[at] <= 'Z' );
                if ( bWordStart )
                    return true;
            }
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
 * @details 인스펙터는 `Units` 로 보이는 값 · 드래그 속도 · 단위 글자를 정한다(`InspectorPropertyLayout::getDisplayUnit`). 철자는 파서가 단위 표(`ReflectionUnits.h`)로
 *          막고 `deg` 는 파서가 거절하지만(도로 적는 칸은 `Radians` · `EulerAngles` 다), 표에 있는 단위가 값과 맞는지는 모른다. 규칙은 다섯이다. (1) 이름이 각도인 실수 칸은 `rad` 다.
 *          (2) `percent` 는 0..100 값이다 — 위 경계가 1 이하면 `ratio` 다. (3) `ratio` 의 위 경계는 1 이하, `rad` 의 위 경계는 2π 이하다.
 *          (4) 툴팁이 `Units` 와 다른 단위를 말하지 않는다(`kArrConflictingUnitWord`). (5) `Radians` · `EulerAngles` 칸은 `Units` 를 적지 않는다 — 형식이 단위를 말하고,
 *          바깥 표현(XML 글 · 인스펙터 · `Min`/`Max`)은 늘 도다(`PackedValueTraits<Radians>`). 라디안 칸에 `Units = rad` 를 더하면 인스펙터가 도를 다시 도로 바꾼다.
 */
SW_TEST_CASE( PropertyUnitsTest, UnitsMatchHowValuesAreStored )
{
    SW_REQUIRE_NOT_NULL( sw::HealthBarComponent::StaticType() );

    sw::vector<const sw::TypeInfo*> listType;
    sw::game::getSubsystem<sw::TypeRegistry>().forEachType( [&listType]( const sw::TypeInfo& typeInfo )
    { listType.push_back( &typeInfo ); } );
    SW_REQUIRE_TRUE( listType.size() > 50 );

    constexpr float32                                 kMaxRadian = sw::MathUtil::kPi * 2.0f + 0.01f;
    sw::StringBuilder<sw::constant::buffer::kMax4096> offenders;
    uint32                                            unitCount{ 0 };
    uint32                                            angleCount{ 0 };
    uint32                                            ratioCount{ 0 };
    uint32                                            radiansCount{ 0 };
    for ( const sw::TypeInfo* pType : listType )
    {
        for ( const sw::PropertyInfo& prop : pType->_listProperty )
        {
            const sw::ReflectUnit*                          pUnits = sw::ReflectUnit::findUnit( prop._metadata->_unit );
            const sw::string                                units  = ( pUnits != nullptr ) ? sw::string{ pUnits->_pName } : sw::string{};
            sw::fixed_string<sw::constant::buffer::kMax512> where;
            sw::formatstring( where.data(), where.capacity(), "%#::%# (Units=%#): ", pType->_fullyQualifiedName.c_str(), prop._name.c_str(), units );
            const bool    bMax  = prop._metadata->_bHasMaxRange != SW_FALSE;
            const float32 maxAt = prop._metadata->_maxRange;
            if ( pUnits != nullptr )
                ++unitCount;
            if ( prop._typeName == sw::hashed_string( "Radians" ) || prop._typeName == sw::hashed_string( "EulerAngles" ) )
            {
                ++radiansCount;
                if ( pUnits != nullptr )
                    offenders.appendFormat( "%#an angle type names its unit by type — remove Units\n", where.c_str() );
            }
            if ( isAngleName( prop._name ) && isFloatType( prop._typeName ) )
            {
                ++angleCount;
                if ( units != "rad" )
                    offenders.appendFormat( "%#an angle property must say Units=rad\n", where.c_str() );
            }
            if ( units == "percent" && ( bMax == false || maxAt <= 1.0f ) )
                offenders.appendFormat( "%#a 0..1 value is a ratio — write Units=ratio (percent means 0..100)\n", where.c_str() );
            if ( units == "ratio" )
            {
                ++ratioCount;
                if ( bMax && maxAt > 1.0f )
                    offenders.appendFormat( "%#a ratio is 0..1 but Max is above 1\n", where.c_str() );
            }
            const bool bRadianMaxTooLarge = units == "rad" && bMax && maxAt > kMaxRadian;
            if ( bRadianMaxTooLarge )
                offenders.appendFormat( "%#Max is above 2*pi — the range looks like degrees\n", where.c_str() );
            for ( const ConflictingUnitWord& conflict : kArrConflictingUnitWord )
            {
                if ( units == conflict._pUnits && sw::StringUtil::stristr( prop._metadata->getEditor()._tooltip.c_str(), conflict._pWord ) != nullptr )
                    offenders.appendFormat( "%#the tooltip says '%#' — tooltip and Units must name the same unit\n", where.c_str(), conflict._pWord );
            }
        }
    }
    // 규칙이 아무것도 보지 않으면 이 테스트는 아무것도 지키지 않는다 — 회전 · FOV · 원뿔 각 둘 · HP 바 비율 셋, 무기 퍼짐 · 리그 관절 제한의 `Radians` 칸이 있다.
    SW_EXPECT_TRUE( unitCount >= 10 );
    SW_EXPECT_TRUE( radiansCount >= 8 );
    SW_EXPECT_TRUE( angleCount >= 4 );
    SW_EXPECT_TRUE( ratioCount >= 3 );
    SW_EXPECT_TRUE_MSG( offenders.size() == 0, offenders.c_str() );
}
