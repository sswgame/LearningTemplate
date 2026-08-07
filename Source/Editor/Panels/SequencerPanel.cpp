/**
 * @file SequencerPanel.cpp
 */
#include "Panels/SequencerPanel.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>

namespace sw
{
	SequencerPanel::SequencerPanel()
	{
		_sequence.Add( 0 );
		_sequence.Add( 1 );
		_sequence.items[1].start = 20;
		_sequence.items[1].end	 = 40;
	}

	void SequencerPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::Text( "Frame: %d", _currentFrame );
		ImSequencer::Sequencer( &_sequence, &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
								ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );

		ImGui::End();
	}
} // namespace sw
