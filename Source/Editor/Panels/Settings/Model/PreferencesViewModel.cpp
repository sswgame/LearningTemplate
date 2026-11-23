#include "pch.h"

#include "Editor/Panels/Settings/Model/PreferencesViewModel.h"

#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorPropertyLayout.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/EditorStateTextHistory.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"

#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 검색어에 맞는 프로퍼티가 섹션에 하나라도 있으면 true 입니다. 검색어가 없으면 늘 true 입니다. */
        bool matchesFilter( const EditorSettingsRegistration& registration, const utf8* pFilterText )
        {
            const EditorListFilter filter{ pFilterText };
            if ( filter.isActive() == false )
                return true;
            if ( filter.matches( registration._pLabel ) )
                return true;
            if ( registration._pfnGetType() == nullptr )
                return false; // 그리기 함수 쪽은 이름으로만 찾는다
            vector<InspectorPropertyGroup> listGroup;
            InspectorPropertyLayout::collectPropertyGroups( *registration._pfnGetType(), {}, filter, listGroup );
            return listGroup.empty() == false;
        }
    } // namespace

    PreferencesViewModel::PreferencesViewModel()
        : _pHistory{ make_unique<EditorStateTextHistory>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) ) }
        , _listVisibleSection{}
        , _sectionFilterText{}
        , _sectionListGeneration{ 0 }
        , _sectionTypeGeneration{ 0 }
        , _bSectionListBuilt{ SW_FALSE }
        , _bModifiedOnly{ false }
        , _pSelected{ nullptr }
        , _pendingSaveNanoseconds{ 0 }
    {
    }

    PreferencesViewModel::~PreferencesViewModel() = default;

    const vector<const EditorSettingsRegistration*>& PreferencesViewModel::refreshVisibleSections( const utf8* pFilterText )
    {
        using SettingsRegistry         = EditorExtensionRegistry<EditorSettingsRegistration>;
        const uint32 listGeneration    = SettingsRegistry::getList().getGeneration();
        const uint32 typeGeneration    = TypeRegistry::getGeneration();
        const bool   bSameAsLastBuild  = _bSectionListBuilt && _sectionListGeneration == listGeneration && _sectionTypeGeneration == typeGeneration;
        const bool   bListStillCurrent = bSameAsLastBuild && _sectionFilterText == pFilterText;
        if ( bListStillCurrent )
            return _listVisibleSection;

        // 검색 글이 있으면 섹션마다 프로퍼티 묶음을 지어 본다 — 매 프레임 하면 섹션 수만큼 묶음을 짓는다.
        _listVisibleSection.clear();
        bool bSelectedVisible = false;
        for ( uint32 index = 0; index < SettingsRegistry::getCount(); ++index )
        {
            const EditorSettingsRegistration& registration = SettingsRegistry::getAt( index );
            if ( matchesFilter( registration, pFilterText ) == false )
                continue;
            _listVisibleSection.push_back( &registration );
            bSelectedVisible = bSelectedVisible || _pSelected == &registration;
        }
        // 등록 세대가 바뀌면 고른 섹션이 모듈과 함께 내려갔을 수 있다 — 목록에 남은 항목만 고른 채로 둔다(풀린 주소를 들지 않는다).
        if ( bSelectedVisible == false )
        {
            _pSelected = _listVisibleSection.empty() ? nullptr : _listVisibleSection.front();
            syncHistorySource();
        }
        _sectionFilterText     = pFilterText;
        _sectionListGeneration = listGeneration;
        _sectionTypeGeneration = typeGeneration;
        _bSectionListBuilt     = SW_TRUE;
        return _listVisibleSection;
    }

    void PreferencesViewModel::collectHiddenProperties( vector<hashed_string>& outListHidden ) const
    {
        outListHidden.clear();
        if ( _bModifiedOnly == false || _pSelected == nullptr || _pSelected->_pfnGetType() == nullptr )
            return;
        for ( const PropertyInfo& prop : _pSelected->_pfnGetType()->getPropertiesWithBase() )
        {
            if ( EditorPreferencesStore::isPropertyModified( prop, ObjectAddress{ _pSelected->_pInstance }, ObjectAddress{ _pSelected->_pDefault } ) == false )
                outListHidden.push_back( prop._name );
        }
    }

    void PreferencesViewModel::selectSection( const EditorSettingsRegistration& registration )
    {
        _pSelected = &registration;
        syncHistorySource();
    }

    void PreferencesViewModel::recordEdited( string_view label, bool bInteracting )
    {
        if ( _pSelected != nullptr && _pSelected->_pfnOnChanged != nullptr )
            _pSelected->_pfnOnChanged();
        _pHistory->notifyEdited( label, bInteracting );
        requestSave();
    }

    void PreferencesViewModel::flushEdit( bool bInteracting )
    {
        _pHistory->flush( bInteracting );
    }

    CommandStack& PreferencesViewModel::getCommandStack()
    {
        return _pHistory->getStack();
    }

    void PreferencesViewModel::syncHistorySource()
    {
        using SettingsRegistry = EditorExtensionRegistry<EditorSettingsRegistration>;
        EditorStateTextSource source;
        if ( _pSelected != nullptr && _pSelected->_pfnGetType() != nullptr ) // 그리기 함수 페이지는 값의 주인이 따로 있다
        {
            const string sectionID{ _pSelected->_pID };
            source._capture = SW_DELEGATE_LAMBDA( Delegate<string()>, [sectionID]()
            {
                const EditorSettingsRegistration* pSection = SettingsRegistry::find( sectionID );
                return pSection != nullptr ? JSONSerializer::serialize( pSection->_pInstance, *pSection->_pfnGetType() ).value_or( string{} ) : string{};
            } );
            source._restore = SW_DELEGATE_LAMBDA( Delegate<void( string_view )>, [this, sectionID]( string_view text )
            { restoreSection( sectionID, text ); } );
        }
        _pHistory->selectSource( source );
    }

    void PreferencesViewModel::restoreSection( const string& sectionID, string_view text )
    {
        const EditorSettingsRegistration* pSection = EditorExtensionRegistry<EditorSettingsRegistration>::find( sectionID );
        if ( pSection == nullptr || JSONSerializer::deserializeSoft( pSection->_pInstance, *pSection->_pfnGetType(), text ) == SoftReadResult::NotParsed )
            return;
        if ( pSection->_pfnOnChanged != nullptr )
            pSection->_pfnOnChanged();
        requestSave();
    }

    void PreferencesViewModel::resetSelectedSection()
    {
        if ( _pSelected == nullptr )
            return;
        EditorPreferencesStore::resetSection( *_pSelected );
        _pHistory->notifyEdited( "Reset Section", false );
        requestSave();
    }

    void PreferencesViewModel::savePendingChanges( bool bForce )
    {
        if ( _pendingSaveNanoseconds == 0 )
            return;
        if ( bForce == false && MonotonicClock::nowNanoseconds() - _pendingSaveNanoseconds < kSaveDelayNanoseconds )
            return;
        _pendingSaveNanoseconds = 0;
        (void)EditorPreferencesStore::saveOpened(); // 실패는 saveAll 이 경고로 알린다
    }

    void PreferencesViewModel::requestSave()
    {
        if ( _pendingSaveNanoseconds == 0 )
            _pendingSaveNanoseconds = MonotonicClock::nowNanoseconds();
    }
} // namespace sw::editor
