/**
 * @file MeshCache.h
 * @brief 메시 에셋(`.mesh`)을 경로로 나눠 주는 표와, 그 표를 에셋 캐시 등록부에 보이는 창구입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/Cache/IAssetCache.h"

namespace sw
{
    class Mesh;

    /**
     * @class MeshCache
     * @brief 메시 에셋을 경로로 나눠 주고, 에셋 캐시 등록부(`AssetManager`)에 보여 핫 리로드 · 종료 · 진단이 다른 캐시와 같은 길을 탑니다.
     * @details 표는 프로세스에 하나이고(Engine.dll 안) 약한 참조입니다 — `MeshUtil::acquirePrimitive` · `SpriteClipCache` 와 같은 모양이라
     *          마지막 사용자가 놓으면 메시도 사라지고, 디바이스가 내려갈 때 이 표가 붙든 GPU 자원이 없습니다. 같은 경로는 같은 `Mesh` 라서
     *          배치 키(메시 포인터)가 갈리지 않습니다. 씬 로드 워커에서 불려도 됩니다(잠급니다).
     */
    class SW_API MeshCache final : public IAssetCache
    {
    public:
        /**
         * @brief 경로의 메시를 **나눠 받습니다**. 처음이면 읽고, 읽을 수 없으면 nullptr 입니다.
         * @details 읽지 못한 경로는 경로마다 한 번만 경고합니다(같은 메시를 쓰는 컴포넌트 수백 개가 같은 줄을 찍지 않게). 실패는 기억하지
         *          않으므로 파일이 생기면 다음 요청이 읽습니다.
         */
        static shared_ptr<Mesh> acquire( string_view path );

        /**
         * @brief 사용 중인 메시를 파일에서 **제자리로** 다시 읽습니다. 그 메시를 가진 컴포넌트 모두가 새 정점을 봅니다.
         * @param pDevice 살아 있는 디바이스면 정점을 바꾸기 전에 `waitIdle` 합니다 — 지난 프레임이 아직 옛 정점 버퍼로 그리고 있을 수 있습니다.
         * @details 틱 밖(게임 스레드)에서만 부릅니다. `Mesh::setVertices` 가 내용 번호를 바꾸므로 정점 풀 · 업로드 큐가 다시 올립니다.
         * @return 사용 중이었고 다시 읽었으면 true. 아무도 쓰지 않거나 읽을 수 없으면(옛 정점 그대로) false 입니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path, IRHIDevice* pDevice );

        /** @brief 등록부의 종류 이름("Mesh")입니다. 에디터 에셋 종류 표가 이 이름으로 핫 리로드를 보냅니다. */
        const utf8* getAssetKindName() const override { return "Mesh"; }
        /** @brief 그 경로의 메시를 누가 쥐고 있는지 반환합니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 제자리로 다시 읽습니다(`reloadShared`). */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 지금 누가 쥔 메시 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 표와 경고 기록을 비웁니다. 쥔 쪽의 메시는 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;
    };
} // namespace sw
