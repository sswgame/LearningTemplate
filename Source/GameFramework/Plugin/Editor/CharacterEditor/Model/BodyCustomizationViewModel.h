/**
 * @file BodyCustomizationViewModel.h
 * @brief 캐릭터 편집 창의 몸 탭 ViewModel 입니다 — 슬롯 숨김, 몸 채널(값 · 이 스켈레톤의 매핑), 염색 구간(별칭 · 묶음 · 염색 여부 · 걸린 염색 수), 피부 층 목록(더하기 · 순서 · 숨김 · 색 · 배치),
 *        몸 동작 미리보기(맞음 · 울기 · 침 · 번짐 지우기와 읽은 값)를 모으고 명령을 받습니다.
 * @details ImGui 를 모른다. `gather` 가 값만 든 상태(`BodyCustomizationViewState`)를 채우고, View 는 그 상태만 읽어 그리며 바꿀 때는 이름 붙은 명령을 부른다.
 *          선택 편집은 외형 편집 모델(`AppearanceEditModel` — 되돌리기 기록)을 지나고, 미리보기 명령(맞음 · 울기)은 미리보기 몸만 바꾸므로 기록하지 않는다.
 *          채널 매핑 · 지글 물성 · 주름 · 체액 경로 · 구간 별칭은 데이터 파일(`*.bodyshape.xml` · `bodyprofiles.xml` · `itemvisuals.xml`)이다 — 이 탭은 그 파일을 몸 데이터 편집 모델
 *          (`BodyDataEditModel` — 원소 열쇠 경로 · 되돌리기 · 저장)로 고치고, 저장하면 외형 데이터 감시가 다시 읽어 미리보기 몸에 건다. 지글은 맞음 · 동작 태그(달리기) 시험 단추로,
 *          체액은 울기 · 침 단추로, 구간 별칭은 뷰포트에서 구간을 집어(`BodySurfacePick`) 고르고 드라이브의 include · exclude 가 고르는 구간을 강조한다(언리얼 페르소나의 미리보기 · 검은사막 커스터마이즈의 "동작 보기" 자리).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/Facial/WrinkleMap.h"
#include "Engine/Animation/Rig/RigBodyInput.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

namespace sw
{
    class AppearanceDatabase;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    class AppearanceEditModel;
    class BodyDataEditModel;

    /** @brief 채널이 이 몸의 매핑에서 어떻게 풀리는지입니다. */
    enum class BodyChannelMapping : uint8
    {
        Mapped = 0,  ///< 체형 축이 본 · 모프로 옮긴다
        Unsupported, ///< 이 스켈레톤이 받지 않는다고 선언했다
        Missing      ///< 매핑도 선언도 없다(로드가 오류를 남겼다)
    };

    /** @brief 몸 탭이 그리는 값입니다(포인터 · 참조 없음). */
    struct BodyCustomizationViewState
    {
        struct SlotRow
        {
            hashed_string _slot{};
            hashed_string _item{};
            uint8         _bHidden{ SW_FALSE };
        };
        struct ChannelRow
        {
            hashed_string      _channel{};
            float32            _value{ 0.0f };
            BodyChannelMapping _mapping{ BodyChannelMapping::Mapped };
        };
        struct SectionRow
        {
            hashed_string _owner{};
            hashed_string _visual{}; ///< 구간을 적은 외형 id(`itemvisuals.xml` 의 `<ItemVisual id>` — 편집 열쇠)
            hashed_string _part{};
            hashed_string _section{};
            hashed_string _alias{};
            string        _groups{};
            uint32        _dyeCount{ 0 }; ///< 이 구간에 걸린 염색 채널 수(해석 결과)
            uint8         _bDyeable{ SW_TRUE };
            uint8         _bHighlighted{ SW_FALSE }; ///< 고른 드라이브(`_highlightDrive`)의 include · exclude 가 이 구간을 고른다
        };
        struct DriveRow
        {
            string        _label{};     ///< "매개변수 / 드라이브 대상"(콤보 글)
            hashed_string _parameter{}; ///< 꾸미기 매개변수 이름
            uint32        _drive{ 0 };  ///< 그 매개변수의 드라이브 번호
        };
        struct JiggleRow
        {
            RigJiggleGroupParams _params{}; ///< 해석된 프로필(상속을 합친)의 값
        };
        struct WrinkleRow
        {
            WrinkleRegionDef _region{};
        };
        struct FluidRow
        {
            hashed_string  _name{};
            vector<float2> _listPathPoint{};
            float32        _ratePerSecond{ 0.0f };
            float32        _speed{ 0.0f };
            float32        _radius{ 0.0f };
        };
        struct ActionRow
        {
            hashed_string _tag{};
            float32       _scale{ 1.0f };
            uint8         _bActive{ SW_FALSE }; ///< 미리보기 몸이 지금 이 태그를 가졌다(시험 단추가 켰다)
        };
        struct LayerRow
        {
            SkinLayerValue _value{};
            vector<float4> _listSwatch{}; ///< 견본 묶음(비면 자유 색)
            hashed_string  _category{};
            float32        _smearResistance{ 0.0f };
            uint8          _bPlaceable{ SW_FALSE };
        };

        vector<SlotRow>       _listSlot{};
        vector<ChannelRow>    _listChannel{};
        vector<SectionRow>    _listSection{};
        vector<LayerRow>      _listLayer{};
        vector<hashed_string> _listAvailableLayer{}; ///< 더할 수 있는 층 종류
        vector<hashed_string> _listFluidTrigger{};   ///< 몸 프로필의 체액 방아쇠(미리보기 단추)
        vector<DriveRow>      _listDrive{};          ///< 구간을 고르는 드라이브(강조 콤보)
        vector<JiggleRow>     _listJiggle{};
        vector<WrinkleRow>    _listWrinkle{};
        vector<FluidRow>      _listFluid{};
        vector<ActionRow>     _listAction{};
        hashed_string         _bodyProfile{};
        hashed_string         _bodyShapes{}; ///< 몸 부품의 채널 매핑 파일(`*.bodyshape.xml`, 리소스 id)
        float32               _wrinkleIntensity{ 1.0f };
        float32               _footLift{ 0.0f };
        float32               _pupilSize{ 0.0f };
        float32               _eyeLuminance{ 0.0f };
        float32               _jiggleDisplacement{ 0.0f };
        float32               _fluidWetness{ 0.0f };
        float32               _smearDilution{ 0.0f };
        uint8                 _bHasDynamics{ SW_FALSE }; ///< 미리보기 몸에 몸 동작 컴포넌트가 있다
        uint8                 _bHasBody{ SW_FALSE };     ///< 미리보기 몸이 조립됐다
    };

    /** @class BodyCustomizationViewModel @brief 파일 머리말 참고. */
    class BodyCustomizationViewModel
    {
    public:
        /** @brief 미리보기 맞음 충격의 크기(m/s)입니다. */
        static constexpr float32 kPreviewHitSpeed = 3.0f;
        /** @brief 미리보기 체액 방아쇠를 켜 두는 시간(초)입니다. */
        static constexpr float32 kPreviewFluidSeconds = 3.0f;

        /** @brief 동작 태그 시험이 켜진 동안 걸음마다 주는 위 충격의 크기(m/s)입니다(달리기 착지). */
        static constexpr float32 kPreviewStepSpeed = 1.2f;
        /** @brief 동작 태그 시험의 걸음 간격(초)입니다. */
        static constexpr float32 kPreviewStepSeconds = 0.35f;
        /** @brief 새 채널 축의 기본 범위입니다(매핑이 없는 채널에 축을 더할 때 — 본은 사람이 더한다). */
        static constexpr float32 kNewAxisMin = -1.0f;
        static constexpr float32 kNewAxisMax = 1.0f;

        /**
         * @brief 상태를 채웁니다(그리기마다 한 번). @p pBody 는 미리보기 몸(없으면 nullptr — 몸 값은 비운다).
         * @param highlightDrive 강조할 드라이브의 `_listDrive` 번호입니다(무효면 강조 없음). 구간 줄의 `_bHighlighted` 를 채운다.
         */
        static void gather( const AppearanceEditModel& model, const AppearanceDatabase& database, const GameObject* pBody, uint32 highlightDrive, BodyCustomizationViewState& outState );

        // --- 몸 데이터 편집(파일 — 되돌리기 한 단계씩, `BodyDataEditModel`) ---
        /** @brief 채널 @p channel 의 축 속성(`min` · `max` · `default` · `response`)을 바꿉니다(`*.bodyshape.xml`). */
        static bool setAxisValue( BodyDataEditModel& shapes, const hashed_string& channel, const utf8* pAttribute, float32 value, bool bContinuous );
        /** @brief 매핑이 없는 채널에 빈 축(기본 범위)을 더합니다 — 본 · 모프는 파일에서 더한다. */
        static bool addAxis( BodyDataEditModel& shapes, const hashed_string& channel );
        /** @brief 채널을 이 스켈레톤이 받지 않는다고 선언합니다(`<Unsupported name>`). */
        static bool declareUnsupported( BodyDataEditModel& shapes, const hashed_string& channel );
        /** @brief 프로필 @p profile 의 지글 묶음 @p group 속성을 바꿉니다(파생 프로필이면 그 프로필에 덮어쓰기가 생긴다). */
        static bool setJiggleValue( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& group, const utf8* pAttribute, float32 value, bool bContinuous );
        static bool setWrinkleIntensity( BodyDataEditModel& profiles, const hashed_string& profile, float32 value, bool bContinuous );
        static bool setWrinkleMask( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& region, uint32 mask );
        static bool setWrinkleWeight( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& region, const hashed_string& expression, float32 weight, bool bContinuous );
        /** @brief 체액 @p fluid 의 경로를 바꿉니다(`"u v; u v; …"` — 점 둘 이상). */
        static bool setFluidPath( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& fluid, vector_reference<const float2> listPoint, bool bContinuous );
        static bool setFluidValue( BodyDataEditModel& profiles, const hashed_string& profile, const hashed_string& fluid, const utf8* pAttribute, float32 value, bool bContinuous );
        /** @brief 구간 별칭 · 묶음(쉼표) · 염색 여부를 바꿉니다(`itemvisuals.xml` 의 `<ItemVisual id><Part name><Section name>`). */
        static bool setSectionAlias( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, string_view alias );
        static bool setSectionGroups( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, string_view groups );
        static bool setSectionDyeable( BodyDataEditModel& visuals, const BodyCustomizationViewState::SectionRow& row, bool bDyeable );
        /** @brief 구간 이름 @p section 의 줄 번호입니다(뷰포트에서 집은 구간 — 이름이 같은 첫 줄). 없으면 무효입니다. */
        static uint32 findSectionRow( const BodyCustomizationViewState& state, const hashed_string& section );
        /** @brief 배치 층 @p index 의 가운데를 피부 UV @p uv 로 옮깁니다(뷰포트에서 표면을 집어 끄는 배치 — 끄는 동안 한 되돌리기 단계). */
        static bool placeSkinLayer( AppearanceEditModel& model, uint32 index, const float2& uv, bool bContinuous );

        // --- 선택 편집(되돌리기 한 단계씩) ---
        static bool setSlotHidden( AppearanceEditModel& model, const hashed_string& slot, bool bHidden );
        /** @brief 층 종류 @p layer 를 그 기본 색 · 불투명도로 맨 위에 더합니다. 모르는 종류면 false 입니다. */
        static bool addSkinLayer( AppearanceEditModel& model, const AppearanceDatabase& database, const hashed_string& layer );
        static bool removeSkinLayer( AppearanceEditModel& model, uint32 index );
        static bool moveSkinLayer( AppearanceEditModel& model, uint32 index, int32 delta );
        static bool setSkinLayer( AppearanceEditModel& model, uint32 index, const SkinLayerValue& value, bool bContinuous );

        // --- 미리보기(몸만 바꾼다 — 기록하지 않는다) ---
        /** @brief 미리보기 몸의 지글에 위로 맞음 충격을 줍니다. */
        static void previewHit( GameObject* pBody );
        /** @brief 미리보기 몸의 체액 방아쇠를 켭니다(울기 · 침). */
        static void previewFluid( GameObject* pBody, const hashed_string& trigger );
        /** @brief 미리보기 몸의 화장 번짐을 지웁니다. */
        static void clearSmears( GameObject* pBody );
        /** @brief 미리보기 몸에 동작 태그 @p tag 를 켜고 끕니다(지글 동작 배율 시험 — 달리기 · 공격). */
        static void previewActionTag( GameObject* pBody, const hashed_string& tag, bool bOn );
        /** @brief 걸음 충격 한 번입니다(동작 태그 시험이 켜진 동안 걸음 간격마다). */
        static void previewStep( GameObject* pBody );
    };
} // namespace sw::editor
