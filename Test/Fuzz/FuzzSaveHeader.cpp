// SaveHeader 퍼저 — 몸은 모듈 `FuzzSaveHeaderCore`(`FuzzSaveHeaderModule.cpp`)에 있다. GameFramework 는 엔진 기동의 `ModuleTypes` 단계에서 올라와 있다(`SW_FUZZ_MODULES`).
#include "pch.h"

#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"

namespace
{
    using FuzzSaveHeaderFunction = void ( * )( const uint8_t*, size_t );

    /** @brief 몸 모듈의 진입점입니다. 올리지 못하면 멈춘다 — 퍼징이 아무것도 보지 않는다. */
    FuzzSaveHeaderFunction loadFuzzFunction()
    {
        const sw::optional<sw::AbsolutePath> libraryPath = sw::AbsolutePath::tryMake( sw::ModuleLibraryUtil::findModuleLibraryPath( "FuzzSaveHeaderCore" ) );
        void* const                          pHandle     = libraryPath.has_value() ? sw::ModuleLibraryUtil::loadLibraryAt( *libraryPath ) : nullptr;
        void* const                          pSymbol     = pHandle != nullptr ? sw::ModuleLibraryUtil::getDynamicSymbol( pHandle, "swFuzzSaveHeader" ) : nullptr;
        if ( pSymbol == nullptr )
            std::abort();
        return reinterpret_cast<FuzzSaveHeaderFunction>( pSymbol );
    }
} // namespace

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    static const FuzzSaveHeaderFunction s_pFuzz = loadFuzzFunction();
    s_pFuzz( pData, size );
    return 0;
}
