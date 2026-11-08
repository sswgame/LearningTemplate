/**
 * @file EditorAssetTypeActions.h
 * @brief 애셋 종류별 에디터 동작(썸네일, 열기, 뷰포트 배치)의 인터페이스입니다. 종류 등록(`EditorAssetTypeRegistration::_pActions`)이 가리킵니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

struct ImDrawList;

namespace sw
{
    struct float2;

    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    struct EditorPlacementPose;
} // namespace sw::editor

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
        /**
         * @brief 씬 뷰에 놓으면 그 자리에 만드는 종류면 true 입니다(프리팹 · 메시 · 텍스처). false 인 종류는 놓을 때 연다 — 뷰포트 고스트가 놓기 전에 그렇다고 알린다.
         * @details 언리얼의 `UActorFactory` 가 있는 에셋만 뷰포트에 놓이고 나머지는 놓을 수 없다고 표시하는 것과 같다. 위치를 몰라 원점에 놓는 일은 없다.
         */
        [[nodiscard]] virtual bool canPlaceInViewport() const { return false; }
        /**
         * @brief @p pose(자리 · 놓기 전에 고른 회전)로 이 종류의 오브젝트를 만들고 되돌리기 · 선택을 기록합니다. `canPlaceInViewport` 가 true 인 종류만 불린다. 자리는 부른 쪽이 표면에서 구한다.
         * @details 기본 구현은 오류를 남긴다 — 놓을 수 있다고 하고 구현하지 않은 종류가 조용히 아무것도 하지 않게 두지 않는다.
         */
        virtual void placeInViewport( GameObjectManager* pManager, const utf8* pPath, const EditorPlacementPose& pose ) const;
    };
} // namespace sw::editor
