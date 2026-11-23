#include "pch.h"

#include "DevTools/Console/DebugHUDSectionLines.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    void DebugHUDSectionLines::write( const DebugHUDSectionRegistration& section, DebugHUDSectionWriter& writer )
    {
        if ( section._pFunc != nullptr )
            section._pFunc( writer );
        if ( section._pWatchGroup == nullptr )
            return;
        vector<const AutomationWatchRegistration*> listWatch;
        AutomationWatchRegistry::collectSceneWatches( section._pWatchGroup, listWatch );
        const AutomationWatchQuery query = AutomationWatchQuery::make( engine::findActiveObjectManager(), {}, {} );
        for ( const AutomationWatchRegistration* pWatch : listWatch )
        {
            AutomationWatchValue value;
            if ( AutomationWatchRegistry::read( *pWatch, query, value ) )
                writer.addLineFormat( pWatch->_pName, "%.2f", value._number );
            else
                writer.addLine( pWatch->_pName, "-" ); // 지금 낼 값이 없다(씬이 없음 등)
        }
    }

    namespace
    {
        struct DebugHUDSectionLinesInternal
        {
            /**
             * @brief `hud.print <섹션>` — 섹션 줄을 한 번 지어 줄마다 로그(`DebugHUD.PrintLine`)와 답으로 낸다. HUD 를 켜지 않고 Output Log · 시나리오가 읽는다.
             * @details 본문은 HUD 가 부를 때와 같다(읽기만 한다). 게임 스레드에서 돈다(개발 명령은 게임 스레드에서 실행된다).
             */
            static bool runPrint( SceneIterationKey key, const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 || engine::areEngineServicesBound() == false )
                    return false;
                const DebugHUDSectionRegistration* const pSection = engine::getSubsystem<DebugHUDRegistry>().findSection( listArgument[0] );
                if ( pSection == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> reply;
                    reply.appendFormat( "hud.print: no section '%#' (hud list)", listArgument[0] );
                    outReply.assign( reply.view() );
                    return false;
                }
                DebugHUDSectionWriter writer{ key };
                DebugHUDSectionLines::write( *pSection, writer );
                StringBuilder<constant::buffer::kMax2048> reply;
                reply.appendFormat( "[%#]", pSection->_pTitle );
                for ( uint32 index = 0; index < writer.getLineCount(); ++index )
                {
                    const DebugHUDLine& line = writer.getLineAt( index );
                    SW_LOG_INFO_ID( "DebugHUD.PrintLine", "[%#] %#: %#", pSection->_pName, line._label, line._value );
                    reply.appendFormat( "\n  %#: %#", line._label, line._value );
                }
                outReply.assign( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_SCENE_COMMAND( DebugHUDPrint, "hud.print", "hud.print <section>", "Run one debug HUD section once and print its lines to the log (no HUD needed; Output Log and scenarios read it)",
                          &DebugHUDSectionLinesInternal::runPrint );
} // namespace sw
