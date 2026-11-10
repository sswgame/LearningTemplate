/**
 * @file PropertyGridViewState.h
 * @brief 프로퍼티 그리드가 한 대상을 그릴 값입니다 — 보이는 프로퍼티마다 값 행 하나(`PropertyValueRow`), 화면 밖 행은 높이만.
 * @details `PropertyGridModel::gather` 가 채우고 View(`EditorPropertyGrid` · 위젯 · 그리기 확장)가 읽는다. 포인터 · 참조 · 인스턴스 주소를 두지 않는다(`docs/agents/EditorUI.md` MVVM 절).
 *          편집은 행 번호(`_rowID`)로 모델 명령을 부른다 — 주소는 모델의 행 표가 든다. 언리얼 Details 의 `FDetailTreeNode`(행) + `IPropertyHandle`(행 번호로 쓰기) 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Panels/Inspector/Model/InspectorBuiltinValue.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentPicker.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

namespace sw::editor
{
    /** @brief 값 행이 그리는 모양입니다. 그리기 확장(`IInspectorProperty`)이 걸린 타입은 확장이 먼저 그리고, 확장이 맡지 않으면 이 종류대로 그립니다. */
    enum class PropertyRowKind : uint8
    {
        Category,      ///< 카테고리 머리(접고 펴는 머리 하나 — 아래 행은 그 표에 든다)
        Skipped,       ///< 화면 밖이라 값을 채우지 않은 행 — 높이(`_height`)만 차지한다
        Builtin,       ///< 내장 값(`InspectorValueWidget`) — 값 사본은 `_value`
        BitField,      ///< 비트필드 체크박스 — 값 사본은 `_value`(bool)
        Enum,          ///< 하나만 고르는 열거형 — `_listChoice` · `_enumValue`
        BitFlagEnum,   ///< 비트 플래그 열거형 — `_listChoice` · `_enumValue`
        Sequence,      ///< 제자리에서 고치는 시퀀스 — 펼치면 원소 행이 자식으로 온다
        KeyedSequence, ///< 원소가 곧 키인 시퀀스(`set`) — 펼치면 `KeyedElement` 행이 자식으로 온다
        Map,           ///< 맵 — 펼치면 값 행이 자식으로 온다(`PropertyRowChild::MapEntry`)
        Struct,        ///< 중첩 구조체 — 펼치면 필드 행이 자식으로 온다
        KeyedElement,  ///< `KeyedSequence` 의 원소 하나 — 글 입력란(`_valueText`)
        Text,          ///< 그리기 확장이 맡는 타입의 값 글(`_valueText`) — 확장이 맡지 않으면 읽기 전용 글로 보인다
        Unreadable,    ///< 값을 읽을 수 없거나 그릴 위젯이 없다 — `_valueText` 가 까닭이다
    };

    /** @brief 자식 행(깊이 1 이상)이 부모 칸 안에서 놓이는 모양입니다. 깊이 0 행은 `None`(표의 한 줄)입니다. */
    enum class PropertyRowChild : uint8
    {
        None,     ///< 표의 한 줄(이름 칸 + 값 칸)
        Field,    ///< 구조체 필드: 점 · 이름 글 뒤에 위젯
        Element,  ///< 시퀀스 원소: `[i]` 뒤에 위젯
        MapEntry, ///< 맵 항목: 지우기 단추 · 키 글 뒤에 값 위젯
    };

    /** @brief 열거형 선택지 하나입니다. */
    struct PropertyEnumChoice
    {
        hashed_string _name{};
        int64         _value{ 0 };
    };

    /**
     * @struct PropertyValueRow
     * @brief 프로퍼티 하나(또는 카테고리 머리 · 자식 원소)의 값 행입니다. 값만 든다 — 위젯은 이것을 그리고 편집을 `_rowID` 로 모델 명령에 돌려준다.
     * @details 행은 그리드가 프레임마다 다시 쓴다(`PropertyGridViewState::_listRow` 의 저장 공간을 다시 쓴다). `_rowID` 는 그 프레임의 모델 행 표 자리다.
     */
    struct PropertyValueRow
    {
        string                     _label{};                ///< 표시 이름(카테고리 머리는 카테고리 이름, 시퀀스 원소는 `[i]`, 맵 항목은 키 글)
        string                     _tooltip{};              ///< 이름 · 위젯의 툴팁
        string                     _valueText{};            ///< 읽기 전용 · 혼합 · 확장 타입의 값 글, 노드 머리 글(`[T] (n elements)`), 열거형 미리보기, 선택기 미리보기
        string                     _markName{};             ///< 자체 테스트 표식의 꼬리(`<타입>.<프로퍼티>`) — 깊이 0 행만
        string                     _favoriteKey{};          ///< 즐겨찾기 키(`InspectorFavorites::makePropertyKey`) — 깊이 0 행만
        TaskValue                  _value{};                ///< `Builtin` · `BitField` 의 값 사본(그 C++ 타입 — `_builtinIndex` 표 줄)
        vector<PropertyEnumChoice> _listChoice{};           ///< 열거형 선택지
        InspectorNumericRange      _range{};                ///< 숫자의 위젯 범위(저장 단위)
        hashed_string              _typeName{};             ///< 프로퍼티 타입 이름(그리기 확장을 찾는 키)
        hashed_string              _propName{};             ///< 프로퍼티 이름(자체 테스트 표식 · ImGui id)
        hashed_string              _handleTargetTypeName{}; ///< 타입 핸들(`TComponentHandle<T>`)의 대상 타입 이름. 비면 타입 없는 핸들이다
        int64                      _enumValue{ 0 };         ///< 열거형의 지금 값
        uint64                     _key{ 0 };               ///< 프레임을 넘어 같은 행을 가리키는 키(높이 · 펼침 상태)
        float32                    _height{ 0.0f };         ///< `Skipped` 행이 차지하는 높이(지난 프레임에 잰 값)
        float32                    _scale{ 1.0f };          ///< 숫자 · 벡터의 표시 배율(보이는 값 = 저장 값 × 이것)
        float32                    _dragSpeed{ 0.0f };      ///< 숫자의 픽셀당 표시 값 변화. 0 이면 타입의 기본값
        uint32                     _rowID{ 0 };             ///< 모델 명령이 받는 행 번호(이 프레임)
        uint32                     _parentRowID{ 0 };       ///< 자식 행의 부모(컨테이너 · 구조체) 행 번호. 깊이 0 행은 자신이다
        uint32                     _ordinal{ 0 };           ///< 자식 행의 자리(시퀀스 원소 번호 · 맵 항목 순번)
        uint32                     _elementCount{ 0 };      ///< 컨테이너의 원소 수
        uint16                     _depth{ 0 };             ///< 0 은 표의 한 줄, 1 이상은 부모 칸 안의 자식
        PropertyRowKind            _kind{ PropertyRowKind::Builtin };
        PropertyRowChild           _child{ PropertyRowChild::None };
        InspectorValueWidget       _widget{ InspectorValueWidget::None }; ///< `Builtin` 의 위젯 종류
        InspectorHandleState       _handleState{};                        ///< 타입 핸들 선택기의 지금 값 상태
        uint8                      _builtinIndex{ 0 };                    ///< `Builtin` 의 `InspectorBuiltinValue` 표 자리
        uint8                      _bReadOnly          : 1;               ///< `ReadOnly` 메타 — 값을 글로만 보인다
        uint8                      _bDisabled          : 1;               ///< 편집을 막는다(`EditCondition` 거짓 · 막힌 컨테이너의 원소)
        uint8                      _bModified          : 1;               ///< 기본값과 다르다(되돌리기 단추) — 깊이 0 행만
        uint8                      _bCanReset          : 1;               ///< 기본값이 있다(CDO 나 메타 기본값 글) — 행 메뉴의 Reset
        uint8                      _bMixed             : 1;               ///< 다중 선택에서 값이 갈린다
        uint8                      _bFavoriteTarget    : 1;               ///< 행 메뉴에 즐겨찾기 토글을 둔다(대상 자신의 프로퍼티)
        uint8                      _bFavorite          : 1;               ///< 즐겨찾기한 프로퍼티
        uint8                      _bTransient         : 1;               ///< 저장하지 않는 프로퍼티
        uint8                      _bPasteAllowed      : 1;               ///< 글 붙여 넣기를 받는다(타입 핸들은 선택기만이 입력 길이다)
        uint8                      _bExpanded          : 1;               ///< 펼친 노드 · 카테고리(창이 `SetNextItemOpen` 으로 넘긴다)
        uint8                      _bColor             : 1;               ///< 색 선택기로 그린다
        uint8                      _bColorHDR          : 1;               ///< 색이 1 을 넘는 값을 받는다
        uint8                      _bAssetPath         : 1;               ///< 에셋 경로 칸(끌어 놓기 · 선택 팝업)
        uint8                      _bMultiline         : 1;               ///< 여러 줄 글
        uint8                      _bHandlePicker      : 1;               ///< 타입 핸들 선택기로 그린다(`_valueText` 가 미리보기)
        uint8                      _bHandleUnavailable : 1;               ///< 선택기를 그릴 수 없다(대상 타입 · 씬 없음) — `_valueText` 가 오류 글
        uint8                      _bCanAddRemove      : 1;               ///< 컨테이너에 원소를 더하고 지울 수 있다
        uint8                      _bRemovable         : 1;               ///< 자식 행 앞에 지우기 단추를 둔다(맵 항목 · `set` 원소)

        PropertyValueRow();

        /** @brief 값을 기본으로 되돌립니다. 글 · 벡터의 저장 공간은 남긴다(행을 프레임마다 다시 쓴다). */
        void clear();
    };

    /** @brief 그리드가 gather 에 넘기는 그리기 영역입니다(이번 프레임의 스크롤 — 값을 채울 행을 정한다). 화면 좌표입니다. */
    struct PropertyGridRowWindow
    {
        float32 _originY{ 0.0f };      ///< 그리드가 그리기 시작하는 높이(커서)
        float32 _clipMinY{ 0.0f };     ///< 보이는 영역의 위
        float32 _clipMaxY{ 0.0f };     ///< 보이는 영역의 아래
        float32 _rowHeight{ 0.0f };    ///< 잰 적 없는 행의 높이(한 줄 위젯 + 칸 여백)
        float32 _headerHeight{ 0.0f }; ///< 잰 적 없는 카테고리 머리의 높이
        uint8   _bSkipOffscreen   : 1; ///< 화면 밖 행을 `Skipped` 로 넘긴다(끄면 모든 행에 값을 채운다)
        uint8   _bNameSkippedRows : 1; ///< `Skipped` 행에도 `_markName` 을 채운다(자체 테스트가 화면 밖 표식으로 스크롤해 오게)

        PropertyGridRowWindow();
    };

    /** @brief 이번 프레임의 모두 펼치기 · 접기 요청입니다(보기 옵션 메뉴). */
    enum class OpenAllRequest : uint8
    {
        None,
        ExpandAll,
        CollapseAll,
    };

    /** @brief gather 한 번의 입력입니다. */
    struct PropertyGridGatherParams
    {
        PropertyGridRowWindow        _window{};
        string_view                  _filterText{};                           ///< 검색 글
        const vector<hashed_string>* _pListDrawnName{ nullptr };              ///< 인스펙터 확장이 이미 그린 이름(다시 그리지 않는다). nullptr 이면 없다
        OpenAllRequest               _openAllRequest{ OpenAllRequest::None }; ///< 모두 펼치기 · 접기 요청
        uint8                        _bFavoritesOnly : 1;                     ///< 즐겨찾기한 프로퍼티만 타입 이름 머리 하나로
        uint8                        _bModifiedOnly  : 1;                     ///< 기본값과 다른 프로퍼티만

        PropertyGridGatherParams();
    };

    /** @brief 그리드가 한 대상을 그릴 값입니다. 패널(그리드)이 멤버로 들어 행의 저장 공간을 다시 쓴다. */
    struct PropertyGridViewState
    {
        vector<PropertyValueRow> _listRow{};        ///< 앞 `_rowCount` 개가 이번 프레임의 행(위에서 아래 순서, 자식은 부모 바로 뒤)
        uint32                   _rowCount{ 0 };    ///< 이번 프레임의 행 수
        uint32                   _filledCount{ 0 }; ///< 값을 채운 행 수(`Skipped` · 카테고리 머리 밖) — 프로브 `Editor.InspectorFilledRows`
        uint8                    _bNoMatch : 1;     ///< 검색 글이 아무 프로퍼티도 맞히지 못했다
        uint8                    _bHasRows : 1;     ///< 그릴 프로퍼티가 있다(구분선 제목을 둔다)

        PropertyGridViewState();
    };
} // namespace sw::editor
