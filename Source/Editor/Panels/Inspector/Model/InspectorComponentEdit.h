/**
 * @file InspectorComponentEdit.h
 * @brief 기본 컴포넌트 인스펙터 확장(카메라 · 트랜스폼 · 태그 · 스프라이트)의 edit model — 컴포넌트에서 값을 모으고(`gather*`) 고친 값을 넣습니다(`apply*`). ImGui 를 모릅니다.
 * @details 확장 View(`*ComponentInspector.cpp`)는 컴포넌트 헤더를 보지 않고 이 값만 그린다(`docs/agents/EditorUI.md` MVVM 계약). 확장이 받는 `Component*` 는 인스펙터 틀이 넘긴 것을 그대로 여기로 돌려준다.
 *          되돌리기 · dirty 는 인스펙터 틀이 확장 영역을 위젯 하나로 묶어 남긴다(`InspectorPropertyUndo::trackLastItem`) — 여기서 따로 남기지 않는다(언리얼 `IDetailCustomization` 의 프로퍼티 핸들과 같은 선).
 *          등록(`SW_EDITOR_INSPECTOR_FOR_TYPE`)에 쓰는 형 정보도 여기서 준다(형 정보 함수가 컴포넌트 헤더를 요구한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct TypeInfo;

    class Component;
} // namespace sw

namespace sw::editor
{
    /** @brief 카메라 투영 구역의 값입니다(값만). */
    struct CameraSectionValues
    {
        float32 _fovYDegrees{ 0.0f };
        float32 _nearPlane{ 0.0f };
        float32 _farPlane{ 0.0f };
        float32 _orthoHeight{ 0.0f };
        uint8   _bOrthographic{ SW_FALSE };
    };

    /** @brief 트랜스폼 구역의 값입니다(값만). 회전은 도로 보이고 고친다(언리얼 FRotator · 유니티 localEulerAngles). */
    struct TransformSectionValues
    {
        float3 _localPosition{};
        float3 _localRotationDegrees{};
        float3 _localScale{};
        float3 _worldPosition{}; ///< 읽기 전용
    };

    /** @brief 기본 컴포넌트 인스펙터 확장의 edit model 입니다. */
    struct InspectorComponentEdit
    {
        /** @brief 등록용 형 정보입니다(`SW_EDITOR_INSPECTOR_FOR_TYPE`). */
        static const TypeInfo* getCameraComponentType();
        static const TypeInfo* getSceneComponentType();
        static const TypeInfo* getTagComponentType();
        static const TypeInfo* getSpriteComponentType();

        /** @brief 카메라 컴포넌트 @p component 의 투영 값을 모읍니다. */
        static void gatherCamera( const Component& component, CameraSectionValues& outValues );
        /** @brief 고친 투영 값을 넣습니다(직교 높이는 직교일 때만 넣는다). */
        static void applyCamera( Component& component, const CameraSectionValues& values );

        /** @brief 씬 컴포넌트 @p component 의 트랜스폼 값을 모읍니다. */
        static void gatherTransform( const Component& component, TransformSectionValues& outValues );
        static void setLocalPosition( Component& component, const float3& position );
        static void setLocalRotationDegrees( Component& component, const float3& rotationDegrees );
        static void setLocalScale( Component& component, const float3& scale );

        /** @brief 태그 컴포넌트 @p component 의 비지 않은 태그 이름을 모읍니다(@p outListTag 를 비우고 채운다). */
        static void collectTags( const Component& component, vector<string>& outListTag );

        /** @brief 스프라이트 클립 도구를 엽니다(작업 공간이 다음 프레임에 연다). */
        static void openSpriteClipTool();
    };
} // namespace sw::editor
