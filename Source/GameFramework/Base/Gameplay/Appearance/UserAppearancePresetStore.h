/**
 * @file UserAppearancePresetStore.h
 * @brief 플레이어가 저장한 외형 프리셋입니다. 이름 붙은 슬롯, 즐겨찾기, 썸네일 경로, 부분 프리셋을 세이브에 싣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Framework/Save/SaveGame.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceDatabase;

    /** @brief 플레이어 프리셋 하나입니다. 내용은 선택(`AppearanceSelection`)이며, 부분 프리셋이면 그 카테고리만 담습니다. */
    struct UserAppearancePreset
    {
        AppearanceSelection _selection{};
        string              _name{};
        string              _thumbnailPath{}; ///< 미리보기 이미지 경로. 렌더링은 나중에 붙이고 지금은 필드만 둔다
        uint8               _bFavorite{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UserAppearancePresetStore
     * @brief 플레이어 프리셋 목록을 `key=value` 텍스트로 읽고 씁니다. 로컬 저장 슬롯의 본문에 해당합니다. 프리셋 내용은 공유 코드(`AppearanceShareCode`) 한 줄로 싣습니다.
     *        세이브와 공유가 같은 형식이고 콘텐츠가 바뀌어도 읽힙니다. 펼칠 때 `AppearanceSelectionUtil::applySelection` 이 지워진 것을 보고합니다.
     * @details **플레이어 세이브는 이미 배포된 데이터이므로 옛 형식을 읽는다.** 엔진 데이터에 적용하는 "옛 이름과 옛 형식 리더를 두지 않는다" 는 규칙은 여기에 해당하지
     *          않는다. 형식 버전(`formatVersion`)마다 다음 버전으로 올리는 변환 단계를 두고, 읽을 때 현재 버전까지 차례로 올린다. 현재 버전보다 새로운 파일은 읽지 않는다.
     *          버전 1은 `version`, `count`, `preset{n}.name`, `preset{n}.code`, `favorites=이름,이름` 을 쓴다. 버전 2는 `formatVersion`, `presetCount`,
     *          `preset{n}.name`, `.code`, `.favorite`, `.thumbnail` 을 쓴다.
     */
    class SW_GF_API UserAppearancePresetStore : public SaveGame
    {
    public:
        static constexpr int32 kVersion = 2;

        UserAppearancePresetStore();

        /** @brief 프리셋 내용을 공유 코드로 쓰고 읽을 때 참조하는 외형 데이터입니다. 소유하지 않고 참조만 합니다. */
        void setDatabase( const AppearanceDatabase* pDatabase ) { _pDatabase = pDatabase; }

        /** @brief 이름 슬롯에 저장합니다. 같은 이름이 있으면 내용만 바꾸고, 즐겨찾기와 썸네일은 그대로 둡니다. 슬롯 번호를 반환합니다. */
        int32              savePreset( string_view name, const AppearanceSelection& selection );
        [[nodiscard]] bool removePreset( string_view name );
        /** @brief 이름을 바꿉니다. 새 이름이 이미 있으면 false 입니다. */
        [[nodiscard]] bool renamePreset( string_view fromName, string_view toName );
        [[nodiscard]] bool setFavorite( string_view name, bool bFavorite );
        [[nodiscard]] bool setThumbnailPath( string_view name, string_view thumbnailPath );
        void               clear() { _listPreset.clear(); }

        const UserAppearancePreset*         findPresetByName( string_view name ) const;
        const vector<UserAppearancePreset>& getPresets() const { return _listPreset; }
        /** @brief 즐겨찾기만 저장한 순서대로 모읍니다. */
        void collectFavorites( vector<const UserAppearancePreset*>& outListPreset ) const;

        [[nodiscard]] bool writeBytes( vector<uint8>& outBytes ) const override;
        [[nodiscard]] bool readBytes( const uint8* pData, size_t size ) override;
        string             saveToText() const;
        /** @brief 텍스트에서 읽습니다. 옛 버전은 올리고, 망가진 프리셋 줄은 경고를 남기고 건너뛰며, 버전이 지금보다 새로우면 false 입니다. */
        [[nodiscard]] bool loadFromText( string_view text );

    private:
        UserAppearancePreset* findMutablePreset( string_view name );

        vector<UserAppearancePreset> _listPreset;
        const AppearanceDatabase*    _pDatabase;
    };
} // namespace sw
