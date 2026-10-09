/**
 * @file InspectorPropertyLayout.h
 * @brief 인스펙터가 반사 프로퍼티를 무엇을 · 어떤 순서로 그리는지 정합니다(ImGui 에 의존하지 않아 시험을 붙일 수 있습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct FunctionParameterInfo;
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class EditorListFilter;

    /**
     * @brief 반사 프로퍼티를 인스펙터에 보이는 단위입니다. 보이는 값 = 저장 값 × `_scale` 입니다.
     * @details 각도는 라디안으로 저장하고(`Units=rad` — 트랜스폼 회전 · FOV · 원뿔 각) 도로 보이고 고칩니다. 언리얼 Details 의 FRotator · FOV,
     *          유니티 인스펙터의 `localEulerAngles` · `fieldOfView` 가 모두 도입니다. 주의: 라디안 값에 `Units=deg` 를 적으면 인스펙터가
     *          그 라디안을 1 픽셀에 0.5(약 29 도)씩 움직인다.
     *          0..1 비율도 같은 모양이다 — `Units=ratio` 로 적고 백분율(× 100, "%")로 보인다. `Units=%` 로 적으면 0.5 를 "0.5 %" 로 읽게 되고,
     *          그 `%` 는 printf 서식 문자라 화면에는 붙지도 않는다.
     */
    struct InspectorDisplayUnit
    {
        float32 _scale{ 1.0f };     ///< 보이는 값 = 저장 값 × 이것
        float32 _dragSpeed{ 0.0f }; ///< 픽셀당 보이는 값의 변화. 0 이면 타입의 기본입니다
        string  _suffix{};          ///< 서식 뒤에 붙는 단위 글자. 없으면 빈 것입니다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 숫자 프로퍼티의 두 범위 — 위젯(슬라이더 · 드래그)이 움직이는 범위와 값이 머무는 허용 범위입니다. 값은 저장 단위입니다.
     * @details 위젯 범위는 `UiMin` · `UiMax` 가 있으면 그것, 없으면 허용 범위(`Min` · `Max`)입니다. 허용 범위는 늘 `Min` · `Max` 이고, 위젯이
     *          그 밖의 값을 냈으면(직접 입력) 그 안으로 막습니다 — 언리얼 `UIMin`/`ClampMin` 의 나눔과 같습니다.
     */
    struct InspectorNumericRange
    {
        float64 _widgetMin{ 0.0 };
        float64 _widgetMax{ 0.0 };
        float64 _clampMin{ 0.0 };
        float64 _clampMax{ 0.0 };
        bool    _bHasWidgetMin{ false };
        bool    _bHasWidgetMax{ false };
        bool    _bHasClampMin{ false };
        bool    _bHasClampMax{ false };
        bool    _bSlider{ false }; ///< 슬라이더로 그린다(UiMin · UiMax 둘 다, 또는 Min · Max 둘 다 + `Meta = "Slider"`)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 인스펙터가 한 카테고리로 묶어 그리는 반사 프로퍼티입니다. */
    struct InspectorPropertyGroup
    {
        string                      _category;
        vector<const PropertyInfo*> _listProperty;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct InspectorPropertyLayout
     * @brief 컴포넌트 인스펙터의 배치 규칙입니다 — 상속 단계(기반 → 파생)로 조립합니다.
     * @details (1) 상속분까지 모든 프로퍼티를 보이고, (2) 인스펙터 확장은 하위 클래스에도 걸리며(게임이 만든 SceneComponent 파생에도
     *          트랜스폼 칸이 있다), (3) 확장은 자기가 그린 것만 감추고 반사 프로퍼티를 **통째로** 감추지 않습니다. 언리얼 Details 패널이
     *          상속 UPROPERTY 를 모두 보이고 `IDetailCustomization` 이 하위 클래스에도 걸리며 자기가 그린 것만 `HideProperty` 하는 것, 유니티
     *          `CustomEditor( editorForChildClasses: true )` · `DrawDefaultInspector` 와 같은 모양입니다.
     */
    struct InspectorPropertyLayout
    {
        /** @brief 타입 사슬을 기반 → 파생 순서로 모읍니다(@p type 이 마지막). 사슬이 돌면 멈춥니다. */
        static void collectTypeChain( const TypeInfo& type, vector<const TypeInfo*>& outListType );

        /**
         * @brief 상속분까지 반사 프로퍼티를 카테고리로 묶습니다.
         * @details 카테고리는 기반부터 처음 나온 순서이고(트랜스폼처럼 기반의 묶음이 먼저 온다), 묶음 안도 그 순서입니다. 인스펙터에서 숨긴 것 ·
         *          인스펙터 확장이 직접 그린 것(@p listDrawnName) · 검색어(@p filter)에 걸리지 않는 것은 뺍니다.
         */
        static void collectPropertyGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                           vector<InspectorPropertyGroup>& outListGroup );

        /** @brief 프로퍼티의 표시 이름입니다(DisplayName → 첫 별칭 → 이름). */
        static const utf8* getPropertyLabel( const PropertyInfo& prop );
        /**
         * @brief 프로퍼티 편집의 되돌리기 이름("Edit <표시 이름>")을 만듭니다. 이름이 비었거나 ImGui 라벨 조각("##…")이면 "Edit Property" 입니다.
         * @details 위젯 라벨("##value")을 되돌리기 이름으로 쓰면 History 의 목록 글에서 "##" 뒤가 숨겨져 모든 항목이 "Edit" 로 보였다.
         */
        static string makeUndoLabel( const utf8* pPropertyLabel );

        /** @brief 각도를 도로 고칠 때 픽셀당 도입니다. 트랜스폼 섹션 · 시퀀서도 같은 값을 씁니다. */
        static constexpr float32 kAngleDragSpeed = 0.5f;
        /** @brief 비율을 백분율로 고칠 때 픽셀당 퍼센트입니다. */
        static constexpr float32 kPercentDragSpeed = 0.5f;

        /**
         * @brief 프로퍼티의 `Units` 메타로 보이는 단위를 정합니다.
         * @details `rad` 는 도로(배율 180/π · 드래그 속도 · "deg"), `ratio` 는 백분율로(배율 100 · 드래그 속도 · "%") 보입니다. 나머지는 글자만 붙습니다.
         */
        static InspectorDisplayUnit getDisplayUnit( const PropertyInfo& prop );

        /**
         * @brief 숫자 서식(`"%.3f"`) 뒤에 단위 글자를 붙인 printf 서식을 만듭니다.
         * @details 단위 글자는 서식 안에 들어가므로 `%` 는 `%%` 로 적습니다 — 그대로 두면 짝 없는 변환 지정자가 되어 글자가 사라지거나
         *          (UCRT 는 조용히 버린다) 다음 인자를 읽는다.
         */
        static string appendUnitSuffix( const utf8* pNumberFormat, const string& suffix );

        /** @brief 함수 · 이벤트의 인자 목록을 `int32 amount, float32 scale = 1.5f` 꼴로 씁니다(이름 · 기본 인자가 없으면 뺀다). */
        static string formatParameterList( const vector<FunctionParameterInfo>& listParameter );

        /** @brief 숫자 프로퍼티의 위젯 범위 · 허용 범위입니다(`InspectorNumericRange`). */
        static InspectorNumericRange getNumericRange( const PropertyInfo& prop );
        /** @brief 값을 허용 범위 안으로 막습니다. 적힌 쪽만 막습니다. */
        static float64 clampToAllowedRange( const InspectorNumericRange& range, float64 value );

        /** @brief 색 선택기로 그릴지 — `ColorHdr` · `Meta = "Color"` · 이름에 color 가 든 float3/float4 입니다. */
        static bool isColorRequested( const PropertyInfo& prop );

        /**
         * @brief 경로가 `FileFilter`(`"*.png;*.dds"` — `;` · `,` 로 나눈 와일드카드)에 맞는지 봅니다. 필터가 비면 늘 맞습니다. 대소문자는 가리지 않습니다.
         * @details 경로 칸이 끌어다 놓은 경로를 받을지 정합니다.
         */
        static bool matchesFileFilter( string_view filter, string_view path );
    };
} // namespace sw::editor
