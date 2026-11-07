/**
 * @file AssetRef.h
 * @brief 타입 있는 에셋 참조 `AssetRef<T>` 와, 대상 종류의 캐시로 가는 길 `AssetRefCache<T>` 입니다.
 * @details 언리얼 `TSoftObjectPtr<T>` · 유니티 `AssetReferenceT<T>` 에 해당한다(기억에 의존). 값은 경로와 대상 종류이고 스스로 읽지 않는다.
 *            PROPERTY( Category = "Prefabs" )
 *            AssetRef<PrefabAsset> _enemyPrefab;                // 헤더 — T 는 앞선 선언으로 충분하다
 *            <EnemyDirector _enemyPrefab="game/x/enemy.prefab.xml"/>  // 파일 형식은 경로 글 그대로
 *          - `T` 는 `T::kAssetType` 을 가진다. 칸이 **만들어지는 곳**에서 `T` 가 완전해야 한다(`unique_ptr<T>` 의 소멸자와 같은 규칙이다). 생성자를 .cpp 에 둔 컴포넌트는
 *            .cpp 만 `T` 의 헤더를 읽으면 되고, 암시 생성자를 쓰는 구조체는 헤더가 읽는다(생성 코드의 팩토리가 그 자리에서 만든다).
 *          - 읽기: 접미사가 대상 종류의 것이 아니면 값을 넣지 않고 오류다(`AssetRefBase::assign`). 접미사가 없는 카탈로그는 종류의 경로 규칙(`/data/crops.xml`)으로,
 *            파일이 없는 내장 에셋(`Cube`)은 종류가 적은 이름 목록(`AssetTypeDef::withBuiltinNames`)으로 가른다. 내장 이름이 아닌 글은 폴백 없이 오류다.
 *          - 파생: 리플렉션 파서가 칸을 형식 "AssetRef" 로 적고 `AssetPath` 메타를 켠다. 그래서 프리로드(`ComponentAssetPathUtil`) · 핫 리로드 알림 · 참조 칸 생성 목록
 *            (`AssetReferenceFields.list`) · 인스펙터 선택기(종류로 거른다)가 칸마다 적지 않아도 이 칸을 본다. 참조 색인(`EditorReferenceIndex`)과 쿠킹은 파일 글을 읽으므로 그대로다.
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"

namespace sw
{
    /**
     * @brief `AssetRef<T>` 가 부르는 캐시입니다. 기본은 공유 캐시이고, 모양이 다른 캐시(메시 · 텍스처 · 머티리얼 · 프리팹)는 그 캐시 헤더가 특수화합니다.
     * @details GPU 수명이나 쓰는 쪽의 수정 때문에 캐시를 합치지 않은 종류가 있어(`SharedAssetCache.h` 머리말) 반환 형식이 대상마다 다르다. 쓰는 쪽은 `auto` 나 그 캐시의 형식으로 받는다.
     */
    template <typename T>
    struct AssetRefCache
    {
        static shared_ptr<const T>  acquire( string_view path ) { return SharedAssetCache<T>::acquire( path ); }
        static SharedAssetHandle<T> request( string_view path ) { return SharedAssetCache<T>::request( path ); }
    };

    /**
     * @class AssetRef
     * @brief 종류 @p T 의 에셋을 가리키는 값입니다. 파일 머리말 참고.
     */
    template <typename T>
    class AssetRef final : public AssetRefBase
    {
    public:
        AssetRef()
            : AssetRefBase{ T::kAssetType }
        {
            static_assert( T::kAssetType.canTellPath(), "AssetRef<T> needs a kind that tells its paths: a suffix, a path rule (catalog) or built-in names" );
        }
        /** @brief 코드에서 경로를 줍니다. 종류가 다르면 오류를 남기고 빈 참조입니다. */
        explicit AssetRef( string_view path )
            : AssetRefBase{ T::kAssetType }
        {
            static_assert( T::kAssetType.canTellPath(), "AssetRef<T> needs a kind that tells its paths: a suffix, a path rule (catalog) or built-in names" );
            (void)assign( path );
        }
        AssetRef( const AssetRef& )                = default;
        AssetRef( AssetRef&& ) noexcept            = default;
        AssetRef& operator=( const AssetRef& )     = default;
        AssetRef& operator=( AssetRef&& ) noexcept = default;
        ~AssetRef()                                = default;

        /** @brief 경로를 넣습니다(`assign`). 종류가 다르면 오류를 남기고 값은 그대로입니다. */
        AssetRef& operator=( string_view path )
        {
            (void)assign( path );
            return *this;
        }

        /**
         * @brief 대상을 지금 읽어 받습니다. 빈 참조면 읽지 않고 캐시의 빈 값입니다.
         * @details 멤버 템플릿인 까닭: 칸이 있는 헤더는 `T` 를 앞선 선언으로만 알 수 있다. 칸의 형식을 쓰는 자리에서 `AssetRefCache<T>` 가 인스턴스화되면 뒤에 오는 그 캐시 헤더의 특수화가 컴파일 오류다.
         */
        template <typename TTarget = T>
        [[nodiscard]] auto acquire() const
        {
            using Result = decltype( AssetRefCache<TTarget>::acquire( string_view{} ) );
            if ( empty() )
                return Result{};
            return AssetRefCache<TTarget>::acquire( path() );
        }
        /** @brief 대상을 비동기로 요청합니다. 빈 참조면 빈 핸들입니다. */
        template <typename TTarget = T>
        [[nodiscard]] auto request() const
        {
            using Result = decltype( AssetRefCache<TTarget>::request( string_view{} ) );
            if ( empty() )
                return Result{};
            return AssetRefCache<TTarget>::request( path() );
        }
    };
} // namespace sw
