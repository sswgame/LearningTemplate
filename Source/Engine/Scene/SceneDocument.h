/**
 * @file SceneDocument.h
 * @brief 씬 문서 모델과 XML/바이너리 직렬화입니다(씬 이름, 엔티티 노드 목록, 프리팹 · 임베디드 상태).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // SceneDocument: 씬 메타데이터와 엔티티 노드 목록
    // ------------------------------------------------------------------------------
    struct SceneDocument
    {
        /** @brief 씬 문서 안의 엔티티 하나입니다(프리팹 참조 또는 임베디드 GameObject 상태). */
        struct EntityNode
        {
            /**
             * @brief 이 파일 안에서 엔티티를 가리키는 id 입니다(유니티의 fileID 자리). 0 이면 id 가 없는 옛 문서입니다.
             * @details 다른 엔티티의 부착(`SceneComponent::_attachOwnerId`)이 이 값으로 부모를 가리킵니다. 런타임 오브젝트 id 와 다른 공간입니다 —
             *          씬이 런타임 id ↔ 파일 id 표를 들고 저장할 때마다 같은 값을 다시 씁니다(저장할 때마다 파일이 흔들리지 않게).
             */
            uint64 _fileId{ 0 };
            string _name;
            string _prefab;
            string _prefabGuid;
            string _embeddedXml;
            /**
             * @brief 쿠킹된 리플렉션 바이너리 상태입니다. **비어 있지 않으면 XML 대신 이것을 씁니다.**
             *
             * XML 은 사람이 고치는 원본이고 이것은 그것을 구운 것입니다. 둘 다 실리는 일은 없습니다.
             * 쿠커가 왕복 검증에 성공한 엔티티만 이쪽에 담고, 실패하면 XML 을 그대로 남깁니다.
             */
            vector<uint8> _embeddedStateBytes;
        };

        string             _name;
        string             _sourcePath;
        vector<EntityNode> _listEntityNode;
        bool               _bValid{ false };

        /** @brief 빌드(Shipping/Dev)와 파일 존재 여부에 따라 알맞은 포맷(바이너리 우선)으로 로드합니다. */
        [[nodiscard]] SW_API bool load( string_view path );
        /** @brief 리소스 상대/절대 경로에서 XML 을 로드합니다. */
        [[nodiscard]] SW_API bool loadXml( string_view path );
        /** @brief XML 을 리소스 상대/절대 경로에 저장합니다. */
        [[nodiscard]] SW_API bool saveXml( string_view path ) const;
        /** @brief 리소스 상대/절대 경로에서 바이너리(SCN1)를 로드합니다. */
        [[nodiscard]] SW_API bool loadBinary( string_view path );
        /** @brief 바이너리(SCN1)를 리소스 상대/절대 경로에 저장합니다. */
        [[nodiscard]] SW_API bool saveBinary( string_view path ) const;

        /** @brief id 가 없는 엔티티(옛 문서)에 이 문서 안에서 겹치지 않는 id 를 줍니다. 쿠커가 굽기 전에 부릅니다. */
        SW_API void assignMissingFileIds();
        /** @brief 이 문서의 엔티티 id 중 가장 큰 것입니다. 없으면 0 입니다. */
        SW_API uint64 getMaxFileId() const;
    };

} // namespace sw
