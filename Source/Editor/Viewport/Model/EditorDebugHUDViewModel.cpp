#include "pch.h"

#include "Editor/Viewport/Model/EditorDebugHUDViewModel.h"

#include "DevTools/Client/DebugHUD/DebugHUD.h"
#include "DevTools/Console/DebugHUDSectionLines.h"

#include "Editor/Common/EditorSceneIterationAccess.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDebugHUDViewModelInternal
        {
            /** @brief 섹션 본문을 다시 부르는 간격(초) — 게임 HUD 와 같은 4 Hz. */
            static constexpr float32 kRefreshSeconds = 0.25f;
        };
    } // namespace

    EditorDebugHUDViewModel::EditorDebugHUDViewModel()
        : _listSlot{}
        , _registryRevision{ UINT64_MAX }
        , _refreshTimer{ 0.0f }
        , _skippedProfilerSectionCount{ 0 }
        , _followedSelectionRevision{ UINT64_MAX }
    {
    }

    EditorDebugHUDViewModel::~EditorDebugHUDViewModel() = default;

    void EditorDebugHUDViewModel::update( float32 deltaSeconds )
    {
        if ( _registryRevision != editor::getSubsystem<DebugHUDRegistry>().getRevision() )
        {
            rebuildSlots();
            _refreshTimer = 0.0f;
        }
        for ( SectionSlot& slot : _listSlot )
        {
            slot._pWriter->recordFrameSeconds( deltaSeconds );
        }
        _refreshTimer -= deltaSeconds;
        if ( _refreshTimer <= 0.0f )
        {
            refresh();
            _refreshTimer = EditorDebugHUDViewModelInternal::kRefreshSeconds;
        }
    }

    void EditorDebugHUDViewModel::rebuildSlots()
    {
        // 기록(프레임 시간)은 섹션마다 따로 들므로, 남는 섹션의 쓰개는 그대로 옮긴다.
        vector<SectionSlot> listOld;
        listOld.swap( _listSlot );
        for ( const DebugHUDSectionRegistration* pSection : editor::getSubsystem<DebugHUDRegistry>().getSections() )
        {
            SectionSlot slot{ pSection, nullptr };
            for ( SectionSlot& old : listOld )
            {
                if ( old._pRegistration == pSection )
                    slot._pWriter = std::move( old._pWriter );
            }
            if ( slot._pWriter == nullptr )
                slot._pWriter = make_unique<DebugHUDSectionWriter>( editor::EditorSceneIterationAccess::getKey() );
            _listSlot.push_back( std::move( slot ) );
        }
        _registryRevision = editor::getSubsystem<DebugHUDRegistry>().getRevision();
    }

    void EditorDebugHUDViewModel::followSelection()
    {
        const EditorSelection& selection = editor::getMainSceneSelection();
        if ( selection.getRevision() == _followedSelectionRevision )
            return;
        _followedSelectionRevision        = selection.getRevision();
        const GameObject* const pSelected = selection.getPrimaryObject();
        if ( pSelected != nullptr )
            editor::getSubsystem<DebugHUDRegistry>().setTarget( GameObjectHandle::make( pSelected->getObjectID() ) );
    }

    void EditorDebugHUDViewModel::refresh()
    {
        followSelection();
        const bool bProfilerOn       = editor::getSubsystem<FrameProfiler>().isEnabled();
        _skippedProfilerSectionCount = 0;
        for ( SectionSlot& slot : _listSlot )
        {
            slot._pWriter->reset();
            if ( DebugHUD::isSectionShown( *slot._pRegistration ) == false )
                continue;
            if ( ( slot._pRegistration->_flags & DebugHUDSectionFlag::kUsesFrameProfiler ) != 0 && bProfilerOn == false )
            {
                ++_skippedProfilerSectionCount;
                continue;
            }
            DebugHUDSectionLines::write( *slot._pRegistration, *slot._pWriter );
        }
    }
} // namespace sw::editor
