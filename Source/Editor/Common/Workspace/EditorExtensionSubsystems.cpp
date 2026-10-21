#include "pch.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"

#include "Core/Module/ModuleUnloadListener.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorExtensionSubsystems" );

    EditorExtensionSubsystems::EditorExtensionSubsystems( SubsystemScope& scope )
        : _scope{ scope }
        , _listEntry{}
        , _syncedGeneration{ 0 }
    {
    }

    EditorExtensionSubsystems::~EditorExtensionSubsystems()
    {
        releaseAll();
    }

    void EditorExtensionSubsystems::syncWithRegistry()
    {
        using SubsystemRegistry                     = EditorExtensionRegistry<EditorSubsystemRegistration>;
        const EditorExtensionRegistrationList& list = SubsystemRegistry::getList();
        if ( list.getGeneration() == _syncedGeneration )
            return;
        _syncedGeneration = list.getGeneration();

        // 1) 등록 목록에서 사라진 항목의 인스턴스를 종료한다(정상 경로에서는 releaseSubsystemsWithin 이 먼저 했다 — 여기는 줄을 직접 뺀 경우)
        for ( size_t index = _listEntry.size(); index-- > 0; )
        {
            if ( SubsystemRegistry::find( _listEntry[index]._pRegistration->_pID ) != _listEntry[index]._pRegistration )
                releaseAt( index );
        }
        // 2) 새 줄은 만들어 초기화한 뒤 범위에 건다(등록 순서대로 — 내릴 때는 역순)
        for ( uint32 index = 0; index < SubsystemRegistry::getCount(); ++index )
        {
            const EditorSubsystemRegistration& registration = SubsystemRegistry::getAt( index );
            bool                               bExisting    = false;
            for ( const Entry& entry : _listEntry )
            {
                bExisting = bExisting || entry._pRegistration == &registration;
            }
            if ( bExisting )
                continue;

            unique_ptr<EditorSubsystem> pSubsystem = registration._pCreate();
            if ( pSubsystem == nullptr )
                continue;
            if ( pSubsystem->initializeSubsystem() == SubsystemInitResult::Failed )
            {
                SW_LOG_ERROR( "Extension editor subsystem '%#' failed to initialize", registration._pID );
                continue;
            }
            if ( registration._pBind( _scope, *pSubsystem ) == false )
            {
                SW_LOG_ERROR( "Extension editor subsystem '%#' is already bound in the editor scope - two modules contribute one type", registration._pID );
                pSubsystem->shutdownSubsystem();
                continue;
            }
            _listEntry.push_back( Entry{ &registration, std::move( pSubsystem ) } );
        }
    }

    uint32 EditorExtensionSubsystems::releaseSubsystemsWithin( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount{ 0 };
        for ( size_t index = _listEntry.size(); index-- > 0; )
        {
            if ( IModuleUnloadListener::isAddressWithin( _listEntry[index]._pRegistration, pBegin, pEnd ) == false )
                continue;
            releaseAt( index );
            ++releasedCount;
        }
        return releasedCount;
    }

    void EditorExtensionSubsystems::releaseAll()
    {
        for ( size_t index = _listEntry.size(); index-- > 0; )
        {
            releaseAt( index );
        }
        _syncedGeneration = 0; // 다음 sync 가 다시 만든다
    }

    void EditorExtensionSubsystems::releaseAt( size_t index )
    {
        Entry& entry = _listEntry[index];
        // 범위에서 먼저 풀어 종료 중인 서브시스템을 아무도 찾지 못하게 한다. 등록 항목의 함수는 아직 올라와 있는 모듈 안에 있다(언로드 직전에 부른다).
        entry._pRegistration->_pUnbind( _scope );
        entry._pSubsystem->shutdownSubsystem();
        _listEntry.erase( _listEntry.begin() + static_cast<ptrdiff_t>( index ) );
    }
} // namespace sw::editor
