#include "pch.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
    namespace
    {
        struct EditorRegistryInternal
        {
            /** @brief lhs 가 rhs 보다 앞에 보이면 true 입니다 — (순서, id) 사전순. */
            static bool isBefore( const EditorRegistration& lhs, const EditorRegistration& rhs )
            {
                if ( lhs._order != rhs._order )
                    return lhs._order < rhs._order;
                return string_view{ lhs._pId } < string_view{ rhs._pId };
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorRegistry" );

    EditorRegistrationList::EditorRegistrationList( const utf8* pKindName )
        : _pKindName{ pKindName }
        , _listRegistration{}
    {
    }

    bool EditorRegistrationList::addRegistration( const EditorRegistration& registration )
    {
        if ( StringUtil::isNullOrEmpty( registration._pId ) )
        {
            SW_LOG_ERROR( "Editor %# registered with an empty id - ignored", _pKindName );
            return false;
        }

        const EditorRegistration* pExisting = findRegistration( registration._pId );
        if ( pExisting == &registration )
            return true;
        if ( pExisting != nullptr )
        {
            SW_LOG_ERROR( "Editor %# '%#' is already registered - keeping the first", _pKindName, registration._pId );
            return false;
        }

        auto insertIt = _listRegistration.begin();
        while ( insertIt != _listRegistration.end() && EditorRegistryInternal::isBefore( **insertIt, registration ) )
        {
            ++insertIt;
        }
        _listRegistration.insert( insertIt, &registration );
        return true;
    }

    void EditorRegistrationList::removeRegistration( const EditorRegistration& registration )
    {
        for ( auto it = _listRegistration.begin(); it != _listRegistration.end(); ++it )
        {
            if ( *it == &registration )
            {
                _listRegistration.erase( it );
                return;
            }
        }
    }

    const EditorRegistration* EditorRegistrationList::findRegistration( string_view id ) const
    {
        for ( const EditorRegistration* pRegistration : _listRegistration )
        {
            if ( id == pRegistration->_pId )
                return pRegistration;
        }
        return nullptr;
    }
} // namespace sw::editor
