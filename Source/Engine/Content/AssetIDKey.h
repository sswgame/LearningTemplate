/**
 * @file AssetIDKey.h
 * @brief 에셋 id 를 대소문자 무시로 묶는 맵 · 집합의 열쇠 형식입니다.
 * @details 같은 에셋을 `Textures/A.png` 와 `textures/a.png` 로 적으면 대소문자를 가리지 않는 Windows 에서는 한 파일이고 Linux 에서는 둘이다.
 *          열쇠마다 손으로 접으면 한 곳이 빠뜨린 순간 같은 에셋이 두 자리가 된다 — 그래서 접기는 이 형식을 만들 때 한 번이고, 글에서 암시적으로 만들지 않는다.
 *          언리얼 `FName` 이 비교에서 대소문자를 무시하는 열쇠 형식인 것과 같은 자리다(유니티 · Godot 은 GUID · uid 가 열쇠라 경로 열쇠가 없다).
 *          접힌 글은 열쇠일 뿐이다 — 에셋을 읽을 때는 원래 표기(`AssetID`)를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"
#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @class AssetIDKey
     * @brief 소문자로 접은 에셋 id 하나입니다. 빈 값은 "id 없음" 입니다. 해시 컨테이너의 열쇠로 쓴다(`computeHash` — `DefaultHash`).
     */
    class SW_API AssetIDKey
    {
    public:
        AssetIDKey() = default;
        /** @brief 에셋 id 글 @p assetID 를 소문자로 접어 열쇠로 씁니다. 암시적 변환은 없다 — 열쇠를 만드는 자리를 드러낸다. */
        explicit AssetIDKey( string_view assetID );
        /** @brief @p assetID 를 소문자로 접어 열쇠로 씁니다. */
        explicit AssetIDKey( const AssetID& assetID )
            : AssetIDKey( assetID.view() )
        {
        }

        [[nodiscard]] bool          isEmpty() const { return _folded.empty(); }
        [[nodiscard]] const string& str() const { return _folded; }
        [[nodiscard]] string_view   view() const { return _folded; }
        /** @brief 해시 열쇠(`DefaultHash`)입니다. 접힌 글의 해시다. */
        [[nodiscard]] uint64 computeHash() const { return static_cast<uint64>( DefaultHash<string>{}( _folded ) ); }

        [[nodiscard]] bool operator==( const AssetIDKey& other ) const { return _folded == other._folded; }
        [[nodiscard]] bool operator!=( const AssetIDKey& other ) const { return _folded != other._folded; }
        /** @brief 접힌 글의 사전 순서입니다(색인 · 목록을 늘 같은 순서로 쓴다). */
        [[nodiscard]] bool operator<( const AssetIDKey& other ) const { return _folded < other._folded; }

    private:
        string _folded;
    };
} // namespace sw
