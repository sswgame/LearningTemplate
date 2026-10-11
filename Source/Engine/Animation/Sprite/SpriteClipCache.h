/**
 * @file SpriteClipCache.h
 * @brief 스프라이트 클립(`.sprite.json`)을 경로별로 나눠 주는 테이블과, 그 테이블을 에셋 캐시 레지스트리에 노출하는 함수입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    class SpriteClipAsset;

    /**
     * @class SpriteClipCache
     * @brief 스프라이트 클립을 경로별로 나눠 주고, 에셋 캐시 레지스트리(`AssetManager`)에 노출합니다. 그러면 핫 리로드, 종료, 진단이 다른 캐시와 같은 경로를 탑니다.
     * @details 테이블은 프로세스에 하나다. 씬 로드 워커와 게임 모듈이 서비스 없이 호출한다(`acquire`). 이 객체는 그 테이블을 레지스트리에 노출하는 진입점일 뿐
     *          상태를 갖지 않는다. 테이블은 약한 참조(`MeshUtil::acquirePrimitive` 와 같은 구조)라 마지막 사용자가 놓으면 사라진다.
     */
    class SW_API SpriteClipCache final : public IAssetCache
    {
    public:
        /**
         * @brief 경로의 클립을 **나눠 받습니다**. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다.
         * @details 같은 클립을 쓰는 스프라이트 백 개가 파일을 백 번 읽지 않습니다. 비동기 씬 로드의 워커에서 불려도 됩니다(잠급니다).
         */
        static shared_ptr<const SpriteClipAsset> acquire( string_view path );

        /**
         * @brief 사용 중인 클립을 파일에서 **제자리로** 다시 읽습니다. 그 클립을 가진 쪽 모두가 새 내용을 봅니다.
         * @details 틱 밖(게임 스레드)에서만 호출한다. 틱 중의 스프라이트가 읽는 내용을 바꾸기 때문이다. 클립에서 계산한 컴포넌트 상태(UV, 아틀라스)는
         *          호출하는 쪽이 `onPropertyChanged` 로 다시 맞추게 한다(에디터 핫 리로드가 그렇게 한다).
         * @return 사용 중이었고 다시 읽었으면 true 를 반환합니다. 아무도 쓰지 않거나(다음 `acquire` 가 새 내용을 읽습니다) 읽을 수 없으면 false 를 반환하며, 이전 내용이 그대로 남습니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path );

        /** @brief 레지스트리의 종류 이름("SpriteClip")입니다. */
        const utf8* getAssetKindName() const override { return "SpriteClip"; }
        /** @brief 그 경로의 클립을 누가 쥐고 있는지 반환합니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 제자리에서 다시 읽습니다(`reloadShared`). 디바이스는 쓰지 않습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 지금 누가 쥔 클립 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 테이블을 비웁니다. 참조를 가진 쪽의 클립은 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;
    };
} // namespace sw
