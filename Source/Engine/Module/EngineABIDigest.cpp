#include "pch.h"

#include "Engine/Module/EngineABIDigest.h"

#include "Core/Log/Logger.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/EngineABIDigestMarker.h"

#include "EngineABIDigest.gen.h"

namespace sw
{
    namespace engine
    {
        const utf8* getEngineABIDigest()
        {
            return SW_ENGINE_ABI_DIGEST_MARKER SW_ENGINE_ABI_DIGEST;
        }

        bool isModuleBuiltWithThisEngine( void* pLibraryHandle, string_view moduleName )
        {
            StringBuilder<constant::buffer::kMax128> symbolName;
            symbolName.appendFormat( "sw_moduleEngineABIDigest_%#", moduleName );
            const utf8* const pModuleDigest = pLibraryHandle != nullptr ? static_cast<const utf8*>( ModuleLibraryUtil::getDynamicSymbol( pLibraryHandle, symbolName.view() ) ) : nullptr;
            if ( pModuleDigest != nullptr && StringUtil::equals( pModuleDigest, getEngineABIDigest() ) )
                return true;
            SW_LOG_ERROR_ID( "Module.EngineABIDigestMismatch", "Module %# was built against other engine headers (%#: %#; expected %#) - rebuild the engine and the module together", moduleName,
                             symbolName.view(), pModuleDigest != nullptr ? pModuleDigest : "missing", getEngineABIDigest() );
            return false;
        }
    } // namespace engine
} // namespace sw
