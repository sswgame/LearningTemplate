#include "pch.h"

#include "Editor/Viewport/EditorVisualizerToggles.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

namespace sw::editor
{
    bool EditorVisualizerToggles::isOn( const EditorVisualizerRegistration& registration ) const
    {
        for ( const EditorVisualizerToggle& toggle : _listToggle )
        {
            if ( toggle._id == registration._pID )
                return toggle._bOn;
        }
        return registration._bDefaultOn;
    }

    void EditorVisualizerToggles::setOn( const EditorVisualizerRegistration& registration, bool bOn )
    {
        for ( size_t index = 0; index < _listToggle.size(); ++index )
        {
            if ( _listToggle[index]._id != registration._pID )
                continue;
            if ( bOn == registration._bDefaultOn )
                _listToggle.erase( _listToggle.begin() + static_cast<ptrdiff_t>( index ) );
            else
                _listToggle[index]._bOn = bOn;
            return;
        }
        if ( bOn != registration._bDefaultOn )
            _listToggle.push_back( EditorVisualizerToggle{ registration._pID, bOn } );
    }
} // namespace sw::editor
