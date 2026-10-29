/**
 * @file EditorAssetTypeActions.h
 * @brief 애셋 종류별 에디터 동작(썸네일, 열기, 뷰포트 드롭)의 인터페이스입니다. 종류 등록(`EditorAssetTypeRegistration::_pActions`)이 가리킵니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

struct ImDrawList;

namespace sw
{
    struct float2;
    struct float3;

    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class IEditorAssetTypeActions
     * @brief 애셋 종류 하나가 에디터에서 하는 일을 정의합니다. 언리얼의 `FAssetTypeActions` 에 해당합니다.
     * @details 보여 주는 정보(이름, 아이콘, 색)는 종류 등록(`EditorAssetTypeRegistration`)에 있고, 여기에는 동작만 둔다. 종류의 코드 파일이
     *          동작 객체를 파일 범위에 두고 그 종류의 등록 줄(`SW_EDITOR_ASSET_TYPE`)이 가리키므로, 콘텐츠 브라우저와 `EditorAssetCommands` 에는 종류별 분기가 없다.
     *          동작 레지스트리를 따로 두지 않는다 — 종류와 동작이 두 목록이면 한쪽만 등록되거나 열쇠가 어긋난다.
     *          플러그인 모듈도 파생한다(그래서 내보낸다). 모든 동작은 선택이다. false 를 반환하면 호출하는 쪽이 일반 동작(일반 문서 썸네일, 열기)으로 넘어간다.
     */
    class SW_EDITOR_API IEditorAssetTypeActions
    {
    public:
        virtual ~IEditorAssetTypeActions() = default;

        /** @brief 콘텐츠 브라우저 카드 썸네일을 그립니다. 배경과 테두리는 브라우저가 그립니다. 그리지 않으면 false 입니다. */
        virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const;
        /**
         * @brief 썸네일이 에셋 자신의 이미지이면 true 입니다. 텍스처가 해당합니다. 브라우저가 썸네일 캐시로 그 이미지를 읽어 그립니다.
         * @details 이미지가 아직 없거나 읽지 못하면 `drawThumbnail` 로 넘어간다. 언리얼의 텍스처 썸네일과 같다.
         */
        virtual bool hasImagePreview() const { return false; }
        /** @brief 전용 도구 패널이 없는 종류를 엽니다. 이 종류가 열기를 하지 않거나 열지 못했으면 false 입니다. */
        [[nodiscard]] virtual bool open( string_view relativePath ) const;
        /** @brief 뷰포트에 드롭한 것을 처리합니다. 스폰이나 로드를 합니다. 처리하지 않으면 false 이고, 호출하는 쪽이 열기로 넘어갑니다. */
        [[nodiscard]] virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const;
    };
} // namespace sw::editor
