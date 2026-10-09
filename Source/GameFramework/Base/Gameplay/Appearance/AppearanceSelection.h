/**
 * @file AppearanceSelection.h
 * @brief 외형 선택 — 플레이어가 저장 · 공유하고 네트워크로 보내는 "기준 프리셋 + 씨앗 + 꾸미기 값 + 장비 구성" 과, 그것을 지금 콘텐츠에 맞춰 펼치는 일입니다.
 * @details 같은 선택 하나가 세 곳에 쓰입니다 — 플레이어 프리셋 저장(`UserAppearancePresetStore`), 공유 코드(`AppearanceShareCode`), 네트워크 동기화
 *          (`AppearanceSelectionCodec`). 이름은 32 비트 해시로 싣습니다(데이터 로드가 같은 해시의 두 이름을 막는다). 받는 쪽이 모르는 해시는 `#xxxxxxxx`
 *          자리 이름이 되어 펼칠 때 "지워진 콘텐츠" 로 보고됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/CustomizationValueSet.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceSpec;
    struct ResolvedAppearance;

    class AppearanceDatabase;
    class BitReader;
    class BitWriter;

    /**
     * @brief 외형 선택 하나입니다. `_listCategory` 가 비면 전체 프리셋, 아니면 그 묶음만 담은 부분 프리셋(머리만 · 얼굴만 · 염색만 · 장비 구성)입니다.
     * @details 묶음 이름은 스키마 매개변수의 `category` 이고, 장비 구성은 `kLoadoutCategory` 입니다.
     */
    struct SW_GF_API AppearanceSelection
    {
        static constexpr const utf8* kLoadoutCategory = "Loadout";

        CustomizationValueSet         _customization{};
        vector<AppearanceSlotRequest> _listSlot{};
        vector<hashed_string>         _listCategory{};
        hashed_string                 _basePresetId{};
        hashed_string                 _schema{};
        hashed_string                 _bodyType{};
        hashed_string                 _bodyShape{};
        hashed_string                 _face{};
        uint32                        _seed{ 0 };

        bool isPartial() const { return _listCategory.empty() == false; }
        /** @brief 묶음을 담는가입니다. 전체 프리셋은 모두 담습니다. */
        bool includesCategory( const hashed_string& category ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 칸을 기본으로 되돌린 까닭입니다. */
    enum class AppearanceFallbackReason : uint8
    {
        MissingItem = 0, ///< 콘텐츠에서 지워진 아이템
        LockedItem,      ///< 플레이어가 아직 얻지 못한 아이템
        UnknownSlot,     ///< 지워진 칸
        MissingVisual    ///< 지워진 보이는 외형(형상 변경만 버린다)
    };

    SW_GF_API const utf8* toString( AppearanceFallbackReason reason );

    /** @brief 선택을 지금 콘텐츠에 맞춰 펼친 보고 — 무엇을 버렸고 무엇을 기본으로 되돌렸는가입니다(플레이어에게 보여 준다). */
    struct AppearanceSelectionReport
    {
        struct SlotFallback
        {
            hashed_string            _slot{};
            hashed_string            _requestedItem{};
            hashed_string            _fallbackItem{};
            AppearanceFallbackReason _reason{ AppearanceFallbackReason::MissingItem };
        };

        vector<hashed_string> _listDroppedParameter{}; ///< 지워진 매개변수 · 항목(이름 또는 `#해시`)
        vector<SlotFallback>  _listSlotFallback{};
        uint8                 _bUnknownBasePreset{ SW_FALSE };

        bool isClean() const { return _listDroppedParameter.empty() && _listSlotFallback.empty() && _bUnknownBasePreset == SW_FALSE; }
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어가 그 아이템을 쓸 수 있는가(잠금)를 묻는 창구입니다. 없으면 모두 풀린 것으로 칩니다. */
    class SW_GF_API IAppearanceUnlockQuery
    {
    public:
        IAppearanceUnlockQuery()                                               = default;
        virtual ~IAppearanceUnlockQuery()                                      = default;
        IAppearanceUnlockQuery( const IAppearanceUnlockQuery& )                = default;
        IAppearanceUnlockQuery& operator=( const IAppearanceUnlockQuery& )     = default;
        IAppearanceUnlockQuery( IAppearanceUnlockQuery&& ) noexcept            = default;
        IAppearanceUnlockQuery& operator=( IAppearanceUnlockQuery&& ) noexcept = default;

        virtual bool isItemUnlocked( const hashed_string& itemId ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @struct AppearanceSelectionUtil
     *  @brief 선택 담기 · 펼치기 · 미리보기입니다. */
    struct SW_GF_API AppearanceSelectionUtil
    {
        /** @brief 지금 모습을 전체 선택으로 담습니다(모든 칸 — 빈 칸도 "비움" 으로). */
        static void captureSelection( const CharacterAppearanceSpec& spec, AppearanceSelection& outSelection );
        /** @brief @p selection 에서 @p listCategory 묶음만 남긴 부분 선택입니다. */
        static void makePartial( const AppearanceSelection& selection, const AppearanceDatabase& database, const vector<hashed_string>& listCategory, AppearanceSelection& outPartial );

        /**
         * @brief 선택을 지금 콘텐츠에 맞춰 펼칩니다. 전체 선택은 기준 프리셋을 씨앗으로 펼친 위에, 부분 선택은 @p current 위에 덮습니다.
         * @details 지워진 매개변수 · 항목은 버리고 보고합니다(새 매개변수는 기본값 그대로). 지워졌거나 잠긴 아이템은 그 칸의 기본(기준 프리셋의 것, 없으면 빈 칸)으로
         *          되돌리고 보고합니다. 실패하지 않습니다 — 플레이어 데이터는 콘텐츠가 바뀌어도 열려야 합니다.
         */
        static void applySelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                    const IAppearanceUnlockQuery* pUnlockQuery, CharacterAppearanceSpec& outSpec, AppearanceSelectionReport& outReport );
        /** @brief 입혀 보기 — 펼쳐서 해석만 하고 아무것도 바꾸지 않습니다. */
        static void previewSelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                      const IAppearanceUnlockQuery* pUnlockQuery, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceSelectionCodec
     * @brief 선택을 비트로 씁니다(네트워크 외형 동기화 — 받는 쪽은 펼쳐 해석하면 같은 해시가 나온다). 공유 코드 · 세이브도 이것을 씁니다.
     * @details 슬라이더는 스키마 범위의 16 비트, 색은 성분마다 8 비트, 피해는 8 비트 — 해석기가 쓰는 정밀도와 같아 손실이 없습니다. 값마다 종류 2 비트를
     *          붙여, 받는 쪽이 모르는 매개변수도 길이를 알고 건너뜁니다.
     */
    struct SW_GF_API AppearanceSelectionCodec
    {
        static void writeSelection( BitWriter& writer, const AppearanceSelection& selection, const AppearanceDatabase& database );
        /** @brief 읽습니다. 비트가 모자라거나 개수가 말이 안 되면 false 입니다. */
        [[nodiscard]] static bool readSelection( BitReader& reader, const AppearanceDatabase& database, AppearanceSelection& outSelection );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceShareCode
     * @brief 공유 코드 — 판 1 바이트 + 선택 비트 + CRC32 를 base64url(채움 없음)로 쓴 짧은 글자열입니다. 판이 높거나 체크섬이 틀리면 거절합니다.
     */
    struct SW_GF_API AppearanceShareCode
    {
        static constexpr uint32 kVersion = 1;

        static string encode( const AppearanceSelection& selection, const AppearanceDatabase& database );
        /** @brief 풉니다. 망가졌거나(base64 · 체크섬) 이 판이 모르는 판이면 false 이고 @p pOutReason 에 까닭을 씁니다. */
        [[nodiscard]] static bool decode( string_view code, const AppearanceDatabase& database, AppearanceSelection& outSelection, string* pOutReason = nullptr );
    };
} // namespace sw
