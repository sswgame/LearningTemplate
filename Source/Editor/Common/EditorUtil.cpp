#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUtil" );

    void EditorUtil::appendCategoryBadge( string_view category, string& inoutBadge )
    {
        if ( category.empty() )
            return;

        // 같은 Category 의 컴포넌트가 여럿 붙어 있으면 뱃지도 여럿이 된다. 그래서 한 번만 넣는다.
        string token{ "[" };
        token.append( string{ category } );
        token.append( "]" );
        if ( inoutBadge.find( token ) != string::npos )
            return;

        if ( inoutBadge.empty() == false )
            inoutBadge.append( " " );
        inoutBadge.append( token );
    }

} // namespace sw::editor
