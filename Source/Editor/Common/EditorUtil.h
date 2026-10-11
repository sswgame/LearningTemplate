/**
 * @file EditorUtil.h
 * @brief EditorModule 유틸리티입니다(프리팹 스폰, 씬 편집 허용 여부, 파이썬 실행기 등). 경로는 EditorPaths. UI 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 에디터 설정 경로 해석과 공통 유틸리티입니다. ImGui 에 의존하지 않습니다(폰트는 GUI/EditorFontSetup). */
    class EditorUtil
    {
    public:
        /** @brief 저장소 파이썬 스크립트를 띄우는 실행기입니다. 저장소 스크립트가 쓰는 것과 같다(Windows 는 런처 `py -3`). */
#if defined( SW_PLATFORM_WINDOWS )
        static constexpr const utf8* kPythonCommand = "py -3";
#else
        static constexpr const utf8* kPythonCommand = "python3";
#endif

        // ------------------------------------------------------------------------------
        // 프리팹 스폰 · 편집 허용
        //    애셋 종류 판별은 EditorAssetTypeRegistry::matches 가 정본입니다
        // ------------------------------------------------------------------------------
        /** @brief 프리팹을 스폰합니다(부모가 있으면 그 아래에). 실패하면 로그를 남기고 nullptr 을 반환합니다. */
        static GameObject* spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent = nullptr );

        /** @brief 플레이 세션이 정지(Stop) 상태일 때만 씬 오브젝트를 편집할 수 있습니다. */
        static bool areSceneEditsAllowed();

        /**
         * @brief 계층 라벨에 붙일 `[Category]` 뱃지를 덧붙입니다.
         * @param category 컴포넌트 타입의 리플렉션 Category (`TypeInfo::getCategory`).
         * @param inoutBadge 누적 중인 뱃지 문자열. 비어 있지 않으면 앞에 공백이 붙습니다.
         * @details 타입 이름이 아니라 Category 로 고르므로 게임이 넣은 컴포넌트도 뱃지가 붙고, 엔진이 컴포넌트를 늘려도 패널을
         *          고칠 필요가 없습니다("컴포넌트 추가" 메뉴와 같은 기준). 같은 Category 는 한 번만 넣습니다.
         */
        static void appendCategoryBadge( string_view category, string& inoutBadge );
    };
} // namespace sw::editor
