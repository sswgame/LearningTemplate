/**
 * @file UserSettingsViewModel.h
 * @brief 사용자 설정 창의 ViewModel — 범주 · 설정 줄을 모으고(`gather`) 보류 값 바꾸기 · 적용 · 되돌리기 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 게임 메뉴가 쓸 바인딩 API(`UserSettingsManager`)만 씁니다 — 값은 `getValue`(보류 우선), 바꾸기는 `setPending*`, 회색은 `isSettingEnabled`.
 *          보류 값은 사용자 설정 파일(문서가 아니라 사용자 환경)이라 에디터 되돌리기를 거치지 않는다 — 적용 전에는 Revert, 적용 뒤에는 화면 확인 카운트다운이 되돌린다(언리얼 GameUserSettings 와 같다).
 *          범주는 모두 모은다(설정 수가 수십이다): 탭을 고른 프레임에 바로 그 범주의 줄을 그린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw::editor
{
    /** @brief 고를 수 있는 값 하나입니다(값만). */
    struct UserSettingOptionRow
    {
        string _value; ///< 저장 값(`borderless`)
        string _label; ///< 스키마의 현지화 이름(없으면 값)
    };

    /** @brief 설정 하나의 줄입니다(값만). */
    struct UserSettingRow
    {
        string                       _id;             ///< 설정 id(`display.windowMode`)
        string                       _label;          ///< 스키마의 현지화 이름(없으면 id)
        string                       _descriptionKey; ///< 설명 현지화 키(비면 툴팁 없음)
        string                       _value;          ///< 지금 값 글(보류 우선)
        string                       _previewLabel;   ///< 선택지 종류의 지금 값 이름
        string                       _glyph;          ///< 키 바인딩 종류의 키보드 · 마우스 글리프
        vector<UserSettingOptionRow> _listOption;     ///< 선택지 종류의 값들
        float64                      _minValue{ 0.0 };
        float64                      _maxValue{ 0.0 };
        float32                      _floatValue{ 0.0f };
        int32                        _intValue{ 0 };
        UserSettingType              _type{ UserSettingType::String };
        uint8                        _bBoolValue{ SW_FALSE };
        uint8                        _bPending{ SW_FALSE }; ///< 적용을 기다리는 값이다
        uint8                        _bEnabled{ SW_FALSE }; ///< 조건(`isSettingEnabled`)이 켜 두었다
        uint8                        _bHasKey{ SW_FALSE };  ///< 형 있는 핸들이 풀렸다(못 풀면 값 칸을 그리지 않는다 — `resolveKey` 가 오류를 남긴다)
    };

    /** @brief 범주 탭 하나입니다(값만). */
    struct UserSettingsCategoryTab
    {
        string                 _id;
        string                 _label; ///< `UserSettingsViewModel::getCategoryLabel`
        vector<UserSettingRow> _listRow;
    };

    /** @brief 사용자 설정 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct UserSettingsViewState
    {
        vector<UserSettingsCategoryTab> _listCategory;               ///< 비면 스키마를 읽지 않았다
        float32                         _confirmSecondsLeft{ 0.0f }; ///< 화면 확인 카운트다운의 남은 초
        uint32                          _lastRejectedCount{ 0 };     ///< 마지막 적용에서 대상이 거절한 설정 수
        uint8                           _bPending{ SW_FALSE };
        uint8                           _bRestartRequired{ SW_FALSE };
        uint8                           _bAwaitingConfirm{ SW_FALSE };
    };

    /** @brief 사용자 설정 창의 ViewModel 입니다. */
    class UserSettingsViewModel
    {
    public:
        UserSettingsViewModel();

        /** @brief 범주 탭에 보이는 이름입니다(스키마 `text` 현지화 키, 없으면 id). 자체 테스트가 탭을 이 이름으로 찾는다. */
        [[nodiscard]] static const utf8* getCategoryLabel( const UserSettingCategoryDef& category );

        /** @brief 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). 설정 매니저가 없거나 스키마가 비면 범주가 비어 있습니다. */
        void gather( UserSettingsViewState& outState ) const;

        /** @brief 설정 하나의 보류 값을 글로 바꿉니다(선택지 · 글 종류). */
        void setPendingValue( const string& id, const string& value );
        void setPendingBool( const string& id, bool bValue );
        void setPendingInt( const string& id, int32 value );
        void setPendingFloat( const string& id, float32 value );
        /** @brief 키 바인딩 슬롯 글을 바꿉니다. 겹치면 맞바꿉니다. */
        void setPendingBinding( const string& id, const string& slot );
        /** @brief 보류 값을 적용합니다. 거절된 수를 기억합니다(이름은 매니저가 경고로 남긴다). */
        void applyPending();
        void revertPending();
        /** @brief 범주 @p categoryID 의 설정을 기본값으로 보류합니다. */
        void resetCategory( const string& categoryID );
        /** @brief 화면 확인 카운트다운에서 바꾼 값을 유지합니다. */
        void confirmChanges();
        /** @brief 화면 확인 카운트다운을 끝내 지금 되돌립니다. */
        void revertConfirmNow();

    private:
        uint32 _lastRejectedCount;
    };
} // namespace sw::editor
