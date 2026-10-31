/**
 * @file UserAppearancePresetStore.h
 * @brief 플레이어가 저장한 외형 프리셋입니다. 이름 붙은 슬롯, 즐겨찾기, 썸네일 슬롯, 부분 프리셋을 세이브에 싣습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

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
        string              _thumbnailSlot{}; ///< 미리보기 이미지(PNG)를 둔 로컬 저장소 슬롯 이름(같은 저장소). 비면 없다
        uint8               _bFavorite{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 세이브의 프리셋 한 줄입니다(`UserAppearancePresetFile` 의 원소). 내용은 공유 코드(`AppearanceShareCode`) 한 줄입니다. */
    REFLECT()
    struct SW_GF_API UserAppearancePresetRecord
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name; ///< 플레이어가 적은 이름

        PROPERTY( Required )
        string _code; ///< 선택의 공유 코드

        PROPERTY()
        string _thumbnailSlot; ///< 미리보기 이미지(PNG)를 둔 로컬 저장소 슬롯 이름

        PROPERTY()
        bool _bFavorite{ false }; ///< 즐겨찾기
    };
} // namespace sw

namespace sw
{
    /** @brief 외형 프리셋 세이브 본문(JSON)입니다. 판(`_formatVersion`)이 `UserAppearancePresetStore::kVersion` 이 아니면 읽지 않습니다. */
    REFLECT()
    struct SW_GF_API UserAppearancePresetFile
    {
        REFLECT_BODY();

        PROPERTY( Required )
        int32 _formatVersion{ 0 }; ///< 세이브 판

        PROPERTY()
        vector<UserAppearancePresetRecord> _listPreset; ///< 저장한 순서의 프리셋
    };
} // namespace sw

namespace sw
{
    /**
     * @class UserAppearancePresetStore
     * @brief 플레이어 프리셋 목록을 리플렉션 JSON(`UserAppearancePresetFile`)으로 읽고 씁니다. 로컬 저장 슬롯의 본문에 해당합니다. 프리셋 내용은 공유 코드(`AppearanceShareCode`) 한 줄로 싣습니다.
     *        세이브와 공유가 같은 코드이고 콘텐츠가 바뀌어도 읽힙니다. 펼칠 때 `AppearanceSelectionUtil::applySelection` 이 지워진 것을 보고합니다.
     * @details 판 3 부터 JSON 이다. 판 1 · 2 의 `key=value` 세이브와 판 3(썸네일 경로)은 출시 전 데이터라 읽는 길을 두지 않았다 — 읽기가 실패로 알린다.
     *          현재 판보다 새 파일도 읽지 않는다. 망가진 프리셋(읽지 못한 공유 코드)은 경고를 남기고 건너뛴다.
     */
    class SW_GF_API UserAppearancePresetStore : public SaveGame
    {
    public:
        static constexpr int32 kVersion = 4; ///< 3: 리플렉션 JSON, 4: 썸네일을 경로 대신 슬롯 이름으로(이전 판은 읽지 않는다)

        UserAppearancePresetStore();

        /** @brief 프리셋 내용을 공유 코드로 쓰고 읽을 때 참조하는 외형 데이터입니다. 소유하지 않고 참조만 합니다. */
        void setDatabase( const AppearanceDatabase* pDatabase ) { _pDatabase = pDatabase; }

        /** @brief 이름 슬롯에 저장합니다. 같은 이름이 있으면 내용만 바꾸고, 즐겨찾기와 썸네일은 그대로 둡니다. 슬롯 번호를 반환합니다. */
        int32              savePreset( string_view name, const AppearanceSelection& selection );
        [[nodiscard]] bool removePreset( string_view name );
        /** @brief 이름을 바꿉니다. 새 이름이 이미 있으면 false 입니다. */
        [[nodiscard]] bool renamePreset( string_view fromName, string_view toName );
        [[nodiscard]] bool setFavorite( string_view name, bool bFavorite );
        [[nodiscard]] bool setThumbnailSlot( string_view name, string_view thumbnailSlot );
        void               clear() { _listPreset.clear(); }

        const UserAppearancePreset*         findPresetByName( string_view name ) const;
        const vector<UserAppearancePreset>& getPresets() const { return _listPreset; }
        /** @brief 즐겨찾기만 저장한 순서대로 모읍니다. */
        void collectFavorites( vector<const UserAppearancePreset*>& outListPreset ) const;

        [[nodiscard]] bool writeBytes( vector<uint8>& outBytes ) const override;
        [[nodiscard]] bool readBytes( const uint8* pData, size_t size ) override;
        string             saveToText() const;
        /** @brief 텍스트에서 읽습니다. JSON 이 아니거나 판이 지금과 다르면 false 이고, 망가진 프리셋 줄은 경고를 남기고 건너뜁니다. */
        [[nodiscard]] bool loadFromText( string_view text );

    private:
        UserAppearancePreset* findMutablePreset( string_view name );

        vector<UserAppearancePreset> _listPreset;
        const AppearanceDatabase*    _pDatabase;
    };
} // namespace sw
