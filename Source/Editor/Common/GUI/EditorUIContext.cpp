#include "pch.h"

#include "Editor/Common/GUI/EditorUIContext.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUIContextInternal
        {
            static EditorUIContextState& getState()
            {
                static EditorUIContextState s_state{};
                return s_state;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorUIContext::publish( const EditorUIContextState& state )
    {
        EditorUIContextInternal::getState() = state;
        using BinderRegistry                = EditorRegistry<EditorUIBinderRegistration>;
        for ( uint32 index = 0; index < BinderRegistry::getCount(); ++index )
        {
            const EditorUIBinderRegistration& registration = BinderRegistry::getAt( index );
            if ( registration._pfnBind != nullptr )
                registration._pfnBind( state );
        }
    }

    const EditorUIContextState& EditorUIContext::getCurrent()
    {
        return EditorUIContextInternal::getState();
    }
} // namespace sw::editor
