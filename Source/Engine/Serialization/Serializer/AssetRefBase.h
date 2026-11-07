/**
 * @file AssetRefBase.h
 * @brief 타입 있는 에셋 참조(`AssetRef<T>`)의 값 부분입니다 — 경로와 대상 종류를 품고, 스스로 읽지 않습니다.
 * @details 직렬화는 이 형식 하나만 안다. `AssetRef<T>` 는 칸을 더하지 않으므로 어떤 `T` 든 처리기 "AssetRef" 하나가 읽고 쓴다(파서가 `TComponentHandle<T>` 를 `ComponentHandle`
 *          하나로 모으는 것과 같은 길이다). 대상 종류를 프로퍼티 메타가 아니라 값이 품는 까닭은 글 처리기가 값 주소만 받기 때문이다 — `vector<AssetRef<T>>` 의 원소, 중첩 구조체,
 *          덮어쓰기 패치, 인스펙터가 모두 같은 검사를 지난다.
 *          파일 형식은 경로 글 그대로다(언리얼 `TSoftObjectPtr<T>` 의 저장 형식과 같다 — 기억에 의존). 층이 직렬화라 캐시를 모른다. 캐시로 가는 길은 `Engine/Asset/AssetRef.h` 가 잇는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

namespace sw
{
    class SerializeContext;

    /**
     * @class AssetRefBase
     * @brief 경로(리소스 id)와 대상 종류입니다. 직접 만들지 않고 `AssetRef<T>` 로 씁니다.
     */
    class SW_API AssetRefBase
    {
    public:
        /** @brief 리플렉션 형식 이름입니다. 파서가 `AssetRef<T>` 칸의 형식을 이 이름으로 적는다(`AstVisitor`). */
        static constexpr const utf8* kTypeName = "AssetRef";

        /** @brief 참조가 비었으면 true 입니다. 빈 참조는 "가리키지 않음" 이고 종류 검사를 하지 않는다. */
        [[nodiscard]] bool empty() const { return _path.empty(); }
        /** @brief 경로 글입니다. */
        [[nodiscard]] const string& path() const { return _path; }
        /** @brief 경로 글(끝에 0)입니다. */
        [[nodiscard]] const utf8* c_str() const { return _path.c_str(); }
        /** @brief 리소스 입구(`AssetIO`)에 넘길 id 입니다. */
        [[nodiscard]] AssetID id() const { return AssetID{ _path }; }
        /** @brief 경로를 받는 함수(`string_view`)에 그대로 넘깁니다. 이 값은 이미 경로라 짐작하는 변환이 아니다. */
        operator string_view() const noexcept { return _path; } // NOLINT(google-explicit-constructor)
        /** @brief 대상 종류입니다. */
        [[nodiscard]] const AssetTypeDef& getTargetType() const { return *_pTargetType; }

        [[nodiscard]] bool operator==( string_view other ) const { return string_view( _path ) == other; }
        [[nodiscard]] bool operator!=( string_view other ) const { return string_view( _path ) != other; }
        [[nodiscard]] bool operator==( const AssetRefBase& other ) const { return _path == other._path; }
        [[nodiscard]] bool operator!=( const AssetRefBase& other ) const { return _path != other._path; }

        /**
         * @brief 경로를 넣습니다. 앞뒤 공백을 뺀 경로의 접미사가 대상 종류의 것이 아니면 오류를 남기고 false 이며 값은 그대로입니다. 빈 글은 칸을 비웁니다.
         * @details 쓰는 쪽의 `.empty()` 분기가 그대로 동작하도록 빈 글은 오류가 아니다. 필수 칸은 `PROPERTY( Required )` 가 본다.
         */
        [[nodiscard]] bool assign( string_view path );
        /** @brief 참조를 비웁니다. */
        void clear() { _path.clear(); }

        /**
         * @brief @p path 가 종류 @p type 의 에셋이면 true 입니다 — 내장 이름(철자 그대로), 접미사(대소문자 무시), 접미사가 없는 카탈로그는 경로 규칙(`AssetPathRule`) 순으로 본다.
         * @details 셋 다 없는 종류는 경로로 가를 수 없어 어떤 경로도 받지 않는다(`AssetRef<T>` 는 그런 대상을 컴파일에서 막는다 — `AssetTypeDef::canTellPath`).
         */
        [[nodiscard]] static bool isPathOfTarget( string_view path, const AssetTypeDef& type );
        /** @brief 값이 대상 종류의 내장 에셋 이름(`Cube`)이면 true 입니다. 내장 에셋은 파일이 아니므로 캐시 · 쿠킹 · 참조 검사가 파일로 찾지 않는다. */
        [[nodiscard]] bool isBuiltin() const;

        /** @brief 리플렉션 글 처리기("AssetRef")를 직렬화 문맥에 등록합니다. 기본 문맥이 만들어질 때 한 번 부른다. */
        static void registerSerializeHandler( SerializeContext& context );

    protected:
        explicit AssetRefBase( const AssetTypeDef& targetType )
            : _path{}
            , _pTargetType{ &targetType }
        {
        }

    private:
        string              _path;
        const AssetTypeDef* _pTargetType; ///< `T::kAssetType` 의 주소다. 이 값을 가진 코드의 이미지에 있으므로 값보다 오래 산다
    };
} // namespace sw
