/**
 * @file WeakInternCache.h
 * @brief 코드로 짓는 값(내장 도형 · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)을 키로 나눠 주는 표이자, 그 표를 에셋 캐시 등록부에 보이는 창구입니다.
 * @details 표는 종류마다 프로세스에 하나(Engine.dll 안의 함수 정적 객체)이고, `AssetManager` 가 생성자에서 등록부에 올립니다. 그래서 진단
 *          (`getCachedCount`)과 종료 · 재초기화의 비우기(`clear`)가 경로 캐시와 같은 길을 탑니다. 키가 리소스 경로가 아니므로 핫 리로드로 다시 읽을
 *          것이 없습니다(`isCached` 는 false, `reload` 는 할 일이 없다) — 값이 기대는 파일(텍스처 · 머티리얼)은 그 파일의 캐시가 제자리로 다시 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"
#include "Engine/Resource/Cache/WeakInternTable.h"

namespace sw
{
    /**
     * @class WeakInternCache
     * @brief `WeakInternTable` 하나를 들고 `IAssetCache` 로 보이는 캐시입니다.
     * @tparam KeyType   값을 정하는 키입니다.
     * @tparam ValueType 나눠 줄 값의 타입입니다.
     * @tparam KeyHash   키 해시 함수 객체입니다.
     */
    template <typename KeyType, typename ValueType, typename KeyHash = std::hash<KeyType>>
    class WeakInternCache final : public IAssetCache
    {
    public:
        /** @brief 값을 짓는 함수입니다. 지을 수 없으면 nullptr 을 돌려줍니다. */
        using CreateFunction = shared_ptr<ValueType> ( * )( const KeyType& key );

        /** @param pKindName 등록부의 종류 이름입니다(정적 문자열). */
        explicit WeakInternCache( const utf8* pKindName )
            : _pKindName{ pKindName }
            , _table{}
        {
        }

        /** @brief 그 키의 값을 나눠 받습니다. 처음이면 @p create 로 짓습니다. 워커에서 불러도 됩니다(잠급니다). */
        shared_ptr<ValueType> acquire( const KeyType& key, CreateFunction create ) { return _table.acquire( key, create ); }

        const utf8* getAssetKindName() const override { return _pKindName; }
        /** @brief 키가 리소스 경로가 아니라 늘 false 입니다. */
        bool isCached( string_view relativePath ) const override
        {
            (void)relativePath;
            return false;
        }
        /** @brief 다시 읽을 파일이 없습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override
        {
            (void)relativePath;
            (void)pDevice;
        }
        /** @brief 지금 누가 쥔 값의 수입니다. */
        size_t getCachedCount() const override { return _table.countLive(); }
        /** @brief 표를 비웁니다. 쥔 쪽의 값은 그대로이고, 다음 요청은 새로 짓습니다. */
        void clear() override { _table.clear(); }

    private:
        const utf8*                                  _pKindName;
        WeakInternTable<KeyType, ValueType, KeyHash> _table;
    };
} // namespace sw
