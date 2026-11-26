#include "pch.h"

#include "Editor/Panels/MapCheck/Model/MapCheckViewModel.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Scene/EditorMapCheck.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Panels/Scene/EditorSceneViewUtil.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

// 맵 검사 결과의 안전한 고침(`ValidationPropertyEdit`)을 적용합니다 — 되돌리기 한 단계, 적용 뒤 그 오브젝트를 다시 검증합니다(ImGui 없음).
namespace sw::editor
{
    namespace
    {
        /**
         * @struct MapCheckFixCommands
         * @brief 검증 결과의 고침을 컴포넌트 프로퍼티에 입힙니다. 고침은 검증 함수가 데이터로 적은 것이고(`ValidationContext::addErrorWithFix`), 여기서는 그 값을 쓰기만 한다.
         * @details 대상은 결과의 출처 오브젝트(`_sourceID`)에서 타입(`_typeName`)이 같은 컴포넌트다. 여러 고침은 트랜잭션 하나라 되돌리기 한 번에 모두 돌아간다
         *          (언리얼 Map Check 의 일괄 수정 · Godot 구성 경고와 같은 자리). Play 중에는 고치지 않는다(씬 편집 거절).
         */
        struct MapCheckFixCommands
        {
            /** @brief 고칠 수 있는 결과면 true 입니다(고침이 있고 출처가 있다). 그 오브젝트가 지금 씬에 있는지는 보지 않는다. */
            static bool canFix( const ValidationIssue& issue );
            /**
             * @brief @p listIssue 중 고칠 수 있는 것을 모두 적용합니다. 되돌리기 한 단계(@p pUndoLabel)로 묶고, 고친 오브젝트를 다시 검증합니다.
             * @return 적용한 고침 수. 오브젝트 · 컴포넌트 · 프로퍼티를 찾지 못했거나 값을 쓰지 못한 고침은 경고하고 건너뛴다.
             */
            static uint32 applyFixes( GameObjectManager* pManager, const vector<ValidationIssue>& listIssue, const utf8* pUndoLabel );
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct MapCheckViewModelInternal
        {
            /** @brief 오브젝트를 화면에 담는 기본 반지름(m)입니다 — 더블클릭 초점. */
            static constexpr float32 kFocusRadius = 2.0f;
            /** @brief 씬을 열 때 띄우는 오류 토스트가 머무는 시간(초)입니다. */
            static constexpr float32 kToastSeconds = 6.0f;

            /** @brief 상태줄 항목이 프레임 사이에 기억하는 값입니다. 패널이 닫혀도 상태줄은 그려지므로 패널 멤버가 아니라 에디터 범위의 상태 저장소가 소유한다. */
            struct State
            {
                MapCheckCounts _statusCounts{};         ///< 상태줄 맵 검사 수 - 결과 번호가 바뀔 때만 다시 센다
                uint64         _statusRevisionKey{ 0 }; ///< `makeRevisionKey` — 씬이 바뀌어도 다시 센다
                bool           _bStatusCounted{ false };
                uint64         _toastSceneGeneration{ 0 }; ///< 씬을 열었을 때 한 번 띄우는 "Map Check: N errors" 토스트의 기준 씬이다(이름이 바뀌면 새 씬이다)
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief 활성 씬의 검증 결과 표입니다(표는 월드마다 하나 — `SceneValidationIssues`). 씬이 없으면 nullptr 입니다. */
            static const ValidationIssueLog* findActiveIssueLog()
            {
                const GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr ? &pManager->getSubsystem<SceneValidationIssues>().getLog() : nullptr;
            }

            /** @brief 표의 결과 번호에 활성 씬 세대를 붙인 열쇠입니다 — 씬이 바뀌면 새 표라 번호가 같아도 다시 모은다. 씬이 없으면 0 입니다. */
            static uint64 makeRevisionKey( const ValidationIssueLog* pLog )
            {
                if ( pLog == nullptr )
                    return 0;
                const uint64 generation = editor::getSubsystem<SceneManager>().getSceneGeneration();
                return ( generation << 32u ) ^ ( static_cast<uint64>( pLog->getRevision() ) + 1u );
            }

            /** @brief 활성 씬 표의 결과를 모읍니다. 씬이 없으면 비웁니다. */
            static void collectActiveIssues( const ValidationIssueLog* pLog, vector<ValidationIssue>& outListIssue )
            {
                outListIssue.clear();
                if ( pLog != nullptr )
                    pLog->collectIssues( outListIssue );
            }

            /** @brief 활성 씬이 바뀐 첫 프레임에 오류가 있으면 토스트를 한 번 띄웁니다. */
            static void notifyOnSceneChange( const MapCheckCounts& counts )
            {
                const SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
                const uint64        generation   = sceneManager.getSceneGeneration();
                if ( generation == getState()._toastSceneGeneration )
                    return;
                getState()._toastSceneGeneration = generation;
                if ( counts._errorCount == 0 )
                    return;
                const string message = SW_LOCKEYFORMAT( "EditorMapCheck", "SceneIssuesToast", TextArgumentList().addInteger( "errorCount", counts._errorCount ).addInteger( "warningCount", counts._warningCount ) );
                editor::getSubsystem<EditorContext>().getSubsystem<EditorNotificationManager>().push( "Map Check", message.c_str(), NotificationKind::Warning, kToastSeconds );
            }

            /** @brief 활성 씬의 오브젝트 @p objectID 입니다. 없으면 nullptr 입니다(지운 오브젝트 · 다른 씬의 결과). */
            static GameObject* findObject( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
            }
        };
    } // namespace

    MapCheckViewModel::MapCheckViewModel()
        : _listIssue{}
        , _counts{}
        , _search{}
        , _builtSearch{}
        , _builtRevisionKey{ 0 }
        , _rowsGeneration{ 0 }
        , _bShowErrors{ SW_TRUE }
        , _bShowWarnings{ SW_TRUE }
        , _bRowsDirty{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
    }

    void MapCheckViewModel::syncRows()
    {
        const ValidationIssueLog* pLog        = MapCheckViewModelInternal::findActiveIssueLog();
        const uint64              revisionKey = MapCheckViewModelInternal::makeRevisionKey( pLog );
        const bool                bChanged    = revisionKey != _builtRevisionKey || _builtSearch != _search;
        if ( bChanged == false && _bRowsDirty == SW_FALSE )
            return;
        vector<ValidationIssue> listIssue;
        MapCheckViewModelInternal::collectActiveIssues( pLog, listIssue );
        MapCheckFilter filter{};
        filter._search        = _search;
        filter._bShowErrors   = _bShowErrors == SW_TRUE;
        filter._bShowWarnings = _bShowWarnings == SW_TRUE;
        MapCheckCounts::populate( listIssue, filter, _listIssue, _counts );
        _builtRevisionKey = revisionKey;
        _builtSearch      = _search;
        _bRowsDirty       = SW_FALSE;
        ++_rowsGeneration;
    }

    void MapCheckViewModel::gather( MapCheckViewState& outState )
    {
        syncRows();
        if ( outState._rowsGeneration != _rowsGeneration || outState._listRow.size() != _listIssue.size() )
        {
            outState._listRow.clear();
            outState._fixableRowCount = 0;
            for ( const ValidationIssue& issue : _listIssue )
            {
                MapCheckRow row{};
                row._issue    = issue;
                row._bFixable = MapCheckFixCommands::canFix( issue ) ? SW_TRUE : SW_FALSE;
                outState._fixableRowCount += row._bFixable == SW_TRUE ? 1u : 0u;
                outState._listRow.push_back( std::move( row ) );
            }
            outState._rowsGeneration = _rowsGeneration;
        }
        outState._counts = _counts;

        const GameObject* pPrimary = editor::getMainSceneSelection().getPrimaryObject();
        for ( MapCheckRow& row : outState._listRow )
        {
            const GameObject* pObject = MapCheckViewModelInternal::findObject( row._issue._sourceID );
            row._bInActiveScene       = pObject != nullptr ? SW_TRUE : SW_FALSE;
            row._bFromCheck           = EditorMapCheck::isCheckSource( row._issue._sourceID ) ? SW_TRUE : SW_FALSE;
            row._bSelected            = pObject != nullptr && pObject == pPrimary ? SW_TRUE : SW_FALSE;
        }
    }

    void MapCheckViewModel::setSearch( string_view search )
    {
        if ( string_view{ _search } != search )
            _search = search;
    }

    void MapCheckViewModel::setShowErrors( const bool bShow )
    {
        _bShowErrors = bShow ? SW_TRUE : SW_FALSE;
        _bRowsDirty  = SW_TRUE;
    }

    void MapCheckViewModel::setShowWarnings( const bool bShow )
    {
        _bShowWarnings = bShow ? SW_TRUE : SW_FALSE;
        _bRowsDirty    = SW_TRUE;
    }

    uint32 MapCheckViewModel::applyAllFixes()
    {
        return MapCheckFixCommands::applyFixes( editor::getActiveObjectManager(), _listIssue, "Map Check: Fix All" ); // 고친 결과는 표에서 빠지고 다음 gather 가 줄을 다시 만든다
    }

    uint32 MapCheckViewModel::applyFix( const ValidationIssue& issue )
    {
        return MapCheckFixCommands::applyFixes( editor::getActiveObjectManager(), { issue }, issue._fix._description.empty() ? "Map Check: Fix" : issue._fix._description.c_str() );
    }

    bool MapCheckViewModel::selectObject( const uint64 objectID )
    {
        GameObject* pObject = MapCheckViewModelInternal::findObject( objectID );
        if ( pObject == nullptr )
            return false;
        editor::getMainSceneSelection().selectObject( pObject );
        return true;
    }

    void MapCheckViewModel::focusObject( const uint64 objectID )
    {
        const GameObject*     pObject         = MapCheckViewModelInternal::findObject( objectID );
        const SceneComponent* pSceneComponent = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
        if ( pSceneComponent != nullptr )
            (void)EditorSceneViewUtil::focusOn( pSceneComponent->getWorldPosition(), MapCheckViewModelInternal::kFocusRadius ); // 씬 뷰가 없으면 고르기만 한다
    }

    uint32 MapCheckViewModel::validateActiveScene()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return 0;
        const Stopwatch     stopwatch;
        vector<GameObject*> listObject;
        pManager->getAllGameObjects( editor::EditorSceneIterationAccess::getKey(), listObject );
        for ( const GameObject* pObject : listObject )
        {
            (void)ObjectValidation::reportGameObject( *pObject, false ); // 결과는 그 씬의 표(SceneValidationIssues)에 남고 테이블이 읽는다
        }
        (void)EditorMapCheck::runAll( *pManager ); // 오브젝트 밖의 검사(카탈로그 · 키트) — 결과는 같은 표에 남는다
        SW_LOG_INFO_ID( "MapCheckPanel.MapCheckObjectChecked", "Map Check: %# object(s) checked in %# ms", listObject.size(), stopwatch.getElapsedMilliseconds() );
        return static_cast<uint32>( listObject.size() );
    }

    MapCheckCounts MapCheckViewModel::updateStatusCounts()
    {
        MapCheckViewModelInternal::State& state       = MapCheckViewModelInternal::getState();
        const ValidationIssueLog*         pLog        = MapCheckViewModelInternal::findActiveIssueLog();
        const uint64                      revisionKey = MapCheckViewModelInternal::makeRevisionKey( pLog );
        if ( state._bStatusCounted == false || revisionKey != state._statusRevisionKey )
        {
            vector<ValidationIssue> listIssue;
            MapCheckViewModelInternal::collectActiveIssues( pLog, listIssue );
            state._statusCounts      = MapCheckCounts::countIssues( listIssue );
            state._statusRevisionKey = revisionKey;
            state._bStatusCounted    = true;
        }
        MapCheckViewModelInternal::notifyOnSceneChange( state._statusCounts );
        return state._statusCounts;
    }

    void MapCheckViewModel::requestOpenWindow( string_view panelID )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().requestOpenPanelByID( panelID );
    }

    bool MapCheckViewModel::readActiveIssueCount( uint32& outCount )
    {
        const ValidationIssueLog* pLog = MapCheckViewModelInternal::findActiveIssueLog();
        if ( pLog == nullptr )
            return false;
        outCount = static_cast<uint32>( pLog->getIssueCount() );
        return true;
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct MapCheckFixCommandsInternal
        {
            /** @brief 오브젝트에서 타입 이름이 @p typeName 인 첫 컴포넌트입니다. 없으면 nullptr 입니다. */
            static Component* findComponent( const GameObject& object, const hashed_string& typeName )
            {
                Component* pFound = nullptr;
                object.forEachComponent( [&pFound, &typeName]( Component* pComponent )
                {
                    const TypeInfo* pType = pComponent->findCachedTypeInfo();
                    if ( pFound == nullptr && pType != nullptr && pType->_fullyQualifiedName == typeName )
                        pFound = pComponent;
                } );
                return pFound;
            }

            /** @brief 고침 하나를 씁니다. 쓰면 true 이고, 찾지 못했거나 쓰지 못했으면 이유를 경고하고 false 입니다. */
            static bool applyOne( GameObject& object, const ValidationIssue& issue )
            {
                Component* pComponent = findComponent( object, issue._typeName );
                if ( pComponent == nullptr )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.ComponentGone", "Fix skipped - '%#' has no %# any more", object.getName().c_str(), issue._typeName.c_str() );
                    return false;
                }
                const TypeInfo*     pType = pComponent->findCachedTypeInfo();
                const PropertyInfo* pProp = pType != nullptr ? pType->findPropertyInHierarchy( issue._fix._propertyName ) : nullptr;
                if ( pProp == nullptr )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.PropertyGone", "Fix skipped - %# has no property '%#'", issue._typeName.c_str(), issue._fix._propertyName.c_str() );
                    return false;
                }
                if ( SerializerUtil::applyPropertyText( *pProp, MutableObjectAddress{ pComponent }, issue._fix._valueText, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.ValueRejected", "Fix skipped - '%#' could not be written to %#.%#", issue._fix._valueText, issue._typeName.c_str(), pProp->_name.c_str() );
                    return false;
                }
                pComponent->onPropertyChanged( pProp->_name );
                return true;
            }
        };
    } // namespace

    bool MapCheckFixCommands::canFix( const ValidationIssue& issue )
    {
        return issue._fix.isValid() && issue._sourceID != 0;
    }

    uint32 MapCheckFixCommands::applyFixes( GameObjectManager* pManager, const vector<ValidationIssue>& listIssue, const utf8* pUndoLabel )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        if ( pManager == nullptr || EditorPlaySession::refuseSceneEdit( "Map Check fix" ) )
            return 0;
        uint32 fixedCount = 0;
        EditorTransaction::beginTransaction( pUndoLabel );
        for ( const ValidationIssue& issue : listIssue )
        {
            if ( canFix( issue ) == false )
                continue;
            GameObject* pObject = pManager->findGameObjectByID( issue._sourceID );
            if ( pObject == nullptr )
                continue; // 지운 오브젝트의 결과다 — 표가 곧 비운다
            const ObjectSnapshot before = EditorTransaction::captureSnapshot( pObject );
            if ( MapCheckFixCommandsInternal::applyOne( *pObject, issue ) == false )
                continue;
            EditorSceneCommands::commitModify( transaction, pObject, before, pUndoLabel );
            (void)ObjectValidation::reportGameObject( *pObject, false ); // 고친 결과가 표에서 빠진다(같은 출처를 다시 검증)
            ++fixedCount;
        }
        if ( fixedCount == 0 )
            EditorTransaction::cancelTransaction();
        else
            EditorTransaction::endTransaction();
        SW_LOG_INFO_ID( "MapCheckFixCommands.Applied", "Map Check: applied %# fix(es)", fixedCount );
        return fixedCount;
    }
} // namespace sw::editor
