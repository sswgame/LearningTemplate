#include "pch.h"

#include "Engine/Module/EngineABIStamp.h"

#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/EngineABIStampMarker.h"

#include "EngineABIStamp.gen.h"

namespace sw
{
    namespace engine
    {
        const utf8* getEngineABIStamp()
        {
            return SW_ENGINE_ABI_STAMP_MARKER SW_ENGINE_ABI_STAMP_DIGEST;
        }

        bool isModuleBuiltWithThisEngine( void* pLibraryHandle, string_view moduleName )
        {
            StringBuilder<constant::buffer::kMax128> symbolName;
            symbolName.appendFormat( "sw_moduleEngineABIStamp_%#", moduleName );
            const utf8* const pModuleStamp = pLibraryHandle != nullptr ? static_cast<const utf8*>( ModuleLibraryUtil::getDynamicSymbol( pLibraryHandle, symbolName.view() ) ) : nullptr;
            if ( pModuleStamp != nullptr && StringUtil::equals( pModuleStamp, getEngineABIStamp() ) )
                return true;
            SW_LOG_ERROR_ID( "Module.EngineABIStampMismatch", "Module %# was built against other engine headers (%#: %#; expected %#) - rebuild the engine and the module together", moduleName,
                             symbolName.view(), pModuleStamp != nullptr ? pModuleStamp : "missing", getEngineABIStamp() );
            return false;
        }
    } // namespace engine
} // namespace sw
