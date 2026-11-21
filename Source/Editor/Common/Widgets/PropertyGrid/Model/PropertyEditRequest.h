/**
 * @file PropertyEditRequest.h
 * @brief 인스펙터 프로퍼티 칸 편집 요청입니다 — (프로퍼티 경로, 편집 종류, 새 값). 값만 들어 프레임을 넘길 수 있고, 적용은 대상 인스턴스에 경로를 그때 풀어 한다.
 * @details 그리드 모델(`PropertyGridViewModel`)은 View 가 행 번호로 낸 명령을 이 요청으로 바꾸고, 씬 대상이면 (범위, 오브젝트 id, 컴포넌트 id)와 함께 게임 단계에 넘긴다
 *          (`EditorCommandRegistry::requestSceneEdit`) — UI 프레임은 씬 메모리에 쓰지 않는다. 씬 밖 대상(환경설정 · 문서)은 주인 패널이 수명을 들어 바로 적용한다.
 *          언리얼 `IPropertyHandle::SetValue`(프로퍼티 노드 경로로 대상을 찾아 쓰고 `NotifyPostChange`) · 유니티 `SerializedProperty.propertyPath` 와 같은 단위다.
 *          경로는 깊이 0 프로퍼티 이름과 그 아래 마디(구조체 필드 이름 · 시퀀스 원소 자리 · 맵 항목 자리)다 — 행 번호와 달리 다음 모으기 뒤에도 뜻이 같다.
 *          끌기 중의 쓰기는 프레임마다 요청이고 되돌리기는 편집 구간(`PropertyEditSpanSet`)이 한 단계로 묶는다(언리얼 `InteractiveChange` 뒤 한 트랜잭션과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Reflection/TextAddress.h"

namespace sw
{
    struct TypeInfo;

    class TypeRegistry;
} // namespace sw

namespace sw::editor
{
    /** @brief 경로 마디 하나의 종류입니다(`PropertyRowChild` 와 같은 갈래). */
    enum class PropertyPathStep : uint8
    {
        Field,    ///< 구조체 필드 — `_name`
        Element,  ///< 시퀀스 원소 — `_ordinal` 번째
        MapValue, ///< 맵 항목의 값 — `_ordinal` 번째 항목
    };

    /** @brief 경로 마디 하나입니다. */
    struct PropertyPathNode
    {
        hashed_string    _name{};       ///< `Field` 의 필드 이름
        uint32           _ordinal{ 0 }; ///< `Element` · `MapValue` 의 자리
        PropertyPathStep _step{ PropertyPathStep::Field };
    };

    /** @brief 대상 인스턴스에서 값 자리까지의 경로입니다 — 깊이 0 프로퍼티(`_root`)와 그 아래 마디. 되돌리기 · 알림의 단위는 `_root` 다. */
    struct PropertyEditPath
    {
        hashed_string            _root{};
        vector<PropertyPathNode> _listNode{};
    };

    /** @brief 편집 종류입니다 — 그리드 모델의 쓰기 명령 하나에 하나. */
    enum class PropertyEditKind : uint8
    {
        SetValue,            ///< 내장 값 사본 `_value`(비트필드는 bool)
        SetNumber,           ///< 숫자 `_number` — 허용 범위(`Min` · `Max`)로 자른다
        SetText,             ///< 값 글 `_text`(`SerializerUtil::formatPropertyText` 꼴)
        SetEnum,             ///< 열거형 값 `_integer`
        AddElement,          ///< 시퀀스 끝에 기본값 원소
        ClearElements,       ///< 시퀀스 비우기
        RemoveMapEntry,      ///< 맵의 `_ordinal` 번째 항목 지우기
        AddMapEntry,         ///< 키 글 `_text` 로 기본값 항목 더하기(같은 키가 있으면 아무것도 하지 않는다)
        ReplaceKeyedElement, ///< 원소가 곧 키인 시퀀스의 `_ordinal` 번째 원소를 글 `_text` 로
        RemoveKeyedElement,  ///< 원소가 곧 키인 시퀀스의 `_ordinal` 번째 원소 지우기
        AddKeyedElement,     ///< 원소가 곧 키인 시퀀스에 글 `_text` 의 원소 더하기
    };

    /**
     * @struct PropertyEditRequest
     * @brief 프로퍼티 편집 하나입니다(값만). `applyTo` 가 대상 인스턴스에 경로를 풀고 편집을 적용한다 — 알림(`onPropertyChanged`)은 부르는 쪽이 `_path._root` 로 한다.
     */
    struct SW_EDITOR_API PropertyEditRequest
    {
        PropertyEditPath _path{};
        TaskValue        _value{};
        string           _text{};
        float64          _number{ 0.0 };
        int64            _integer{ 0 };
        uint32           _ordinal{ 0 };
        PropertyEditKind _kind{ PropertyEditKind::SetValue };

        /**
         * @brief 인스턴스 @p root(타입 @p type)에서 경로를 풀어 편집을 적용합니다. 경로가 풀리지 않거나(그 사이 원소가 사라졌다) 값을 쓰지 못하면 false 입니다.
         * @details 컨테이너 편집은 컨테이너가 풀리면 true 다(빈 시퀀스 비우기처럼 바뀐 것이 없어도 — 예전 쓰기 길과 같다).
         */
        [[nodiscard]] bool applyTo( MutableObjectAddress root, const TypeInfo& type, const TypeRegistry& registry ) const;
    };
} // namespace sw::editor
