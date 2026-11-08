/**
 * @file AppearanceEditModel.h
 * @brief 외형 선택(`AppearanceSelection`) 편집 모델입니다 — 슬롯별 아이템 입히기, 체형 · 얼굴 · 몸 종류, 꾸미기 값(슬라이더 · 색 · 고르기), 시드, 되돌리기, 프리셋 저장소로 내보내기.
 * @details 편집 대상은 "기준 프리셋 + 꾸미기 값 + 장비 구성" 이라는 원본 데이터이다(펼친 결과 `ResolvedAppearance` 가 아니다). 이 모델은 콘텐츠 데이터베이스를 모른다 —
 *          아이템 · 슬롯 이름이 실제로 있는지는 호출하는 쪽(패널)이 슬롯 표와 세트 목록으로 걸러서 넘긴다. 펼친 결과를 보는 일(규칙 설명, 입혀 보기)은
 *          `AppearanceSelection::previewSelection` 이 하며 이 모델 밖이다. ImGui 와 씬을 모릅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"

namespace sw
{
    struct CustomizationParamDef;

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
        /** @brief 선택이 바뀔 때마다 오르는 번호입니다. 입혀 보기 결과를 다시 계산할지 가릴 때 씁니다(바뀌었을 수 있는 쪽으로 기웁니다). */
        uint32 getRevision() const { return _revision; }
        /** @brief 지금 상태를 "저장된 상태" 로 맞춥니다. 저장 뒤에 부릅니다. */
        void markSaved() { _state.markSaved( _selection ); }
        /** @brief 저장이 끝난 선택 @p saved 를 "저장된 상태" 로 맞춥니다 - 비동기 저장은 맡길 때의 선택을 넘긴다(그 사이 편집은 미저장으로 남는다). */
        void markSaved( const AppearanceSelection& saved ) { _state.markSaved( saved ); }
        /** @brief 저장된 상태와 다르면 true 입니다. */
        bool isDirty() const;

        // --- 편집 ----------------------------------------------------------------------------------------------------
        // 바뀌는 것이 없으면(이미 같은 값) false 를 반환하고 되돌리기 기록도 남기지 않습니다.
        // bContinuous 가 true 이면 같은 대상의 연속 편집(슬라이더 끌기)을 되돌리기 한 단계로 합칩니다. 끌기가 끝나면 `endContinuousEdit` 를 부릅니다.

        /** @brief 슬롯에 아이템을 입힙니다. @p itemID 가 비면 그 슬롯을 "비움" 으로 둡니다. 슬롯 요청이 없으면 만듭니다. */
        bool setSlotItem( const hashed_string& slot, const hashed_string& itemID );
        /** @brief 슬롯 요청 자체를 지웁니다(기준 프리셋의 기본값으로 돌아갑니다). 없으면 false 입니다. */
        [[nodiscard]] bool removeSlot( const hashed_string& slot );
        /** @brief 슬롯을 플레이어가 숨긴 것으로 둡니다(투구 숨기기 — 외형 · 몸 채널만 빠진다). 슬롯 요청이 없으면 false 입니다. */
        bool setSlotHidden( const hashed_string& slot, bool bHidden );
        /** @brief 피부 층을 맨 위(목록 끝)에 더합니다. */
        bool addSkinLayer( const hashed_string& layer, const float4& color, float32 opacity );
        /** @brief 피부 층 하나를 지웁니다. 번호가 범위 밖이면 false 입니다. */
        [[nodiscard]] bool removeSkinLayer( uint32 index );
        /** @brief 피부 층을 @p delta 칸 옮깁니다(−1 아래로 · +1 위로 — 목록 뒤가 위다). 끝을 넘으면 false 입니다. */
        bool moveSkinLayer( uint32 index, int32 delta );
        /** @brief 피부 층 하나의 값(색 · 불투명도 · 배치 · 대칭 · 숨김)을 바꿉니다. */
        bool setSkinLayer( uint32 index, const SkinLayerValue& value, bool bContinuous );
        bool setNumber( const hashed_string& parameter, float32 value, bool bContinuous );
        bool setColor( const hashed_string& parameter, const float4& color, bool bContinuous );
        bool setOption( const hashed_string& parameter, const hashed_string& option );
        /** @brief @p other 가 @p param 과 같은 대칭 그룹의 다른 매개변수(같은 종류)이면 true 입니다. */
        static bool isSymmetryPartner( const CustomizationParamDef& param, const CustomizationParamDef& other );
        /** @brief 값을 정하고 대칭 그룹(좌우 귀)의 짝도 같은 값으로 바꿉니다. 짝은 자기 범위로 자릅니다. */
        void setNumberSymmetric( const vector<CustomizationParamDef>& listParameter, const CustomizationParamDef& param, float32 value, bool bContinuous );
        /** @brief 색을 정하고 대칭 그룹의 짝도 같은 색으로 바꿉니다. */
        void setColorSymmetric( const vector<CustomizationParamDef>& listParameter, const CustomizationParamDef& param, const float4& color, bool bContinuous );
        /** @brief 꾸미기 값을 지웁니다(스키마 기본값으로 돌아갑니다). 없으면 false 입니다. */
        [[nodiscard]] bool removeValue( const hashed_string& parameter );
        bool               setBodyType( const hashed_string& bodyType );
        bool               setBodyShape( const hashed_string& bodyShape );
        bool               setFace( const hashed_string& face );
        bool               setBasePreset( const hashed_string& basePresetID );
        bool               setSeed( uint32 seed );
        /** @brief 연속 편집을 끝냅니다. 다음 편집은 새 되돌리기 단계를 만듭니다. */
        void endContinuousEdit() { _state.endCoalescing(); }

        /** @brief 슬롯에 입힌 아이템을 반환합니다. 슬롯 요청이 없거나 비움이면 빈 이름입니다. */
        hashed_string findSlotItem( const hashed_string& slot ) const;

        // --- 되돌리기 ------------------------------------------------------------------------------------------------

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _state.canUndo(); }
        bool               canRedo() const { return _state.canRedo(); }

        // --- 저장 ----------------------------------------------------------------------------------------------------

        // 프리셋 저장소를 슬롯에 쓰고 읽는 일은 문서가 한다(`CharacterEditorModel::savePreset` - 쓰기가 끝난 뒤 `markSaved( 맡긴 선택 )`).
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

    private:
        AppearanceSelection                                           _selection;
        EditorDocumentState<AppearanceSelection, AppearanceSelection> _state; ///< 저장한 선택 · 되돌리기
        uint32                                                        _revision;
    };
} // namespace sw::editor
