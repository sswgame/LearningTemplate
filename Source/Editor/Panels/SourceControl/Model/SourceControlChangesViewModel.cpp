#include "pch.h"

#include "Editor/Panels/SourceControl/Model/SourceControlChangesViewModel.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"

#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    EditorSourceControl& SourceControlChangesViewModel::getSourceControl()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>();
    }

    EditorSourceControl* SourceControlChangesViewModel::findSourceControl()
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        return pContext != nullptr ? &pContext->getSubsystem<EditorSourceControl>() : nullptr;
    }

    namespace
    {
        struct SourceControlDevCommandsInternal
        {
            /** @brief `sourcecontrol.history <저장소 기준 경로>` — 파일 하나의 최근 커밋을 읽는다(History 단추와 같다 — 바뀌지 않은 파일도 된다). */
            static bool runHistory( const vector<string>& listArgument, string& outReply )
            {
                EditorSourceControl* const pSourceControl = SourceControlChangesViewModel::findSourceControl();
                if ( listArgument.size() != 1 || pSourceControl == nullptr )
                    return false;
                if ( pSourceControl->requestHistory( listArgument[0] ) )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "reading history of %#", listArgument[0] );
                    outReply = string( replyBuilder.view() );
                }
                else
                    outReply = "source control does not track changes (no git)";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SourceControlHistory, "sourcecontrol.history", "sourcecontrol.history <repository path>", "Read the last commits of one file into the Changes window",
                    &SourceControlDevCommandsInternal::runHistory );
} // namespace sw::editor
