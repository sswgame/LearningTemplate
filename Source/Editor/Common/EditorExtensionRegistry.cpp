#include "pch.h"

#include "Editor/Common/EditorExtensionRegistry.h"

namespace sw::editor
{
    bool EditorExtensionRegistration::addTo( RegistrationList<const EditorExtensionRegistration>& list, const EditorExtensionRegistration& registration )
    {
        const string_view        id     = ( registration._pID != nullptr ) ? string_view{ registration._pID } : string_view{};
        const RegistrationResult result = list.addLogged( &registration, list.getListName(), id, registration._order );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    void EditorExtensionRegistration::removeFrom( RegistrationList<const EditorExtensionRegistration>& list, const EditorExtensionRegistration& registration )
    {
        (void)list.remove( &registration ); // 거절된 등록자의 소멸도 여기로 온다 — 올라 있지 않으면 할 일이 없다
    }
} // namespace sw::editor
