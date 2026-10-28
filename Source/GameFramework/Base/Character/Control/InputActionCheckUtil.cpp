#include "pch.h"

#include "GameFramework/Base/Character/Control/InputActionCheckUtil.h"

#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    SW_LOG_CALLER( "InputActionCheck" );

    namespace
    {
        /** @brief 액션 하나가 비지 않았고 입력 맵에 있는지 봅니다. 아니면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool hasInputActionChecked( const InputMap& inputMap, const hashed_string& action, string_view ownerName )
        {
            if ( action.empty() )
            {
                SW_LOG_ERROR( "%#: an input action PROPERTY is empty - set it in the scene", ownerName );
                return false;
            }
            if ( inputMap.hasAction( action ) == false )
            {
                SW_LOG_ERROR( "%#: input action '%#' is not in the input map", ownerName, action.c_str() );
                return false;
            }
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool InputActionCheckUtil::hasAllActions( const InputMap& inputMap, std::initializer_list<const hashed_string*> listAction, string_view ownerName )
    {
        bool bAll = true;
        for ( const hashed_string* pAction : listAction )
        {
            bAll = hasInputActionChecked( inputMap, *pAction, ownerName ) && bAll;
        }
        return bAll;
    }

    bool InputActionCheckUtil::hasAllActions( const InputMap& inputMap, const vector<hashed_string>& listAction, size_t expectedCount, string_view ownerName )
    {
        bool bAll = true;
        if ( listAction.size() != expectedCount )
        {
            SW_LOG_ERROR( "%#: needs %# input actions in the list, has %#", ownerName, static_cast<uint32>( expectedCount ), static_cast<uint32>( listAction.size() ) );
            bAll = false;
        }
        for ( const hashed_string& action : listAction )
        {
            bAll = hasInputActionChecked( inputMap, action, ownerName ) && bAll;
        }
        return bAll;
    }
} // namespace sw
