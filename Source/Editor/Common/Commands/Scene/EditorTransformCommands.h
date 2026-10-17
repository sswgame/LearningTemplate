/**
 * @file EditorTransformCommands.h
 * @brief 컴포넌트 붙여 넣기와 프리셋, 다중 선택의 정렬과 스냅 커맨드를 제공합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    class EditorWorkspace;

    /** @brief 정렬 축입니다. */
    enum class AlignAxis : uint8
    {
        X = 0,
        Y,
        Z
    };

    /** @brief 정렬 기준입니다. */
    enum class AlignType : uint8
    {
        Min = 0,
        Center,
        Max
    };

    /**
     * @class EditorTransformCommands
     * @brief 워크스페이스 클립보드의 컴포넌트 값(XML — 프리셋 파일과 같은 형식 하나)을 붙여 넣고 정렬을 수행합니다.
     */
    class EditorTransformCommands
    {
    public:
        /** @brief 복사한 컴포넌트의 값(XML)을 대상에 덮어씁니다. 이름(컴포넌트 키)은 대상의 것을 지킵니다. 읽지 못하면 false 이고 대상은 그대로입니다. */
        static bool pasteComponentValues( Component* pTargetComp, string_view xml );
        /** @brief 작업 공간 클립보드(`EditorWorkspace::copyComponent`)의 값을 @p pTargetComp 에 붙여 넣습니다. */
        static bool pasteCopiedComponentValues( const EditorWorkspace& workspace, Component* pTargetComp );
        /** @brief 복사한 타입으로 새 컴포넌트를 붙이고 값(XML)을 적용합니다. 값을 읽지 못하면 경고하고 기본값 컴포넌트로 둡니다. */
        static Component* pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml );
        /** @brief 작업 공간 클립보드의 컴포넌트를 @p pTargetObj 에 새로 붙입니다. */
        static Component* pasteCopiedComponentAsNew( const EditorWorkspace& workspace, GameObject* pTargetObj );
        /**
         * @brief 이름으로 저장하는 프리셋의 파일 이름을 만듭니다. 프리셋 폴더 안의 `<타입>_<이름>.preset.xml` 입니다. 인스펙터의 프리셋 목록이 이 규칙으로 찾습니다.
         * @details 저장하는 쪽과 목록을 읽는 쪽이 이 함수 하나를 쓴다.
         */
        static string makeComponentPresetFileName( const Component* pComp, string_view presetName );
        /** @brief `makeComponentPresetFileName` 규칙을 따른 이 컴포넌트 타입의 프리셋이면 그 이름을, 아니면 빈 문자열을 반환합니다. 경로를 받아도 됩니다. */
        static string getComponentPresetName( const Component* pComp, string_view presetFilePath );
        /** @brief 컴포넌트 프리셋을 이름으로 프리셋 폴더에 저장합니다(`makeComponentPresetFileName`). 폴더를 찾지 못하면 false 입니다. */
        [[nodiscard]] static bool saveComponentPreset( const Component* pComp, string_view presetName );
        /**
         * @brief 컴포넌트 프리셋을 선택한 파일에 그대로 저장합니다. `.preset.xml` 이 없으면 붙입니다.
         * @details "Save Component Preset" 대화상자가 쓴다. 선택한 파일 이름을 이름 규칙(`makeComponentPresetFileName`)에 넘기면
         *          선택한 폴더는 버려지고 `MyPreset.preset.xml` 이 프리셋 폴더의 `<타입>_MyPreset.preset.preset.xml` 이 되므로 주의한다.
         */
        [[nodiscard]] static bool saveComponentPresetTo( const Component* pComp, string_view filePath );
        /** @brief 프리셋 XML 을 컴포넌트에 적용합니다. */
        [[nodiscard]] static bool loadComponentPreset( Component* pComp, string_view presetFilePath );
        /** @brief 선택한 오브젝트를 지면(Y)에 맞춥니다(`snapObjectsToGround`). */
        static void snapSelectedToGround();
        /** @brief 선택한 오브젝트를 축 기준으로 정렬합니다(`alignObjects`). */
        static void alignSelectedObjects( AlignAxis axis, AlignType type );
        /** @brief 선택한 오브젝트를 축 방향으로 균등하게 배치합니다(`distributeObjects`). */
        static void distributeSelectedObjects( AlignAxis axis );

        // 본체는 오브젝트 목록을 받는다. 선택(EditorContext)에 의존하지 않으므로 EditorTest 가 호출할 수 있다. 위치는 모두 월드로 읽고 쓴다
        // (`SceneComponent::setWorldPosition`). 월드 값을 로컬 값에 쓰거나 월드 축 차이를 로컬 축에 더하면 부모가 회전했거나 커진 오브젝트가
        // 엉뚱한 위치로 간다.
        /** @brief 오브젝트들의 바닥을 월드 Y = 0 에 맞춥니다. 메시는 월드 Y 스케일의 절반을, 2D 박스는 박스 높이의 절반을 바닥까지의 거리로 봅니다. */
        static void snapObjectsToGround( const vector<GameObject*>& listObject );
        /** @brief 오브젝트들의 월드 위치를 축 하나에서 맞춥니다. 기준은 최솟값, 최댓값, 가운데 중 하나입니다. 둘보다 적으면 아무것도 하지 않습니다. */
        static void alignObjects( const vector<GameObject*>& listObject, AlignAxis axis, AlignType type );
        /** @brief 오브젝트들을 월드 축 하나를 따라 양 끝 사이에 같은 간격으로 배치합니다. 셋보다 적으면 아무것도 하지 않습니다. */
        static void distributeObjects( vector<GameObject*> listObject, AlignAxis axis );
    };
} // namespace sw::editor
