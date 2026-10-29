/**
 * @file TypeCastHeader.h
 * @brief 캐스트가 읽는 `TypeInfo` 의 머리 — 조상 표(루트부터 자기까지의 이름 번호와 자기 깊이) · 살아 있음과 그 인라인 비교입니다. `TypeInfo` 의 맨 앞 멤버입니다.
 * @details 캐스트(`castTo`)가 `ReflectionTypes.h`(약 700 TU 에 닿는다)를 끌지 않고도 파생 검사를 인라인으로 하게 떼었다 — 게임 코드의 충돌 · 겹침 콜백처럼
 *          등록부 없이 캐스트하는 자리가 매번 줄 밖 호출을 치르지 않는다(언리얼 `UStruct::IsChildOf` 가 인라인 `StructBaseChainArray` 비교인 것과 같다).
 *          `TypeInfo` 는 전방 선언만 보여도 되도록 이 표를 **맨 앞 멤버**로 두고 `TypeCastHeader::of` 가 그 주소를 읽는다. 맨 앞이라는 것은 Reflection.cpp 의
 *          `static_assert` 가 지킨다. 표가 아직 없거나 만들 수 없으면 `tryCompare` 는 답하지 않고, 부르는 쪽이 줄 밖 느린 길(`TypeInfo::isDerivedFromSlow`)로 간다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionConstants.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @struct TypeCastHeader
     * @brief 캐스트가 읽는 `TypeInfo` 의 머리입니다. 표의 뜻과 비우는 때는 `TypeInfo::_castHeader` 의 설명을 본다.
     */
    struct TypeCastHeader
    {
        /** @brief 루트부터 자기까지의 이름(FQN 의 intern 인덱스)입니다. 자기 자리는 `_ancestorDepth` 가 가리킨다. */
        mutable atomic<uint32> _arrAncestorNameIndex[constant::reflection::kAncestorDisplayDepth];
        /** @brief 표에서 자기 자리의 깊이입니다. Unknown 이면 아직 만들지 않았고, None 이면 만들 수 없어 부모 포인터를 따라간다. */
        mutable atomic<uint8> _ancestorDepth{ constant::reflection::kAncestorDepthUnknown };
        /**
         * @brief 레지스트리에 살아 있는 타입인지입니다. 모듈을 해제하면 항목을 지우지 않고 이 값을 내린다(묘비). 같은 FQN 을 다시 등록하면 올린다. 사본은 늘 살아 있다.
         * @details 머리에 두는 까닭은 컴포넌트의 타입 캐시(`Component::findCachedTypeInfo`)가 `ReflectionTypes.h` 없이 인라인으로 읽게 하려는 것이다.
         */
        mutable atomic<uint8> _bAlive{ SW_TRUE };

        bool isAlive() const { return _bAlive.load( std::memory_order_acquire ) != SW_FALSE; }

        /**
         * @brief 두 표가 모두 섰으면 "이 타입이 @p target 이거나 그 파생인가" 를 @p outDerived 에 담고 true 입니다. 어느 쪽이든 표가 없으면 false 입니다(느린 길).
         * @details `표[target 의 깊이] == target 의 이름` 한 번이다. 포인터가 아니라 이름을 비교하므로 레지스트리 밖 사본(테스트 목의 `StaticType()`)도 이름이 같으면 같은 타입이다.
         */
        bool tryCompare( const TypeCastHeader& target, bool& outDerived ) const
        {
            const uint8 selfDepth   = _ancestorDepth.load( std::memory_order_acquire );
            const uint8 targetDepth = target._ancestorDepth.load( std::memory_order_acquire );
            if ( selfDepth >= constant::reflection::kAncestorDisplayDepth || targetDepth >= constant::reflection::kAncestorDisplayDepth )
                return false;
            outDerived = targetDepth <= selfDepth && _arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed ) ==
                                                         target._arrAncestorNameIndex[targetDepth].load( std::memory_order_relaxed );
            return true;
        }

        /** @brief @p pType 의 조상 표입니다(`TypeInfo` 의 맨 앞 멤버). @p pType 은 nullptr 이 아니어야 한다. */
        static const TypeCastHeader& of( const TypeInfo* pType ) { return *reinterpret_cast<const TypeCastHeader*>( pType ); }
    };
} // namespace sw
