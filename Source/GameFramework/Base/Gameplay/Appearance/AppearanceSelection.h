/**
 * @file AppearanceSelection.h
 * @brief 외형 선택입니다. 플레이어가 저장하고 공유하며 네트워크로 보내는 "기준 프리셋, 시드, 꾸미기 값, 장비 구성" 의 묶음이고, 이것을 지금 콘텐츠에 맞춰 펼치는 일도 맡습니다.
 * @details 같은 선택 하나가 세 곳에 쓰인다. 플레이어 프리셋 저장(`UserAppearancePresetStore`), 공유 코드(`AppearanceShareCode`), 네트워크 동기화(`AppearanceSelectionCodec`)이다.
 *          이름은 32 비트 해시로 싣는다. 같은 해시를 가진 두 이름은 데이터 로드가 막는다. 받는 쪽이 알지 못하는 해시는 `#xxxxxxxx` 형태의
 *          임시 이름이 되고, 펼칠 때 "지워진 콘텐츠" 로 보고된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/CustomizationValueSet.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceDef;
    struct CharacterAppearanceSpec;
    struct ResolvedAppearance;

    class AppearanceDatabase;
    class BitReader;
    class BitWriter;

    /**
     * @brief 외형 선택 하나입니다. `_listCategory` 가 비어 있으면 전체 프리셋이고, 아니면 그 카테고리만 담은 부분 프리셋입니다. 머리만, 얼굴만, 염색만, 장비 구성만 담는 식입니다.
     * @details 카테고리 이름은 스키마 매개변수의 `category` 이고, 장비 구성의 카테고리는 `kLoadoutCategory` 이다.
     */
    struct SW_GF_API AppearanceSelection
    {
        static constexpr const utf8* kLoadoutCategory = "Loadout";

        CustomizationValueSet         _customization{};
        vector<AppearanceSlotRequest> _listSlot{};
        vector<hashed_string>         _listCategory{};
        hashed_string                 _basePresetID{};
        hashed_string                 _schema{};
        hashed_string                 _bodyType{};
        hashed_string                 _bodyShape{};
        hashed_string                 _face{};
        uint32                        _seed{ 0 };

        bool isPartial() const { return _listCategory.empty() == false; }
        /** @brief 카테고리를 담고 있으면 true 입니다. 전체 프리셋은 모든 카테고리를 담습니다. */
        bool includesCategory( const hashed_string& category ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 슬롯을 기본으로 되돌린 이유입니다. */
    ENUM()
    enum class AppearanceFallbackReason : uint8
    {
        MissingItem = 0, ///< 콘텐츠에서 지워진 아이템
        LockedItem,      ///< 플레이어가 아직 얻지 못한 아이템
        UnknownSlot,     ///< 지워진 슬롯
        MissingVisual    ///< 지워진 보이는 외형(형상 변경만 버린다)
    };

    /** @brief 선택을 지금 콘텐츠에 맞춰 펼친 결과의 보고입니다. 무엇을 버렸고 무엇을 기본으로 되돌렸는지 담으며, 플레이어에게 보여 줍니다. */
    struct AppearanceSelectionReport
    {
        struct SlotFallback
        {
            hashed_string            _slot{};
            hashed_string            _requestedItem{};
            hashed_string            _fallbackItem{};
            AppearanceFallbackReason _reason{ AppearanceFallbackReason::MissingItem };
        };

        vector<hashed_string> _listDroppedParameter{}; ///< 지워진 매개변수와 항목(이름 또는 `#해시`)
        vector<SlotFallback>  _listSlotFallback{};
        uint8                 _bUnknownBasePreset{ SW_FALSE };

        bool isClean() const { return _listDroppedParameter.empty() && _listSlotFallback.empty() && _bUnknownBasePreset == SW_FALSE; }
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어가 그 아이템을 쓸 수 있는지(잠금 여부)를 묻는 인터페이스입니다. 없으면 모두 잠금 해제된 것으로 취급합니다. */
    class IAppearanceUnlockQuery
    {
    public:
        IAppearanceUnlockQuery()                                               = default;
        virtual ~IAppearanceUnlockQuery()                                      = default;
        IAppearanceUnlockQuery( const IAppearanceUnlockQuery& )                = default;
        IAppearanceUnlockQuery& operator=( const IAppearanceUnlockQuery& )     = default;
        IAppearanceUnlockQuery( IAppearanceUnlockQuery&& ) noexcept            = default;
        IAppearanceUnlockQuery& operator=( IAppearanceUnlockQuery&& ) noexcept = default;

        virtual bool isItemUnlocked( const hashed_string& itemID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @struct AppearanceSelectionUtil
     *  @brief 선택 담기, 펼치기, 미리보기를 제공합니다. */
    struct SW_GF_API AppearanceSelectionUtil
    {
        /** @brief 지금 모습을 전체 선택으로 담습니다. 모든 슬롯을 담고, 빈 슬롯도 "비움" 으로 담습니다. */
        static void captureSelection( const CharacterAppearanceSpec& spec, AppearanceSelection& outSelection );
        /** @brief @p selection 에서 @p listCategory 카테고리만 남긴 부분 선택입니다. */
        static void makePartial( const AppearanceSelection& selection, const AppearanceDatabase& database, const vector<hashed_string>& listCategory, AppearanceSelection& outPartial );

        /**
         * @brief 선택을 지금 콘텐츠에 맞춰 펼칩니다. 전체 선택은 기준 프리셋을 시드로 펼친 결과 위에 덮고, 부분 선택은 @p current 위에 덮습니다.
         * @details 지워진 매개변수와 항목은 버리고 보고한다. 새 매개변수는 기본값 그대로 둔다. 지워졌거나 잠긴 아이템은 그 슬롯의 기본값(기준 프리셋의 것, 없으면 빈 슬롯)으로
         *          되돌리고 보고한다. 이 함수는 실패하지 않는다. 콘텐츠가 바뀌어도 플레이어 데이터는 열려야 하기 때문이다.
         */
        static void applySelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                    const IAppearanceUnlockQuery* pUnlockQuery, CharacterAppearanceSpec& outSpec, AppearanceSelectionReport& outReport );
        /**
         * @brief 전체 선택을 데이터 프리셋 하나(`CharacterAppearanceDef`)로 바꿉니다. 기준 프리셋이 부모이고, 선택이 정한 필드(몸 종류, 체형, 얼굴, 꾸미기 값, 슬롯)만 적습니다(유니티 프리팹 변형).
         * @details 값의 종류(슬라이더, 색, 고르기)는 선택의 스키마가 정한다. 스키마에 없는 매개변수, 부분 선택, 아이템 인스턴스 꾸미기 값은 프리셋이 나타낼 수 없어
         *          false 와 @p outReason 을 돌려준다(조용히 버리지 않는다). 시드는 프리셋에 없다. 부모의 후보는 그 프리셋을 쓰는 컴포넌트의 시드로 뽑는다.
         */
        [[nodiscard]] static bool makePresetDef( const AppearanceDatabase& database, const AppearanceSelection& selection, const hashed_string& presetID,
                                                 CharacterAppearanceDef& outDef, string& outReason );
        /** @brief 입혀 보기입니다. 펼쳐서 해석만 하고 아무것도 바꾸지 않습니다. */
        static void previewSelection( const AppearanceDatabase& database, const AppearanceSelection& selection, const CharacterAppearanceSpec& current,
                                      const IAppearanceUnlockQuery* pUnlockQuery, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceSelectionCodec
     * @brief 선택을 비트로 씁니다. 네트워크 외형 동기화에 쓰며, 받는 쪽이 펼쳐서 해석하면 같은 해시가 나옵니다. 공유 코드와 세이브도 이것을 씁니다.
     * @details 슬라이더는 스키마 범위의 16 비트, 색은 성분마다 8 비트, 피해는 8 비트로 쓴다. 해석기가 쓰는 정밀도와 같으므로 손실이 없다. 값마다 종류를 2 비트로
     *          붙이므로, 받는 쪽이 알지 못하는 매개변수도 길이를 알고 건너뛸 수 있다.
     */
    struct SW_GF_API AppearanceSelectionCodec
    {
        static void writeSelection( BitWriter& writer, const AppearanceSelection& selection, const AppearanceDatabase& database );
        /** @brief 읽습니다. 비트가 모자라거나 개수가 비정상이면 false 입니다. */
        [[nodiscard]] static bool readSelection( BitReader& reader, const AppearanceDatabase& database, AppearanceSelection& outSelection );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceShareCode
     * @brief 공유 코드입니다. 버전 1 바이트, 선택 비트, CRC32 를 base64url(패딩 없음)로 쓴 짧은 문자열입니다. 버전이 다르거나 체크섬이 틀리면 거절합니다(정확히 같은 판만 읽습니다).
     */
    struct SW_GF_API AppearanceShareCode
    {
        static constexpr uint32 kVersion = 1;

        static string encode( const AppearanceSelection& selection, const AppearanceDatabase& database );
        /** @brief 공유 코드를 해석합니다. 코드가 망가졌거나(base64 오류, 체크섬 불일치) 이 빌드가 모르는 버전이면 false 를 반환하고, @p pOutReason 에 이유를 씁니다. */
        [[nodiscard]] static bool decode( string_view code, const AppearanceDatabase& database, AppearanceSelection& outSelection, string* pOutReason = nullptr );
    };
} // namespace sw
