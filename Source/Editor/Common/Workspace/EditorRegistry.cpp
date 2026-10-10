#include "pch.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

#include "Core/Log/Logger.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorRegistry" );

    EditorRegistrationList::EditorRegistrationList( const utf8* pKindName )
        : _pKindName{ pKindName }
        , _registered{ RegistrationOrder::ByOrderThenName, true }
    {
    }

    bool EditorRegistrationList::addRegistration( const EditorRegistration& registration )
    {
        const string_view        id     = ( registration._pID != nullptr ) ? string_view{ registration._pID } : string_view{};
        const RegistrationResult result = _registered.add( &registration, id, registration._order );
        switch ( result )
        {
            case RegistrationResult::Added:
            case RegistrationResult::AlreadyPresent:
            {
                return true;
            }
            case RegistrationResult::EmptyName:
            {
                SW_LOG_ERROR( "Editor %# registered with an empty id - ignored", _pKindName );
                return false;
            }
            case RegistrationResult::DuplicateName:
            {
                SW_LOG_ERROR( "Editor %# '%#' is already registered - keeping the first", _pKindName, id );
                return false;
            }
            case RegistrationResult::NullItem:
            {
                return false;
            }
        }
    }

    void EditorRegistrationList::removeRegistration( const EditorRegistration& registration )
    {
        (void)_registered.remove( &registration ); // 거절된 등록자의 소멸도 여기로 온다 — 올라 있지 않으면 할 일이 없다
    }
} // namespace sw::editor
