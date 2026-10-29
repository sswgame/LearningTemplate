#include "pch.h"

#include "Editor/EditorModuleUnloadListener.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorDefaultObjects.h"
#include "Editor/Common/Workspace/EditorExtensionRegistry.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Panels/Inspector/InspectorPropertyManager.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    span<const utf8* const> EditorModuleUnloadListener::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorPanelManager::kSubsystemName, EditorPopupManager::kSubsystemName, InspectorComponentManager::kSubsystemName, InspectorPropertyManager::kSubsystemName, EditorDefaultObjects::kSubsystemName, EditorExtensionSubsystems::kSubsystemName };
        return kArrDependency;
    }

    EditorModuleUnloadListener::EditorModuleUnloadListener( EditorContext& context )
        : _context{ context }
        , _registrationRemovedSubscription{}
        , _moduleUnloadingSubscription{}
    {
    }

    EditorModuleUnloadListener::~EditorModuleUnloadListener() = default;

    SubsystemInitResult EditorModuleUnloadListener::initializeSubsystem()
    {
        _registrationRemovedSubscription = subscribeEditorExtensionRemoved( Delegate<void()>::create<&EditorModuleUnloadListener::onRegistrationRemoved>( this ) );
        _moduleUnloadingSubscription     = editor::getSubsystem<TypeRegistry>().subscribeModuleUnloading(
            TypeRegistry::ModuleUnloadingDelegate::create<&EditorModuleUnloadListener::onModuleUnloading>( this ) );
        return SubsystemInitResult::Succeeded;
    }

    void EditorModuleUnloadListener::shutdownSubsystem()
    {
        _moduleUnloadingSubscription     = Subscription{};
        _registrationRemovedSubscription = Subscription{};
    }

    void EditorModuleUnloadListener::onRegistrationRemoved()
    {
        _context.syncExtensionRegistrations();
    }

    void EditorModuleUnloadListener::onModuleUnloading( string_view /*moduleName*/ )
    {
        _context.getSubsystem<EditorDefaultObjects>().clear();
    }
} // namespace sw::editor
