/**
 * @file EditorSceneSettingsCommands.h
 * @brief 씬 설정(`SceneSettings` — 스트리밍 격자 · 데이터 레이어)을 되돌리기와 함께 바꾸는 커맨드입니다.
 * @details 설정은 씬 파일에 적히는 문서 데이터라 되돌리기 · 씬 dirty 를 거친다. 단계는 설정 전체의 XML(엔진 직렬화기)을 데이터로 들어 모듈 리로드를 넘긴다 —
 *          칸마다 단계를 두지 않으므로 설정에 칸이 늘어도 이 한 길이다(언리얼 `AWorldSettings` 편집이 트랜잭션을 거치고, 유니티 Lighting · 씬 설정이
 *          `Undo.RecordObject` 를 거치는 것과 같다). 플레이 중에는 거절한다 — 플레이 사본을 고쳐도 Stop 이 버려 저장되지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Scene;
    struct SceneSettings;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorSceneSettingsCommands
     * @brief 씬 설정 편집의 한 길입니다. 패널(데이터 레이어 · 스트리밍 셀)은 고친 사본을 `apply` 에 넘긴다.
     */
    class EditorSceneSettingsCommands
    {
    public:
        /**
         * @brief @p scene 의 설정을 @p settings 로 바꾸고 되돌리기 한 단계와 씬 dirty 를 남깁니다.
         * @return 바꿨으면 true 입니다. 지금 설정과 같으면 아무것도 하지 않고 false, 플레이 중이면 거절(로그)하고 false,
         *         @p settings 가 쓸 수 없으면(`SceneSettings::validate` 가 오류를 남긴다) 바꾸지 않고 false 입니다.
         */
        [[nodiscard]] static bool apply( Scene& scene, const SceneSettings& settings, string_view undoLabel );
        /** @brief 설정을 단계에 담을 XML 로 씁니다(씬 파일의 `<SceneSettings>` 와 같은 직렬화기). */
        [[nodiscard]] static string writeSettingsXML( const SceneSettings& settings );
        /**
         * @brief @p settingsXML 을 읽어 @p scene 의 설정으로 둡니다. 되돌리기 단계의 몸이다.
         * @return 읽지 못하거나 쓸 수 없는 설정이면 오류를 남기고 바꾸지 않은 채 false 입니다.
         */
        [[nodiscard]] static bool restoreSettingsXML( Scene& scene, string_view settingsXML );
        /** @brief 되돌리기 · 다시 하기 단계의 처리기입니다(`SW_UNDO_STEP`). 활성 씬에 @p settingsXML 을 두고 씬을 dirty 로 표시합니다. */
        static void restoreActiveSceneSettings( string settingsXML );
    };
} // namespace sw::editor
