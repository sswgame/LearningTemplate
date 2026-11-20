/**
 * @file PropertyEditStep.h
 * @brief 인스펙터 프로퍼티 편집의 되돌리기 단위입니다 — (오브젝트 id, 컴포넌트 지역 id, 프로퍼티 경로, 값 글). 되돌리기 · 다시 하기가 같은 데이터 단계(`PropertyEditTarget::applyPropertyText`)를 탄다.
 * @details 유니티 `Undo.RecordObject` 의 `PropertyModification`(대상 · `propertyPath` · 값), Godot `add_do_property` / `add_undo_property` 와 같은 단위다.
 *          예전에는 위젯이 활성화되면 선택 전체를 직렬화했다(8000 선택에서 숫자 한 칸이 XML 16000 개, 감사 E-7).
 *          컴포넌트는 영속 지역 id(`ComponentStableKey::getLocalID` — 프리팹 오버라이드 키 `#<16진>` 의 값)이고 0 이면 오브젝트 자신이다(지역 id 는 0 이 아니다).
 *          프로퍼티 경로는 깊이 0 프로퍼티 이름이다 — 중첩 필드 · 원소 편집도 다중 선택은 그 프로퍼티를 통째로 입히므로(`PropertyGridViewModel` 의 편집 요청 적용) 단위가 같다.
 *          값 글은 `SerializerUtil::formatPropertyText` 꼴이다(`PropertyGridViewModel::setPropertyText` 와 같다).
 *          단계 하나는 경로 하나와 대상 목록이고 값 글은 겹치지 않게 한 벌만 든다 — 다중 선택은 대개 값이 하나라 8000 선택의 거래도 대상 수 × 20 바이트 남짓이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Component;
    class GameObject;
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    struct UndoStepContext;

    /**
     * @struct PropertyEditTarget
     * @brief 편집 대상 하나 — 오브젝트 id 와 컴포넌트 지역 id(0 이면 오브젝트 자신)입니다. 단순 복사 형이라 되돌리기 인자로 바이트 그대로 간다.
     */
    struct PropertyEditTarget
    {
        uint64 _objectID{ 0 };
        uint64 _componentLocalID{ 0 };

        /** @brief 컴포넌트 @p pComponent(nullptr 이면 오브젝트 자신)의 대상입니다. */
        static PropertyEditTarget make( const GameObject& owner, const Component* pComponent );
        /**
         * @brief 되돌리기 · 다시 하기가 함께 쓰는 데이터 단계입니다 — 씬 @p sceneKey 의 대상마다 프로퍼티 @p propertyPath 에 값 글(@p listText 의 `listTextIndex[i]` 번째)을 쓰고
         *        모델의 쓰기 길대로 알립니다(`onPropertyChanged` · 맵 검사).
         * @details 대상을 못 찾으면 그 대상만 건너뛴다(언리얼 트랜잭션 · `ObjectSnapshot::restore` 와 같다). 단계를 실행하는 스택에 오브젝트 편집을 알린다(선택 · dirty).
         */
        static void applyPropertyText( UndoStepContext& context, uint32 sceneKey, string propertyPath, vector<PropertyEditTarget> listTarget, vector<string> listText,
                                       vector<uint32> listTextIndex );
        /** @brief @p object 가 사는 씬의 키입니다(활성 씬이면 0, 미리보기 씬이면 그 키 — `ObjectSnapshot` 과 같은 규칙). 씬 관리자가 없으면 0 입니다. */
        static uint32 findSceneKey( const GameObject& object );
        /** @brief 씬 @p sceneKey 의 오브젝트 @p objectID 입니다. 없거나 지우는 중이면 nullptr 입니다. */
        static GameObject* findObject( uint32 sceneKey, uint64 objectID );

        /** @brief 오브젝트 @p object 에서 이 대상의 인스턴스(지역 id 의 컴포넌트, 0 이면 오브젝트 자신)와 그 타입입니다. 컴포넌트가 없으면 nullptr 입니다. */
        [[nodiscard]] void* findInstance( GameObject& object, Component*& outComponent, const TypeInfo*& outType ) const;
        /** @brief 오브젝트 @p object 의 이 대상의 프로퍼티 @p propertyPath 에 값 글 @p valueText 를 쓰고 알립니다. 컴포넌트 · 프로퍼티가 없거나 글을 읽지 못하면 false 입니다. */
        [[nodiscard]] bool applyTo( GameObject& object, string_view propertyPath, string_view valueText ) const;
        /** @brief 오브젝트 @p object 의 이 대상의 프로퍼티 @p propertyPath 의 지금 값 글입니다. 컴포넌트 · 프로퍼티가 없으면 false 입니다. */
        [[nodiscard]] bool readValueText( GameObject& object, string_view propertyPath, string& outText ) const;
    };
} // namespace sw::editor
