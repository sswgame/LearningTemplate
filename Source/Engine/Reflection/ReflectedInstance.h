/**
 * @file ReflectedInstance.h
 * @brief 리플렉션이 기본 생성자(`$ctor`)를 불러 만든 형식 소거 인스턴스의 이동 전용 소유자입니다.
 * @details 블록(`MemoryBlock`)과 그 타입(`TypeInfo`)을 함께 쥐고, 소멸하면 `TypeInfo::_destroyInstance` 로 소멸자를 부른 뒤 블록을 푼다.
 *          맨 `void*` 를 들고 `_destroyInstance` 를 손으로 짝지어 부르던 모양(누수 · 이중 해제가 형식에 안 보인다)을 대신한다.
 *          언리얼은 `UScriptStruct::InitializeStruct` · `DestroyStruct` 짝을 `FStructOnScope` 가 쥐는 것과 같은 자리다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/MemoryBlock.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw
{
    /**
     * @class ReflectedInstance
     * @brief 리플렉션 타입 하나의 인스턴스를 소유합니다. 비어 있을 수 있습니다(만들 수 없는 타입).
     */
    class SW_API ReflectedInstance
    {
    public:
        ReflectedInstance() noexcept = default;
        ~ReflectedInstance() { reset(); }

        ReflectedInstance( const ReflectedInstance& )            = delete;
        ReflectedInstance& operator=( const ReflectedInstance& ) = delete;
        ReflectedInstance( ReflectedInstance&& other ) noexcept;
        ReflectedInstance& operator=( ReflectedInstance&& other ) noexcept;

        /**
         * @brief @p type 의 기본 생성자로 인스턴스를 만듭니다. 생성자(`$ctor`)나 소멸 함수(`_destroyInstance`)가 없으면 빈 소유자입니다.
         * @details 블록은 `max_align_t` 정렬이다 — 리플렉션 타입은 그보다 큰 정렬을 요구하지 않는다(`TypeInfo::_size` 만 안다).
         */
        [[nodiscard]] static ReflectedInstance create( const TypeInfo& type );

        /** @brief 인스턴스 주소입니다. 비었으면 nullptr 입니다. */
        [[nodiscard]] void* get() const noexcept { return _block.get(); }
        /** @brief 인스턴스의 타입입니다. 비었으면 nullptr 입니다. */
        [[nodiscard]] const TypeInfo* getType() const noexcept { return _pType; }
        /** @brief 인스턴스가 있으면 true 입니다. */
        [[nodiscard]] bool isValid() const noexcept { return _block.get() != nullptr; }
        /** @brief 인스턴스를 소멸시키고 블록을 풉니다. 비었으면 아무것도 하지 않습니다. */
        void reset() noexcept;

    private:
        ReflectedInstance( const TypeInfo& type, MemoryBlock&& block ) noexcept;

        MemoryBlock     _block{};
        const TypeInfo* _pType{ nullptr };
    };
} // namespace sw
