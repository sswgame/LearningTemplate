#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationStageFingerprint.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct AutomationStageFingerprintInternal
        {
            static void addState( AutomationStageFingerprint& fingerprint, string_view name, string_view state )
            {
                fingerprint._listEntry.push_back( AutomationStageFingerprintEntry{ string( name ), string( state ), nullopt } );
            }

            static void addCount( AutomationStageFingerprint& fingerprint, string_view name, uint64 count )
            {
                fingerprint._listEntry.push_back( AutomationStageFingerprintEntry{ string( name ), string{}, count } );
            }
        };
    } // namespace

    AutomationStageFingerprint AutomationStageFingerprint::capture( bool bCountOwned )
    {
        using Internal = AutomationStageFingerprintInternal;
        AutomationStageFingerprint fingerprint;

        // 게임 인스턴스 수명 서비스 — 경계가 비운 뒤다. 남은 이름이 있으면 그 서비스의 비우기가 빠뜨린 것이다.
        vector<Subsystem*> listSubsystem;
        engine::collectInitializedSubsystems( listSubsystem );
        for ( const Subsystem* const pSubsystem : listSubsystem )
        {
            const utf8* const pState = pSubsystem->findGameInstanceState();
            if ( pState != nullptr )
                Internal::addState( fingerprint, pSubsystem->getSubsystemName(), pState );
        }

        // 프로세스 수명이지만 건 쪽이 게임과 함께 풀어야 하는 것.
        Internal::addCount( fingerprint, "GameTimeScale pause requests", engine::getSubsystem<GameTimeScale>().getPauseRequestCount() );
        Internal::addCount( fingerprint, "SceneManager loaded scenes", engine::getSubsystem<SceneManager>().getLoadedScenes().size() );
        if ( engine::getSubsystem<DebugHUDRegistry>().getTarget().isValid() )
            Internal::addState( fingerprint, "DebugHUDRegistry", "a target object" );
        if ( engine::getSubsystem<InputManager>().isVirtualInputAttached() )
            Internal::addState( fingerprint, "InputManager", "an attached virtual input" );
        string                         attachment;
        const GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
        if ( pInspector != nullptr && pInspector->findVisualizedAttachment( attachment ) )
            Internal::addState( fingerprint, "GPUPixelInspector", "a visualized attachment" );
        if ( bCountOwned )
        {
            Internal::addCount( fingerprint, "EventDispatcher subscribers", engine::getSubsystem<EventDispatcher>().getSubscriberCount() );
            // 플레이 오브젝트의 주인은 씬이다 — 연 씬은 위에서 세었고, 씬 밖의 씬은 미리보기뿐이며 만든 컴포넌트가 내린다. 트랜스폼 칸 수는 세지 않는다(리플렉션 기본 개체 같은
            // 프로세스 수명 캐시가 주인인 칸이 섞인다).
            Internal::addCount( fingerprint, "SceneManager preview scenes", engine::getSubsystem<SceneManager>().getPreviewScenes().size() );
        }
        return fingerprint;
    }

    const AutomationStageFingerprintEntry* AutomationStageFingerprint::findEntry( string_view name ) const
    {
        for ( const AutomationStageFingerprintEntry& entry : _listEntry )
        {
            if ( entry._name == name )
                return &entry;
        }
        return nullptr;
    }

    vector<string> AutomationStageFingerprint::describeLeaks( const AutomationStageFingerprint& baseline, const AutomationStageFingerprint& current )
    {
        vector<string> listLeak;
        for ( const AutomationStageFingerprintEntry& entry : current._listEntry )
        {
            const AutomationStageFingerprintEntry* const pBefore = baseline.findEntry( entry._name );
            StringBuilder<constant::buffer::kMax256>     line;
            if ( entry._count.has_value() )
            {
                const bool bGrew = pBefore != nullptr && pBefore->_count.has_value() && *entry._count > *pBefore->_count;
                if ( bGrew == false )
                    continue;
                line.appendFormat( "%#: %# -> %#", entry._name, *pBefore->_count, *entry._count );
            }
            else
            {
                if ( pBefore != nullptr && pBefore->_state == entry._state )
                    continue;
                line.appendFormat( "%#: %#", entry._name, entry._state );
            }
            listLeak.emplace_back( line.view() );
        }
        return listLeak;
    }
} // namespace sw
