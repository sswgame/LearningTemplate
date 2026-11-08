#include "pch.h"

#include "App/Dev/InteractiveAssertDialog.h"

#include "DevTools/Client/Diagnostics/AssertDialog.h"

namespace sw
{
    void InteractiveAssertDialog::install( const CommandLineManager& commandLine )
    {
        AssertDialog::installForInteractiveRun( commandLine );
    }
} // namespace sw
