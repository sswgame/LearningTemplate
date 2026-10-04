/**
 * @file SpriteClipCache.h
 * @brief 스프라이트 클립(`.sprite.json`)을 경로로 나눠 주는 표와, 그 표를 에셋 캐시 등록부에 보이는 창구입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    class SpriteClipAsset;

    /**
     * @class SpriteClipCache
     * @brief 스프라이트 클립을 경로로 나눠 주고, 에셋 캐시 등록부(`AssetManager`)에 보여 핫 리로드 · 종료 · 진단이 다른 캐시와 같은 길을 탑니다.
     * @details 표는 프로세스에 하나입니다 — 씬 로드 워커와 게임 모듈이 서비스 없이 부릅니다(`acquire`). 이 객체는 그 표를 등록부에 보이는 창구일 뿐
     *          상태를 갖지 않습니다. 표는 약한 참조(`MeshUtil::acquirePrimitive` 와 같은 모양)라 마지막 사용자가 놓으면 사라집니다.
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
         * @details 틱 밖(게임 스레드)에서만 부릅니다 — 틱 중의 스프라이트가 읽는 내용을 바꿉니다. 클립에서 계산한 컴포넌트 상태(UV · 아틀라스)는
         *          부르는 쪽이 `onPropertyChanged` 로 다시 맞추게 합니다(에디터 핫 리로드가 한다).
         * @return 사용 중이었고 다시 읽었으면 true. 아무도 쓰지 않으면(다음 `acquire` 가 새 내용을 읽는다) · 읽을 수 없으면 false(옛 내용 그대로)입니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path );

        /** @brief 등록부의 종류 이름("SpriteClip")입니다. */
        const utf8* getAssetKindName() const override { return "SpriteClip"; }
        /** @brief 그 경로의 클립을 누가 쥐고 있는지 반환합니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 제자리로 다시 읽습니다(`reloadShared`). 디바이스는 쓰지 않습니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 지금 누가 쥔 클립 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 표를 비웁니다. 쥔 쪽의 클립은 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;
    };
} // namespace sw
