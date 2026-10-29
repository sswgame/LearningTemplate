/**
 * @file DebugDrawQueueDevCommands.cpp
 * @brief 디버그 그리기 분류 명령 `debugdraw.category`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"

namespace sw
{
    namespace
    {
        bool runDebugDrawCategory( const vector<string>& listArgument, string& outReply )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            DebugDrawQueue& queue = engine::getSubsystem<DebugDrawQueue>();
            if ( listArgument.empty() )
            {
                vector<hashed_string> listCategory;
                queue.collectCategories( listCategory );
                outReply = "categories:";
                for ( const hashed_string& category : listCategory )
                {
                    outReply += ' ';
                    outReply += category.c_str();
                    outReply += queue.isCategoryEnabled( category ) ? "(on)" : "(off)";
                }
                return true;
            }
            if ( listArgument.size() != 2 )
                return false;
            bool bOn{ false };
            if ( StringUtil::tryParseBool( listArgument[1], bOn ) == false )
                return false;
            queue.setCategoryEnabled( hashed_string( listArgument[0] ), bOn );
            outReply = listArgument[0] + ( bOn ? " on" : " off" );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( DebugDrawCategory, "debugdraw.category", "debugdraw.category [<name> <on|off>]", "List or toggle DebugDrawQueue categories",
                    &runDebugDrawCategory );
} // namespace sw
