/**
 * @file EditorTransformCommands.h
 * @brief 컴포넌트 붙여넣기/프리셋 및 다중 선택 정렬·스냅 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 정렬 축 */
    enum class AlignAxis : uint8
    {
        X = 0,
        Y,
        Z
    };

    /** @brief 정렬 기준 */
    enum class AlignType : uint8
    {
        Min = 0,
        Center,
        Max
    };

    /**
     * @class EditorTransformCommands
     * @brief 워크스페이스 클립보드 바이너리/XML을 받아 붙여넣기·정렬을 수행합니다.
     */
    class EditorTransformCommands
    {
    public:
        /** @brief 복사한 컴포넌트 바이너리/XML 값을 대상에 덮어씁니다. */
        static bool pasteComponentValues( Component* pTargetComp, const vector<uint8>& bytes, string_view xmlFallback = {} );
        static bool pasteComponentValues( Component* pTargetComp, string_view xml );
        /** @brief 복사한 타입으로 새 컴포넌트를 붙이고 바이너리/XML을 적용합니다. */
        static Component* pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, const vector<uint8>& bytes, string_view xmlFallback = {} );
        static Component* pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml );
        /** @brief 컴포넌트 프리셋을 Resource 프리셋 폴더에 저장합니다. */
        [[nodiscard]] static bool saveComponentPreset( const Component* pComp, string_view presetName );
        /** @brief 프리셋 XML을 컴포넌트에 적용합니다. */
        [[nodiscard]] static bool loadComponentPreset( Component* pComp, string_view presetFilePath );
        /** @brief 선택 오브젝트를 지면(Y)에 맞춥니다(`snapObjectsToGround`). */
        static void snapSelectedToGround();
        /** @brief 선택 오브젝트를 축 기준으로 정렬합니다(`alignObjects`). */
        static void alignSelectedObjects( AlignAxis axis, AlignType type );
        /** @brief 선택 오브젝트를 축 방향으로 균등 배치합니다(`distributeObjects`). */
        static void distributeSelectedObjects( AlignAxis axis );

        // 본체는 오브젝트 목록을 받는다 — 선택(EditorContext)에 기대지 않아 EditorTest 가 부를 수 있다. 위치는 모두 **월드**로 읽고 쓴다
        // (`SceneComponent::setWorldPosition`). 예전에는 월드 값을 로컬 칸에 쓰거나 월드 축 차이를 로컬 축에 더해, 부모가 돌았거나 커진 오브젝트가
        // 엉뚱한 자리로 갔다.
        /** @brief 오브젝트들의 바닥을 월드 Y = 0 에 맞춥니다(메시는 월드 Y 스케일의 반, 2D 박스는 박스 높이의 반을 바닥까지의 거리로 봅니다). */
        static void snapObjectsToGround( const vector<GameObject*>& listObject );
        /** @brief 오브젝트들의 월드 위치를 축 하나에서 맞춥니다(최소 · 최대 · 가운데). 둘보다 적으면 아무것도 하지 않습니다. */
        static void alignObjects( const vector<GameObject*>& listObject, AlignAxis axis, AlignType type );
        /** @brief 오브젝트들을 월드 축 하나를 따라 양 끝 사이에 같은 간격으로 놓습니다. 셋보다 적으면 아무것도 하지 않습니다. */
        static void distributeObjects( vector<GameObject*> listObject, AlignAxis axis );
    };
} // namespace sw::editor
