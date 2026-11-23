#include "pch.h"

#include "DevTools/Console/ExecFunctionCommand.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionEditorMetadata.h"
#include "Engine/Reflection/ReflectionInvoke.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        /** @brief `exec <object> <function> [argument...]` */
        bool runExecCommand( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() < 2 )
                return false;
            GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
            if ( pObject == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "no object named '%#' in the active scene", listArgument[0] );
                outReply.assign( reply.view() );
                return false;
            }
            return ExecFunctionCommand::run( *pObject, listArgument[1], vector<string>( listArgument.begin() + 2, listArgument.end() ), outReply );
        }
    } // namespace

    bool ExecFunctionCommand::run( GameObject& object, string_view functionName, const vector<string>& listArgument, string& outReply )
    {
        const hashed_string                      name( functionName );
        StringBuilder<constant::buffer::kMax256> reply;
        for ( Component* pComponent : object.getComponents() )
        {
            if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                continue;
            const TypeInfo*     pType     = pComponent->findCachedTypeInfo();
            const FunctionInfo* pFunction = pType != nullptr ? pType->findMethodInHierarchy( name ) : nullptr;
            if ( pFunction == nullptr || pFunction->_metadata->getEditor()._bExec == SW_FALSE )
                continue;
            ReflectedValue          returned;
            const ReflectCallResult result = ReflectionInvoke::callWithText( *pFunction, pComponent, listArgument, &returned );
            const bool              bOk    = result == ReflectCallResult::Ok;
            if ( bOk )
                reply.appendFormat( "%#.%# %#", pType->_name.c_str(), functionName, returned.toText() );
            else
                reply.appendFormat( "%#.%# takes %# argument(s): %#", pType->_name.c_str(), functionName, pFunction->getParameterCount(), getEnumName( result ) );
            outReply.assign( reply.view() );
            return bOk;
        }
        reply.appendFormat( "no component of '%#' has a FUNCTION( Exec ) named '%#'", object.getName().c_str(), functionName );
        outReply.assign( reply.view() );
        return false;
    }

    SW_DEV_COMMAND( ExecFunction, "exec", "exec <object> <function> [argument...]", "Call a FUNCTION( Exec ) method of a named object's component in the active scene", &runExecCommand );
} // namespace sw
