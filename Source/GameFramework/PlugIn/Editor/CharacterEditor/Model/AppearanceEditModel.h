/**
 * @file AppearanceEditModel.h
 * @brief 외형 선택(`AppearanceSelection`) 편집 모델입니다 — 슬롯별 아이템 입히기, 체형 · 얼굴 · 몸 종류, 꾸미기 값(슬라이더 · 색 · 고르기), 시드, 되돌리기, 프리셋 저장소로 내보내기.
 * @details 편집 대상은 "기준 프리셋 + 꾸미기 값 + 장비 구성" 이라는 원본 데이터이다(펼친 결과 `ResolvedAppearance` 가 아니다). 이 모델은 콘텐츠 데이터베이스를 모른다 —
 *          아이템 · 슬롯 이름이 실제로 있는지는 호출하는 쪽(패널)이 슬롯 표와 세트 목록으로 걸러서 넘긴다. 펼친 결과를 보는 일(규칙 설명, 입혀 보기)은
 *          `AppearanceSelectionUtil::previewSelection` 이 하며 이 모델 밖이다. ImGui 와 씬을 모릅니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/EditSnapshotHistory.h"

namespace sw
{
    struct ResolvedAppearance;

    class AppearanceDatabase;
    class UserAppearancePresetStore;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AppearanceEditModel
     * @brief 외형 선택 하나와 그 편집 기록입니다.
     */
    class AppearanceEditModel
    {
    public:
        AppearanceEditModel();

        /** @brief 선택을 통째로 바꿉니다. 기록을 지우고 "저장된 상태" 로 맞춥니다(문서를 열 때). */
        void                       setSelection( const AppearanceSelection& selection );
        const AppearanceSelection& getSelection() const { return _selection; }
        /** @brief 지금 상태를 "저장된 상태" 로 맞춥니다. 저장 뒤에 부릅니다. */
        void markSaved() { _saved = _selection; }
        /** @brief 저장된 상태와 다르면 true 입니다. */
        bool isDirty() const;

        // --- 편집 ----------------------------------------------------------------------------------------------------
        // 바뀌는 것이 없으면(이미 같은 값) false 를 반환하고 되돌리기 기록도 남기지 않습니다.
        // bContinuous 가 true 이면 같은 대상의 연속 편집(슬라이더 끌기)을 되돌리기 한 단계로 합칩니다. 끌기가 끝나면 `endContinuousEdit` 를 부릅니다.

        /** @brief 슬롯에 아이템을 입힙니다. @p itemID 가 비면 그 슬롯을 "비움" 으로 둡니다. 슬롯 요청이 없으면 만듭니다. */
        bool setSlotItem( const hashed_string& slot, const hashed_string& itemID );
        /** @brief 슬롯 요청 자체를 지웁니다(기준 프리셋의 기본값으로 돌아갑니다). 없으면 false 입니다. */
        [[nodiscard]] bool removeSlot( const hashed_string& slot );
        bool               setNumber( const hashed_string& parameter, float32 value, bool bContinuous );
        bool               setColor( const hashed_string& parameter, const float4& color, bool bContinuous );
        bool               setOption( const hashed_string& parameter, const hashed_string& option );
        /** @brief 꾸미기 값을 지웁니다(스키마 기본값으로 돌아갑니다). 없으면 false 입니다. */
        [[nodiscard]] bool removeValue( const hashed_string& parameter );
        bool               setBodyType( const hashed_string& bodyType );
        bool               setBodyShape( const hashed_string& bodyShape );
        bool               setFace( const hashed_string& face );
        bool               setBasePreset( const hashed_string& basePresetID );
        bool               setSeed( uint32 seed );
        /** @brief 연속 편집을 끝냅니다. 다음 편집은 새 되돌리기 단계를 만듭니다. */
        void endContinuousEdit() { _history.endCoalescing(); }

        /** @brief 슬롯에 입힌 아이템을 반환합니다. 슬롯 요청이 없거나 비움이면 빈 이름입니다. */
        hashed_string findSlotItem( const hashed_string& slot ) const;

        // --- 되돌리기 ------------------------------------------------------------------------------------------------

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _history.canUndo(); }
        bool               canRedo() const { return _history.canRedo(); }

        // --- 저장 ----------------------------------------------------------------------------------------------------

        /**
         * @brief 지금 선택을 프리셋 저장소의 @p name 슬롯에 넣습니다(같은 이름이면 내용만 바꿉니다). 성공하면 "저장된 상태" 로 맞춥니다.
         * @details 저장소를 파일에 쓰는 일(세이브)은 호출하는 쪽의 일이다. 이름은 저장소가 정리한다(`UserAppearancePresetStore`).
         */
        [[nodiscard]] bool saveToPresetStore( UserAppearancePresetStore& store, string_view name );

        /**
         * @brief 지금 선택을 프리셋 저장소에 넣고 저장소 전체를 @p absolutePath 에 씁니다(`UserAppearancePresetStore::saveToText`). 쓰기에 실패하면 false 이고 저장 상태는 그대로입니다.
         * @details 저장소는 프리셋 내용을 공유 코드로 싣기 때문에 `setDatabase` 로 외형 데이터를 먼저 정해 둬야 한다(없으면 코드가 비어 다시 읽을 수 없다).
         */
        [[nodiscard]] bool saveToPresetFile( UserAppearancePresetStore& store, string_view name, string_view absolutePath );
        /** @brief 파일에서 저장소를 읽습니다. 파일이 없거나 형식이 틀리면 false 이고 저장소는 그대로입니다. */
        [[nodiscard]] static bool loadPresetFile( UserAppearancePresetStore& store, string_view absolutePath );
        /** @brief 저장소의 프리셋 하나를 열어 선택을 바꿉니다(기록을 지우고 "저장된 상태" 로 맞춥니다). 이름이 없으면 false 입니다. */
        [[nodiscard]] bool loadFromPreset( const UserAppearancePresetStore& store, string_view name );

        // --- 콘텐츠 데이터베이스를 보는 도우미 -----------------------------------------------------------------------

        /**
         * @brief 슬롯에 입힐 수 있는 아이템 id 를 모읍니다 — 아이템 카탈로그의 장비 중 슬롯이 받는 종류(`accept`, 비면 슬롯 이름)와 같은 것. 카탈로그가 없으면 비어 있습니다.
         */
        static void collectSlotItems( const AppearanceDatabase& database, const hashed_string& slot, vector<hashed_string>& outListItem );
        /** @brief 세트의 조각을 지금 몸 종류(`_bodyType`)에 맞춰 슬롯에 입힙니다(조각의 첫 아이템). 되돌리기 한 단계입니다. 세트가 없거나 바뀌는 것이 없으면 false 입니다. */
        bool equipSet( const AppearanceDatabase& database, const hashed_string& setID );
        /**
         * @brief 지금 선택을 콘텐츠에 맞춰 펼쳐 봅니다(입혀 보기). 규칙 설명은 @p outResolved 의 `_listTrace`, 버려지거나 기본으로 돌아간 것은 @p outReport 에 있습니다.
         * @details 아무것도 바꾸지 않습니다. 몸 종류 · 규칙 판정에 쓰는 현재 외형(`current`)은 비어 있다고 본다 — 전체 선택은 기준 프리셋이 시드이다.
         */
        void previewResolved( const AppearanceDatabase& database, ResolvedAppearance& outResolved, AppearanceSelectionReport& outReport ) const;

    private:
        void          recordUndo( string_view coalesceKey );
        static string makeKey( string_view kind, const hashed_string& target );

        AppearanceSelection                      _selection;
        AppearanceSelection                      _saved;
        EditSnapshotHistory<AppearanceSelection> _history;
    };
} // namespace sw::editor
