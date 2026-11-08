/**
 * @file BodyCustomizationViewModel.h
 * @brief 캐릭터 편집 창의 몸 탭 ViewModel 입니다 — 슬롯 숨김, 몸 채널(값 · 이 스켈레톤의 매핑), 염색 구간(별칭 · 묶음 · 염색 여부 · 걸린 염색 수), 피부 층 목록(더하기 · 순서 · 숨김 · 색 · 배치),
 *        몸 동작 미리보기(맞음 · 울기 · 침 · 번짐 지우기와 읽은 값)를 모으고 명령을 받습니다.
 * @details ImGui 를 모른다. `gather` 가 값만 든 상태(`BodyCustomizationViewState`)를 채우고, View 는 그 상태만 읽어 그리며 바꿀 때는 이름 붙은 명령을 부른다.
 *          선택 편집은 외형 편집 모델(`AppearanceEditModel` — 되돌리기 기록)을 지나고, 미리보기 명령(맞음 · 울기)은 미리보기 몸만 바꾸므로 기록하지 않는다.
 *          채널 매핑 · 지글 물성 · 주름 · 체액 경로 · 구간 별칭의 저작은 데이터 파일(`*.bodyshape.xml` · `bodyprofiles.xml` · `itemvisuals.xml`)이고, 이 탭은 그것이 이 몸에 어떻게
 *          풀렸는지 보이고 미리보기로 시험한다(언리얼 페르소나의 미리보기 · 검은사막 커스터마이즈의 "동작 보기" 자리).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SkinLayer.h"

namespace sw
{
    class AppearanceDatabase;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    class AppearanceEditModel;

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
            hashed_string _part{};
            hashed_string _section{};
            hashed_string _alias{};
            string        _groups{};
            uint32        _dyeCount{ 0 }; ///< 이 구간에 걸린 염색 채널 수(해석 결과)
            uint8         _bDyeable{ SW_TRUE };
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
        hashed_string         _bodyProfile{};
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

        /** @brief 상태를 채웁니다(그리기마다 한 번). @p pBody 는 미리보기 몸(없으면 nullptr — 몸 값은 비운다). */
        static void gather( const AppearanceEditModel& model, const AppearanceDatabase& database, const GameObject* pBody, BodyCustomizationViewState& outState );

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
    };
} // namespace sw::editor
