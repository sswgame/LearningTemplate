/**
 * @file ComponentAssetPathUtil.h
 * @brief 컴포넌트의 에셋 경로 값(`PROPERTY( AssetPath )` · `AssetType = …` · `AssetRef<T>`, 그 시퀀스, 중첩 구조체 안의 것)을 리플렉션으로 찾아 돕니다.
 * @details 에디터 핫 리로드(바뀐 파일을 쓰는 컴포넌트 찾기) · 씬 프리로드 목록 수집(`ScenePreloadList`) · 에셋 리다이렉트 반영(에디터 옮김 `EditorAssetReferenceUtil`)이
 *          같은 순회를 쓴다 — 경로 칸의 판정(형식 · 시퀀스 · 중첩 구조체)이 세 곳에서 갈라지지 않게 한 곳에 둔다. 예전에는 맨 위 스칼라 칸만 읽어 시퀀스와
 *          중첩 구조체 안의 경로를 셋 모두 놓쳤다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;

    /** @brief 에셋 경로 값의 C++ 형식입니다. */
    enum class AssetPathValueKind : uint8
    {
        AssetRef,     ///< `AssetRef<T>`(`AssetRefBase`)
        String,       ///< `PROPERTY( AssetPath ) string`
        HashedString, ///< `PROPERTY( AssetPath ) hashed_string`
    };

    /**
     * @struct AssetPathValue
     * @brief 컴포넌트 안의 에셋 경로 값 하나를 가리킵니다(`ComponentAssetPathUtil::forEachAssetPath` 가 넘긴다). 그 순회 안에서만 씁니다.
     */
    struct SW_API AssetPathValue
    {
        void*               _pValue{ nullptr };                  ///< 값의 주소(`_kind` 의 형식)
        AssetPathValueKind  _kind{ AssetPathValueKind::String }; ///< 값의 형식
        const PropertyInfo* _pTopProperty{ nullptr };            ///< 이 값을 품은 컴포넌트의 맨 위 칸 — 바꾼 뒤 `onPropertyChanged` 에 넘길 이름

        /** @brief 경로 글입니다. 내장 에셋(`AssetRefBase::isBuiltin` — `Cube`)은 파일이 아니라 빈 값입니다. */
        [[nodiscard]] string_view getPath() const;
        /**
         * @brief 경로를 @p path 로 바꿉니다 — 에셋 리다이렉트를 런타임 메모리에 반영하는 일이라 에디터 옮김과 런타임 핫 리로드가 같이 쓴다(언리얼 CoreRedirects 가 로드된 객체에 적용되는 자리).
         * @return `AssetRef` 의 종류 검사(`assign`)에 지면 false 이고 값은 그대로입니다.
         */
        [[nodiscard]] bool assign( string_view path ) const;
    };

    /** @brief 에셋 경로 값 하나마다 부르는 함수입니다. */
    using AssetPathVisitor = Delegate<void( const AssetPathValue& )>;

    struct SW_API ComponentAssetPathUtil
    {
        /**
         * @brief @p component 의 에셋 경로 값마다 @p visit 을 부릅니다 — 상속 칸, `AssetPath` 시퀀스의 원소, 리플렉션 구조체 칸(과 그 시퀀스 원소) 안으로 내려간다.
         * @details 연관 컨테이너(맵 · 집합)는 원소가 정렬 키라 제자리에서 고칠 수 없어 내려가지 않는다(인스펙터와 같은 규칙, `allowsInPlaceElementWrite`).
         */
        static void forEachAssetPath( Component& component, const AssetPathVisitor& visit );
        /** @brief 리플렉션 타입 @p type 의 인스턴스 @p pInstance 에 같은 순회를 합니다(컴포넌트가 아닌 데이터 — 맨 위 칸은 그 인스턴스의 칸이다). */
        static void forEachAssetPath( const TypeInfo& type, void* pInstance, const AssetPathVisitor& visit );
    };
} // namespace sw
