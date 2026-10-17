/**
 * @file EnumName.h
 * @brief 리플렉션 `ENUM()` 열거자의 이름을 로그 · 진단 · 화면 표시용으로 읽습니다.
 * @details 열거형마다 이름 배열과 `toString` 을 손으로 두지 않습니다. `ENUM()` 을 붙이면 코드 생성기가 이름을 등록하고, 이 함수가 그 이름을 돌려줍니다.
 *          언리얼 `UEnum::GetValueAsString`, Godot 의 `VARIANT_ENUM_CAST` + ClassDB 열거 상수와 같은 자리입니다.
 *          철자가 열거자 이름과 다른 와이어 · 데이터 문자열(`AdminRole` 의 `"viewer"`, 지표 라벨)은 이 함수가 아니라 그 형식의 이름 표가 정본입니다.
 *          레지스트리는 엔진 범위의 `TypeRegistry` 이므로 Engine 밖(GameFramework · Editor · 게임)에서도 범위를 몰라도 씁니다.
 *
 *          템플릿 `getEnumName<E>` 는 찾은 `EnumInfo` 를 열거형마다 함수 지역 캐시(`EnumNameCache`)에 둡니다. 호출마다 잠금과 해시 맵 조회를 하지 않습니다.
 *          열거자 값이 `0..N-1` 로 빈틈없으면 이름도 목록 인덱스로 찾습니다(`EnumInfo::_listDenseName`).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    /** @brief @p enumTypeFqn(`typeFqn<E>()`)의 @p value 이름입니다. 엔진 서비스가 없거나 등록되지 않았거나 범위 밖이면 `"Unknown"` 입니다. 템플릿 `getEnumName` 을 씁니다. */
    SW_API const utf8* getEnumName( const hashed_string& enumTypeFqn, int64 value );

    /**
     * @brief @p enumTypeFqn 의 @p value 이름입니다. 엔진 서비스가 없거나 등록되지 않았거나 범위 밖이면 nullptr 입니다.
     * @details 표시용 `getEnumName` 과 달리 대신 쓸 이름이 없습니다. 이름이 와이어 해시에 들어가는 곳(`OnlineProtocolEntry::computeMethodHash`)처럼 없으면 멈춰야 하는 쪽이 씁니다.
     */
    SW_API const utf8* findEnumName( const hashed_string& enumTypeFqn, int64 value );
} // namespace sw

namespace sw
{
    /**
     * @brief 열거형 하나의 `findEnumByTypeFqn` 결과를 타입 테이블 세대와 함께 기록해 두는 캐시입니다.
     * @details `EnumInfo` 의 주소는 레지스트리 안에서 고정이지만(묘비), 모듈 해제는 FQN 조회를 nullptr 로 바꾸고 레지스트리 파괴는 객체를 지웁니다.
     *          둘 다 세대(`TypeRegistry::getGeneration`)를 올리므로, 세대가 그대로일 때만 기록한 답을 씁니다. 답과 세대는 한 쌍으로 기록합니다
     *          (`TypeCacheSlot`, `TypeLookupCache` 와 같은 칸). 찾지 못한 답도 기록하고, 열거형 등록 · 별칭이 세대를 올려 다시 찾게 합니다.
     */
    struct SW_API EnumNameCache
    {
        static constexpr const utf8* kUnknownName = "Unknown"; ///< 표시용 `getEnumName` 이 이름을 찾지 못했을 때 돌려주는 글입니다

        TypeCacheSlot<EnumInfo> _slot;

        /** @brief @p enumTypeFqn 의 EnumInfo 입니다. 엔진 서비스가 없거나 등록되지 않았으면 nullptr 입니다. */
        const EnumInfo* find( const hashed_string& enumTypeFqn ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief @p value 의 열거자 이름입니다. `ENUM()` 이 없거나 범위 밖(`Count` 포함)이면 `"Unknown"` 입니다. */
    template <typename E>
    const utf8* getEnumName( E value )
    {
        static_assert( std::is_enum_v<E>, "getEnumName requires an enum type" );
        static const EnumNameCache s_cache;
        const EnumInfo*            pInfo = s_cache.find( typeFqn<E>() );
        const utf8*                pName = pInfo != nullptr ? pInfo->valueToCString( static_cast<int64>( value ) ) : nullptr;
        return pName != nullptr ? pName : EnumNameCache::kUnknownName;
    }
} // namespace sw
