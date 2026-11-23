/**
 * @file ReflectValueBinding.h
 * @brief 프로퍼티 값 · 원소 · 키 형식에 묶는 레지스트리 사실(내장형 색인 · `TypeInfo*` · `EnumInfo*`)입니다.
 * @details 직렬화기는 값 하나마다 형식 이름으로 처리기 표 · 열거형 · 구조체를 다시 찾았다(해시 조회 최대 3 번 · 공유 잠금 2 번, 컨테이너 원소마다 반복).
 *          이 묶음은 레지스트리 세대(`getTypeTableGeneration`)마다 한 번 찾아 두고, 같은 세대 동안은 원자 읽기 몇 번으로 답한다 — 재연결 · 등록 · 해제가
 *          세대를 올리므로 그 뒤 첫 조회가 다시 묶는다(모듈 해제로 사라진 형식을 들고 있지 않다). 언리얼 `FProperty` 가 링크 때 형식을 풀어 두는 것,
 *          Godot `PropertyInfo.type` 이 `Variant::Type` 인 것과 같은 자리다. 처리기(직렬화 함수)는 문맥마다 달라(`SerializeContext` 의 자기 표 → 빌린 표)
 *          여기 묶지 않고, 문맥이 내장형 색인으로 고른다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    struct TypeInfo;
    struct EnumInfo;
    class TypeRegistry;
    enum class ReflectBuiltinIndex : uint8; // 정의는 ReflectedValue.h — 이 머리는 그 표를 끌지 않는다

    /** @brief 형식 이름 하나의 레지스트리 사실입니다. 셋 다 비면 레지스트리가 모르는 이름입니다(처리기만 있는 형식일 수 있다). */
    struct SW_API ReflectValueFacts
    {
        const TypeInfo*     _pType{ nullptr }; ///< 레지스트리의 형식(내장형의 원시 형식 포함 — 구조체인지는 `isPrimitive` 로)
        const EnumInfo*     _pEnum{ nullptr }; ///< 열거형이면 그 정보
        ReflectBuiltinIndex _builtinIndex;     ///< 내장형이면 표 안 자리, 아니면 `kNone`(별칭은 정규 이름으로 푼 뒤) — `resolve` 가 늘 채운다

        /** @brief 반사 구조체(내장 타입 · 열거형이 아닌 등록 타입)면 그 타입, 아니면 nullptr 입니다. */
        const TypeInfo* findStructType() const;

        /** @brief @p name 의 사실을 묶음 없이 @p registry 에서 이름으로 찾습니다. */
        static ReflectValueFacts find( const hashed_string& name, const TypeRegistry& registry );
    };

    /**
     * @brief 형식 이름 하나에 묶는 사실의 세대 캐시입니다. 쓰는 쪽은 잠그지 않는 순번 잠금(seqlock)이고 읽는 쪽은 원자 읽기뿐입니다.
     * @details 묶음은 그 이름의 사실만 든다 — 부르는 쪽이 같은 이름(`PropertyInfo::_typeName` 등)을 넘기고, 캐시는 이름 해시도 견줘 칸 이름을 바꾼 사본이
     *          옛 사실을 쓰지 않게 한다. 복사는 캐시를 옮기지 않는다(새 사본은 처음 조회에서 묶는다).
     */
    class SW_API ReflectValueBinding
    {
    public:
        ReflectValueBinding() noexcept = default;
        ReflectValueBinding( const ReflectValueBinding& ) noexcept {}
        ReflectValueBinding& operator=( const ReflectValueBinding& ) noexcept
        {
            invalidate();
            return *this;
        }

        /** @brief @p name 의 사실입니다. 이 세대에 묶은 것이 있으면 그것, 없으면 @p registry 에서 찾아 묶습니다. */
        ReflectValueFacts resolve( const hashed_string& name, const TypeRegistry& registry ) const;

    private:
        [[nodiscard]] bool tryRead( uint32 generation, uint32 nameHash, ReflectValueFacts& outFacts ) const;
        void               write( uint32 generation, uint32 nameHash, const ReflectValueFacts& facts ) const;
        void               invalidate() noexcept { _sequence.store( 0, std::memory_order_release ); }

    private:
        mutable atomic<uint32>          _sequence{ 0 };     ///< 0 이면 묶은 적 없음, 홀수면 쓰는 중, 그 밖이면 아래 칸이 온전하다
        mutable atomic<uint32>          _generation{ 0 };   ///< 묶은 레지스트리 세대
        mutable atomic<uint32>          _nameHash{ 0 };     ///< 묶은 이름의 해시
        mutable atomic<const TypeInfo*> _pType{ nullptr };  ///< `ReflectValueFacts::_pType`
        mutable atomic<const EnumInfo*> _pEnum{ nullptr };  ///< `ReflectValueFacts::_pEnum`
        mutable atomic<uint8>           _builtinIndex{ 0 }; ///< `ReflectValueFacts::_builtinIndex`
    };

    /**
     * @brief 프로퍼티 하나의 값 · 원소 · 키 형식 묶음입니다. `PropertyInfo` 가 공유 포인터로 든다 — 사본끼리 나눠도 묶음은 이름으로 확인하므로 맞고,
     *        프로퍼티 머리(`PropertyInfo.h`)가 원자 칸을 끌지 않는다.
     */
    struct PropertyValueBindings
    {
        ReflectValueBinding _value;
        ReflectValueBinding _element;
        ReflectValueBinding _key;
    };
} // namespace sw
