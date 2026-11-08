#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    void EditorUtil::appendCategoryBadge( string_view category, fixed_string<constant::buffer::kMax256>& inoutBadge )
    {
        if ( category.empty() )
            return;

        // 같은 Category 의 컴포넌트가 여럿 붙어 있으면 뱃지도 여럿이 된다. 그래서 한 번만 넣는다.
        fixed_string<constant::buffer::kMax256> token{ "[" };
        token.append( category );
        token.append( "]" );
        if ( inoutBadge.view().find( token.view() ) != string_view::npos )
            return;

        if ( inoutBadge.empty() == false )
            inoutBadge.append( " " );
        inoutBadge.append( token );
    }

} // namespace sw::editor
