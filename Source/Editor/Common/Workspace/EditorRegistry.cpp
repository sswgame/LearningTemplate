#include "pch.h"

#include "Editor/Common/Workspace/EditorRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorRegistry" );

    namespace
    {
        struct EditorRegistryInternal
        {
            /** @brief 종류 이름 → 목록입니다. 종류는 열 개 안팎이라 줄 찾기로 충분하다. 함수 정적이라 다른 TU 의 정적 등록자보다 늦게 만들어질 걱정이 없다. */
            static vector<unique_ptr<EditorRegistrationList>>& getLists()
            {
                static vector<unique_ptr<EditorRegistrationList>> s_listList;
                return s_listList;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorRegistrationList::EditorRegistrationList( const utf8* pKindName )
        : _pKindName{ pKindName }
        , _registered{ RegistrationOrder::ByOrderThenName, true }
        , _generation{ 0 }
    {
    }

    bool EditorRegistrationList::addRegistration( const EditorRegistration& registration )
    {
        const string_view        id     = ( registration._pID != nullptr ) ? string_view{ registration._pID } : string_view{};
        const RegistrationResult result = _registered.add( &registration, id, registration._order );
        switch ( result )
        {
            case RegistrationResult::Added:
            {
                ++_generation;
                return true;
            }
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
        // 거절된 등록자의 소멸도 여기로 온다 — 올라 있지 않으면 할 일이 없다
        if ( _registered.remove( &registration ) )
            ++_generation;
    }

    EditorRegistrationList& getEditorRegistrationList( const utf8* pKindName )
    {
        vector<unique_ptr<EditorRegistrationList>>& listList = EditorRegistryInternal::getLists();
        for ( const unique_ptr<EditorRegistrationList>& pList : listList )
        {
            if ( StringUtil::equals( pList->getKindName(), pKindName, false ) )
                return *pList;
        }
        listList.push_back( make_unique<EditorRegistrationList>( pKindName ) );
        return *listList.back();
    }
} // namespace sw::editor
