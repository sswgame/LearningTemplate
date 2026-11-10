/**
 * @file InspectorViewModel.h
 * @brief 인스펙터 창의 ViewModel 입니다 — 선택에서 보일 오브젝트를 고르고(잠금 · 다중 · 미리보기), 머리(이름 · 활성 · 부모 · 태그) · 프리팹 줄 · 씬 인스턴스 줄 ·
 *        컴포넌트 카드 · 다중 선택의 공통 컴포넌트 · 즐겨찾기 대상을 값으로 모으고, 오브젝트 편집을 명령으로 받습니다.
 *        ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 절 '프로퍼티 그리드 편집 모델' 6). 프로퍼티는 그리드가 대상 id 로 그린다(`EditorPropertyGridTarget`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Panels/Inspector/Model/InspectorFavorites.h"
#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    struct EditorCommandTarget;

    class IInspectorComponent;
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 인스펙터가 이번 프레임에 그리는 것입니다. */
    enum class InspectorContent : uint8
    {
        NothingSelected, ///< 고른 것이 없다
        NoActiveScene,   ///< 활성 씬이 없다
        SelectionGone,   ///< 고른(잠근) 오브젝트가 사라졌다 — 이번 gather 가 선택을 풀었다
        Single,          ///< 오브젝트 하나(메인 씬 — 잠금 포함)
        Multi,           ///< 둘 이상 선택(메인 씬)
        Preview,         ///< 미리보기 창의 선택
        PreviewEmpty,    ///< 미리보기 창이 겨눠졌지만 고른 것이 없다
    };

    /** @brief 컴포넌트 카드 한 장입니다(값만). */
    struct InspectorComponentCard
    {
        uint64        _componentID{ 0 };
        string        _title;           ///< 아이콘 + 컴포넌트 이름
        hashed_string _typeName;        ///< 리플렉션 타입 이름(확장 조회 · ImGui id). 타입이 없으면 비어 있다
        string        _typeDisplayName; ///< 타입 표시 이름(접는 머리 · 되돌리기 이름)
        uint8         _bSelfActive : 1; ///< 컴포넌트 자기 비트(계층 활성은 빼고)
        uint8         _bAccent     : 1; ///< 씬 컴포넌트 — 카드 머리를 강조한다
        uint8         _bScrollTo   : 1; ///< 계층 창이 이 카드로 스크롤해 펼치라고 했다

        InspectorComponentCard();
    };

    /** @brief 씬이 선언한 데이터 레이어 한 줄입니다. */
    struct InspectorDataLayerRow
    {
        string _name;
        uint8  _bOn : 1; ///< 오브젝트가 이 레이어에 있다

        InspectorDataLayerRow();
    };

    /** @brief 컴포넌트 프리셋 파일 한 줄입니다. */
    struct InspectorPresetRow
    {
        string _file;        ///< 프리셋 파일 경로
        string _displayName; ///< 메뉴에 보일 이름
    };

    /** @brief 인스펙터 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct InspectorViewState
    {
        InspectorContent _content;
        uint64           _objectID; ///< 보이는 오브젝트(다중이면 주 선택)
        uint64           _lockedObjectID;
        string           _objectName;
        string           _parentName; ///< 부모 이름. 루트이면 비어 있다(`_bHasParent`)
        vector<string>   _listTag;
        // 프리팹 · 씬 인스턴스
        string                        _prefabPath;      ///< 프리팹 인스턴스의 프리팹 경로. 아니면 비어 있다
        string                        _outerName;       ///< 중첩 부품이면 바깥 인스턴스 이름
        string                        _outerPrefabPath; ///< 그 바깥 인스턴스의 프리팹 경로
        string                        _instanceLabel;   ///< 씬 인스턴스 앵커 이름(없으면 인스턴스 경로)
        vector<InspectorDataLayerRow> _listDataLayer;   ///< 씬이 선언한 데이터 레이어(앵커 멤버는 비운다)
        // 컴포넌트
        vector<InspectorComponentCard>   _listComponent;          ///< 단일 · 미리보기: 오브젝트의 컴포넌트, 다중: 공통 컴포넌트(주 선택의 것)
        vector<EditorPropertyGridTarget> _listFavorite;           ///< 즐겨찾기 구역에 그릴 대상(즐겨찾기가 있는 것만)
        string                           _previewSceneName;       ///< 미리보기 창 씬 이름
        uint32                           _selectedCount;          ///< 다중 선택 수
        uint32                           _unsharedCount;          ///< 다중 선택에서 공통이 아니라 빠진 컴포넌트 수
        uint8                            _bSceneEditsAllowed : 1; ///< 정지 상태 — 아니면 "Stop 에 되돌아간다" 안내를 그린다
        uint8                            _bActive            : 1; ///< 오브젝트 활성(다중이면 주 선택)
        uint8                            _bActiveMixed       : 1; ///< 다중 선택의 활성이 갈린다
        uint8                            _bHasParent         : 1;
        uint8                            _bHasOuter          : 1; ///< 중첩 부품이다(바깥 인스턴스 줄을 그린다)
        uint8                            _bInstanceRelated   : 1; ///< 씬 인스턴스의 앵커이거나 멤버다
        uint8                            _bHasObjectType     : 1; ///< 오브젝트 자신의 리플렉션 타입이 있다

        InspectorViewState();
    };

    /**
     * @class InspectorViewModel
     * @brief 인스펙터 창의 유일한 모델 창구입니다. `gather` 는 프레임 맨 위에서 한 번 부르고, 명령은 오브젝트 · 컴포넌트 id 를 받습니다.
     * @details 오브젝트 편집 명령은 되돌리기 한 단계와 씬 dirty 를 남긴다(`InspectorObjectEdit` · 씬 명령). 잠금 · 스크롤 소비는 보기 상태라 남기지 않는다.
     */
    class SW_EDITOR_API InspectorViewModel
    {
    public:
        InspectorViewModel();
        ~InspectorViewModel();
        InspectorViewModel( const InspectorViewModel& )            = delete;
        InspectorViewModel& operator=( const InspectorViewModel& ) = delete;

        /** @brief 이번 프레임의 값을 모읍니다. 처음 부를 때 즐겨찾기 파일을 읽습니다. 고른(잠근) 오브젝트가 사라졌으면 선택(잠금)을 풉니다. */
        void gather( InspectorViewState& outState );
        /** @brief 즐겨찾기 집합입니다. 그리드가 행 메뉴 토글 · 별 표시에 쓴다(`EditorPropertyGrid::setFavorites`). */
        InspectorFavorites& editFavorites() { return _favorites; }

        // ------------------------------------------------------------------------------
        // 보기 명령 — 되돌리기에 남기지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 자물쇠를 켜고 끕니다. 켜면 지금 선택한 오브젝트를 계속 보인다(유니티 Inspector 의 자물쇠). */
        void toggleLock();
        /** @brief 계층 창이 걸어 둔 "이 카드로 스크롤" 요청을 소비합니다. */
        void consumeScrollTo();

        // ------------------------------------------------------------------------------
        // 편집 명령 — 오브젝트 하나를 바꾸고 되돌리기 한 단계를 남긴다(플레이 중에는 바로 바꾼다)
        // ------------------------------------------------------------------------------
        void renameObject( uint64 objectID, string_view name );
        void setObjectActive( uint64 objectID, bool bActive );
        void detachObjectFromParent( uint64 objectID );
        void setComponentActive( uint64 objectID, uint64 componentID, bool bActive );
        void removeComponent( uint64 objectID, uint64 componentID );
        /** @brief 다중 선택 전부의 활성을 @p bActive 로 바꿉니다. */
        void setMultiActive( bool bActive );
        void applyToPrefab( uint64 objectID );
        void revertToPrefab( uint64 objectID );
        void unlinkPrefab( uint64 objectID );
        void applyToOuterPrefab( uint64 objectID );
        /** @brief 씬 인스턴스의 차이를 앵커의 `<Part>` 로 저장합니다(활성 씬 저장). */
        void applyInstanceOverrides();
        /** @brief 오브젝트 @p objectID 를 데이터 레이어 @p layerName 에 넣거나(@p bOn) 뺍니다. */
        void setDataLayer( uint64 objectID, string_view layerName, bool bOn );

        // ------------------------------------------------------------------------------
        // 다중 선택 그리드 — 그리드 대상의 `_onEdited` · `_isMixed` 가 부른다
        // ------------------------------------------------------------------------------
        /** @brief 공통 컴포넌트 @p index 를 이번에 그리는 것으로 둡니다. `kNoMultiComponent` 이면 풉니다. */
        void setCurrentMultiComponent( uint32 index ) { _currentMultiComponent = index; }
        /** @brief 고친 프로퍼티를 나머지 오브젝트의 같은 컴포넌트에 입힙니다. */
        void copyEditToOthers( const PropertyInfo& prop );
        /** @brief 그 프로퍼티 값이 오브젝트마다 다르면 true 입니다. */
        bool                    isMultiEditMixed( const PropertyInfo& prop );
        static constexpr uint32 kNoMultiComponent = 0xFFFFFFFFu;

        // ------------------------------------------------------------------------------
        // 프리셋
        // ------------------------------------------------------------------------------
        // 프리셋 하위 메뉴는 계층 창의 컴포넌트 행에서도 열린다 — 컴포넌트는 보이는 오브젝트가 아니라 맥락 명령 대상(값)이 가리킨다.
        /** @brief 대상 컴포넌트에 맞는 프리셋 줄을 채웁니다. 파일 목록은 배경 작업으로 모으고, 다 모이기 전에는 지난 목록을 씁니다. */
        void gatherPresets( const EditorCommandTarget& target, vector<InspectorPresetRow>& outListPreset );
        /** @brief 대상 컴포넌트를 이름 붙인 프리셋으로 저장합니다. 성공하면 true 이고 목록을 다시 모읍니다. */
        bool saveComponentPreset( const EditorCommandTarget& target, string_view presetName );
        /** @brief 프리셋 파일을 대상 컴포넌트에 입힙니다. */
        void loadComponentPreset( const EditorCommandTarget& target, string_view presetFile );
        /** @brief 맥락 명령 대상이 컴포넌트를 가리키면 true 입니다. */
        static bool hasTargetComponent( const EditorCommandTarget& target );

        // ------------------------------------------------------------------------------
        // 확장 창구 — 포인터를 받는 공용 위젯 · 확장 API 를 위해서만 쓴다
        // ------------------------------------------------------------------------------
        /** @brief 타입 이름 @p typeName 과 그 기반에 등록된 인스펙터 확장입니다(기반 → 파생 순서). */
        static const vector<IInspectorComponent*>& getInspectorsForType( const hashed_string& typeName );
        /** @brief 지금 보이는 오브젝트입니다(`EditorComponentMenu::drawAddComponentList` 가 포인터를 받는다). 없으면 nullptr 입니다. */
        GameObject* findShownObject() const;
        /** @brief 보이는 오브젝트의 컴포넌트 @p componentID 입니다(`IInspectorComponent` 가 포인터를 받는다). 없으면 nullptr 입니다. */
        Component* findShownComponent( uint64 componentID ) const;

    private:
        struct MultiEdit;

        InspectorFavorites      _favorites;
        unique_ptr<MultiEdit>   _pMultiEdit; ///< 다중 선택의 공통 컴포넌트(gather 마다 다시 모은다)
        EditorFileCollectJob    _presetJob;
        vector<string>          _listPresetFile;
        uint64                  _lockedObjectID;        ///< 자물쇠로 잠근 오브젝트. 0 이면 선택을 따라간다
        uint64                  _shownObjectID;         ///< 이번 gather 가 보인 오브젝트
        uint32                  _currentMultiComponent; ///< 그리드가 지금 그리는 공통 컴포넌트 번호
        EditorPropertyGridScope _shownScope;            ///< 보인 오브젝트의 씬
        uint8                   _bPresetDirty     : 1;
        uint8                   _bFavoritesLoaded : 1; ///< `_favorites` 를 저장 파일에서 읽었다
    };
} // namespace sw::editor
