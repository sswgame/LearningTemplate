/**
 * @file InstancedStruct.h
 * @brief 형식을 런타임에 고르는 리플렉션 구조체 값입니다. 조건 인자 · 동작 칸 · 행동 트리 노드 칸이 이것으로 들립니다.
 * @details 등록된 이름마다 칸 구조체가 다르므로 값은 형식을 지운 채 든다. 만들기 · 복사는 등록 줄의 함수 표(`InstancedStructOps`)가 맡는다.
 *          칸 구조체의 함수는 등록한 이미지의 코드이므로 값은 그 이미지보다 오래 살 수 없다(`BehaviorTree` 의 함수 포인터와 같은 규칙 — 핫 리로드 뒤에는 다시 읽는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct TypeInfo;

    /** @brief 칸 구조체 하나의 함수 표입니다. `InstancedStructOps::of<T>()` 로 얻습니다. */
    struct InstancedStructOps
    {
        const TypeInfo* ( *_pfnGetType )();
        shared_ptr<void> ( *_pfnCreate )();
        shared_ptr<void> ( *_pfnClone )( const void* pSource );

        template <typename T>
        static const InstancedStructOps& of()
        {
            static const InstancedStructOps s_ops{
                []() -> const TypeInfo*
            { return T::StaticType(); },
                []() -> shared_ptr<void>
            { return sw::make_shared<T>(); },
                []( const void* pSource ) -> shared_ptr<void>
            { return sw::make_shared<T>( *static_cast<const T*>( pSource ) ); },
            };
            return s_ops;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class InstancedStruct
     * @brief 칸 구조체 값 하나입니다. 복사하면 값도 복사합니다(편집 모델이 노드를 복사해도 서로 섞이지 않는다).
     */
    class SW_API InstancedStruct
    {
    public:
        InstancedStruct();
        /** @brief 칸 구조체의 기본값으로 만듭니다. */
        explicit InstancedStruct( const InstancedStructOps& ops );
        InstancedStruct( const InstancedStruct& other );
        InstancedStruct& operator=( const InstancedStruct& other );
        InstancedStruct( InstancedStruct&& other ) noexcept            = default;
        InstancedStruct& operator=( InstancedStruct&& other ) noexcept = default;
        ~InstancedStruct()                                             = default;

        bool                      isValid() const { return _pInstance != nullptr; }
        const TypeInfo*           getType() const;
        const InstancedStructOps* getOps() const { return _pOps; }
        void*                     getInstance() { return _pInstance.get(); }
        const void*               getInstance() const { return _pInstance.get(); }
        /** @brief 읽기 전용으로 나눠 드는 값입니다. 컴파일된 식 · 호출이 듭니다(복사해도 값을 다시 만들지 않는다). */
        shared_ptr<const void> share() const { return _pInstance; }

        /** @brief 칸 구조체가 @p T 면 그 값입니다. 다른 형식이면 nullptr 입니다. */
        template <typename T>
        T* find()
        {
            return getType() == T::StaticType() ? static_cast<T*>( _pInstance.get() ) : nullptr;
        }
        template <typename T>
        const T* find() const
        {
            return getType() == T::StaticType() ? static_cast<const T*>( _pInstance.get() ) : nullptr;
        }

    private:
        const InstancedStructOps* _pOps;
        shared_ptr<void>          _pInstance;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct InstancedStructText
     * @brief 칸 구조체를 글 인자로 읽고 씁니다. `flag.set(hasKey, 1)` · `date(fromSeason=Spring, fromDay=20)` 처럼 차례로 또는 이름으로 적습니다.
     * @details 칸 이름 규칙은 XML 속성과 같다(`XMLFields::getAttributeName`). 차례는 칸을 선언한 순서이고, 목록 칸은 남은 글을 모두 받는다. 필수 칸 · `Min`/`Max` 는 `XMLFields::readProperties` 와 같이 본다.
     */
    struct SW_API InstancedStructText
    {
        /** @brief "a, b=c" 를 맨 위 쉼표로 나눠 앞뒤 공백을 뗍니다. 빈 글이면 목록도 비어 있습니다. */
        static void splitArguments( string_view text, vector<string_view>& outListToken );
        /**
         * @brief 글 인자를 칸에 읽습니다.
         * @return 칸이 모자라거나, 모르는 이름이거나, 읽지 못한 값 · 범위 밖 값 · 빠진 필수 칸이 있으면 그 까닭입니다.
         */
        [[nodiscard]] static expected<void> read( void* pInstance, const TypeInfo& typeInfo, const vector<string_view>& listToken );
        /** @brief 기본값과 다른 칸을 `이름=값` 으로 이어 씁니다(`read` 의 짝). 필수 칸은 늘 씁니다. 편집기가 칸을 고친 뒤 글로 되돌립니다. */
        [[nodiscard]] static string write( const void* pInstance, const void* pDefaults, const TypeInfo& typeInfo );
    };
} // namespace sw
