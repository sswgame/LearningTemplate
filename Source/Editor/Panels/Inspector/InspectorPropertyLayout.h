/**
 * @file InspectorPropertyLayout.h
 * @brief 인스펙터가 리플렉션 프로퍼티를 무엇을 어떤 순서로 그릴지 정합니다. ImGui 에 의존하지 않으므로 테스트를 붙일 수 있습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/TextAddress.h"

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
     * @brief 리플렉션 프로퍼티를 인스펙터에 표시하는 단위입니다. 표시 값은 저장 값에 `_scale` 을 곱한 값입니다.
     * @details 각도는 라디안으로 저장하고(`Units=rad`: 트랜스폼 회전, FOV, 원뿔 각) 도 단위로 보여 주고 편집한다. 언리얼 Details 의 FRotator 와 FOV,
     *          유니티 인스펙터의 `localEulerAngles` 와 `fieldOfView` 가 모두 도 단위를 쓴다. 라디안 값에 `Units=deg` 를 적으면 인스펙터가
     *          그 라디안을 1 픽셀에 0.5(약 29도)씩 움직이므로 주의한다.
     *          0..1 비율도 같은 방식이다. `Units=ratio` 로 적으면 백분율(× 100, "%")로 보여 준다. `Units=%` 로 적으면 0.5 를 "0.5 %" 로 읽게 되고,
     *          그 `%` 는 printf 서식 문자라 화면에는 붙지도 않는다.
     */
    struct InspectorDisplayUnit
    {
        float32 _scale{ 1.0f };     ///< 보이는 값 = 저장 값 × 이것
        float32 _dragSpeed{ 0.0f }; ///< 픽셀당 표시 값의 변화량. 0 이면 타입의 기본값을 쓴다
        string  _suffix{};          ///< 서식 뒤에 붙는 단위 문자열. 없으면 비어 있다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 숫자 프로퍼티의 두 범위입니다. 위젯(슬라이더, 드래그)이 움직이는 범위와 값이 머무르는 허용 범위가 있으며, 값은 저장 단위입니다.
     * @details 위젯 범위는 `UIMin` 과 `UIMax` 가 있으면 그 값이고, 없으면 허용 범위(`Min`, `Max`)이다. 허용 범위는 항상 `Min` 과 `Max` 이며, 위젯이
     *          그 밖의 값을 내면(직접 입력) 그 안으로 제한한다. 언리얼의 `UIMin`/`ClampMin` 구분과 같다.
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
        bool    _bSlider{ false }; ///< 슬라이더로 그린다. UIMin 과 UIMax 가 모두 있거나, Min 과 Max 가 모두 있고 `Meta = "Slider"` 일 때다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 인스펙터가 한 카테고리로 묶어 그리는 리플렉션 프로퍼티입니다. */
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
     * @brief 컴포넌트 인스펙터의 배치 규칙입니다. 상속 단계(기반에서 파생 순서)로 조립합니다.
     * @details 규칙은 셋이다. 첫째, 상속한 프로퍼티까지 모두 보인다. 둘째, 인스펙터 확장은 하위 클래스에도 적용된다. 게임이 만든 SceneComponent 파생에도
     *          트랜스폼 항목이 있다. 셋째, 확장은 자기가 그린 프로퍼티만 감추고 리플렉션 프로퍼티를 통째로 감추지 않는다.
     *          언리얼 Details 패널은 상속한 UPROPERTY 를 모두 보이고, `IDetailCustomization` 이 하위 클래스에도 적용되며, 자기가 그린 것만 `HideProperty` 한다.
     *          유니티의 `CustomEditor( editorForChildClasses: true )` 와 `DrawDefaultInspector` 도 같은 구조다.
     */
    struct InspectorPropertyLayout
    {
        /** @brief 타입 상속 사슬을 기반에서 파생 순서로 모읍니다. @p type 이 마지막입니다. 사슬이 순환하면 멈춥니다. */
        static void collectTypeChain( const TypeInfo& type, vector<const TypeInfo*>& outListType );

        /**
         * @brief 상속한 프로퍼티까지 리플렉션 프로퍼티를 카테고리로 묶습니다.
         * @details 카테고리는 기반 클래스부터 처음 나온 순서이고(트랜스폼처럼 기반의 그룹이 먼저 온다), 그룹 안의 프로퍼티도 같은 순서다.
         *          인스펙터에서 숨긴 프로퍼티, 인스펙터 확장이 직접 그린 프로퍼티(@p listDrawnName), 검색어(@p filter)에 맞지 않는 프로퍼티는 뺀다.
         */
        static void collectPropertyGroups( const TypeInfo& type, const vector<hashed_string>& listDrawnName, const EditorListFilter& filter,
                                           vector<InspectorPropertyGroup>& outListGroup );

        /** @brief 프로퍼티의 표시 이름을 반환합니다. DisplayName, 첫 별칭, 이름 순으로 찾습니다. */
        static const utf8* getPropertyLabel( const PropertyInfo& prop );
        /**
         * @brief 프로퍼티 편집의 되돌리기 이름("Edit <표시 이름>")을 만듭니다. 이름이 비었거나 ImGui 라벨 조각("##…")이면 "Edit Property" 입니다.
         * @details 위젯 라벨("##value")을 되돌리기 이름으로 쓰면 History 목록에서 "##" 뒤가 숨겨져 모든 항목이 "Edit" 로 보인다.
         */
        static string makeUndoLabel( const utf8* pPropertyLabel );

        /**
         * @brief 시퀀스 프로퍼티 @p containerProp 의 원소 하나를 값 위치(오프셋 0)에 둔 프로퍼티입니다. 원소 타입, 이름, 메타, 타입 핸들의 대상 타입을 싣습니다.
         * @details 원소 프로퍼티를 손으로 필드 몇 개만 복사해 만들면 `_handleTargetTypeName` 이 빠져 `vector<TComponentHandle<T>>` 의 원소가 선택기 대신
         *          정수 입력으로 떨어진다. 시퀀스 원소의 프로퍼티는 이 함수 하나로 만든다.
         */
        static PropertyInfo makeElementProperty( const PropertyInfo& containerProp );

        /** @brief 각도를 도 단위로 편집할 때 픽셀당 도 수입니다. 트랜스폼 섹션과 시퀀서도 같은 값을 씁니다. */
        static constexpr float32 kAngleDragSpeed = 0.5f;
        /** @brief 비율을 백분율로 편집할 때 픽셀당 퍼센트입니다. */
        static constexpr float32 kPercentDragSpeed = 0.5f;

        /**
         * @brief 프로퍼티의 `Units` 메타로 표시 단위를 정합니다.
         * @details `rad` 는 도로 보여 준다(배율 180/π, 드래그 속도, "deg"). `ratio` 는 백분율로 보여 준다(배율 100, 드래그 속도, "%"). 나머지는 단위 문자열만 붙는다.
         */
        static InspectorDisplayUnit getDisplayUnit( const PropertyInfo& prop );

        /**
         * @brief 숫자 서식(`"%.3f"`) 뒤에 단위 문자열을 붙인 printf 서식을 만듭니다.
         * @details 단위 문자열은 서식 안에 들어가므로 `%` 는 `%%` 로 적는다. 그대로 두면 짝 없는 변환 지정자가 되어 문자가 사라지거나
         *          다음 인자를 읽는다. UCRT 는 조용히 버린다.
         */
        static string appendUnitSuffix( const utf8* pNumberFormat, const string& suffix );

        /** @brief 함수나 이벤트의 인자 목록을 `int32 amount, float32 scale = 1.5f` 형태로 만듭니다. 이름이나 기본 인자가 없으면 생략합니다. */
        static string formatParameterList( const vector<FunctionParameterInfo>& listParameter );

        /**
         * @brief 숫자 프로퍼티의 위젯 범위와 허용 범위를 구합니다(`InspectorNumericRange`).
         * @details 경계는 숫자(`Min = 0`)이거나 같은 타입의 필드(`Min = "-_halfSize"`, `PropertyMetadata::_pReadMinRange`)다. 필드 경계는
         *          @p instance(프로퍼티를 가진 객체)에서 지금 값을 읽는다. 필드 경계가 있는 프로퍼티에 @p instance 가 nullptr 이면 멈춘다.
         */
        static InspectorNumericRange getNumericRange( const PropertyInfo& prop, ObjectAddress instance );
        /** @brief 값을 허용 범위 안으로 제한합니다. 범위가 적힌 쪽만 제한합니다. */
        static float64 clampToAllowedRange( const InspectorNumericRange& range, float64 value );

        /** @brief 색 선택기로 그릴지 정합니다. `ColorHdr` 이거나 `Meta = "Color"` 이거나 이름에 color 가 들어간 float3, float4 이면 true 입니다. */
        static bool isColorRequested( const PropertyInfo& prop );

        /**
         * @brief 경로가 `FileFilter` 에 맞는지 검사합니다. 필터는 `"*.png;*.dds"` 처럼 `;` 나 `,` 로 나눈 와일드카드입니다. 필터가 비어 있으면 항상 맞고, 대소문자는 구분하지 않습니다.
         * @details 경로 입력란이 드래그해 놓은 경로를 받을지 판단한다.
         */
        static bool matchesFileFilter( string_view filter, string_view path );
    };
} // namespace sw::editor
