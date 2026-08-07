/**
 * @file GizmoPanel.cpp
 */
#include "Panels/GizmoPanel.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <cmath>

namespace sw
{
	namespace
	{
		void setIdentity( float* m )
		{
			for ( int i = 0; i < 16; ++i )
				m[i] = 0.0f;
			m[0] = m[5] = m[10] = m[15] = 1.0f;
		}

		void lookAt( float* out, float eyeX, float eyeY, float eyeZ, float atX, float atY, float atZ )
		{
			float fx = atX - eyeX;
			float fy = atY - eyeY;
			float fz = atZ - eyeZ;
			float fl = std::sqrt( fx * fx + fy * fy + fz * fz );
			if ( fl > 1e-6f )
			{
				fx /= fl;
				fy /= fl;
				fz /= fl;
			}
			// up = (0,1,0)
			float sx = fy * 0.0f - fz * 1.0f;
			float sy = fz * 0.0f - fx * 0.0f;
			float sz = fx * 1.0f - fy * 0.0f;
			float sl = std::sqrt( sx * sx + sy * sy + sz * sz );
			if ( sl > 1e-6f )
			{
				sx /= sl;
				sy /= sl;
				sz /= sl;
			}
			const float ux = sy * fz - sz * fy;
			const float uy = sz * fx - sx * fz;
			const float uz = sx * fy - sy * fx;

			setIdentity( out );
			out[0]	= sx;
			out[4]	= sy;
			out[8]	= sz;
			out[1]	= ux;
			out[5]	= uy;
			out[9]	= uz;
			out[2]	= -fx;
			out[6]	= -fy;
			out[10] = -fz;
			out[12] = -( sx * eyeX + sy * eyeY + sz * eyeZ );
			out[13] = -( ux * eyeX + uy * eyeY + uz * eyeZ );
			out[14] = -(-fx * eyeX + -fy * eyeY + -fz * eyeZ );
		}

		void perspective( float* out, float fovYDeg, float aspect, float zNear, float zFar )
		{
			setIdentity( out );
			const float f = 1.0f / std::tan( fovYDeg * 0.5f * 3.14159265f / 180.0f );
			out[0]		  = f / aspect;
			out[5]		  = f;
			out[10]		  = ( zFar + zNear ) / ( zNear - zFar );
			out[11]		  = -1.0f;
			out[14]		  = ( 2.0f * zFar * zNear ) / ( zNear - zFar );
			out[15]		  = 0.0f;
		}
	} // namespace

	GizmoPanel::GizmoPanel()
	{
		setIdentity( _matrix );
		_matrix[12] = 0.0f;
		_matrix[13] = 0.0f;
		_matrix[14] = 0.0f;
		lookAt( _view, 3.0f, 3.0f, 3.0f, 0.0f, 0.0f, 0.0f );
		perspective( _proj, 45.0f, 1.0f, 0.1f, 100.0f );
	}

	void GizmoPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), &_bOpen ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted( "ImGuizmo demo (local matrix). Operation:" );
		ImGui::RadioButton( "Translate", &_operation, 0 );
		ImGui::SameLine();
		ImGui::RadioButton( "Rotate", &_operation, 1 );
		ImGui::SameLine();
		ImGui::RadioButton( "Scale", &_operation, 2 );

		const ImVec2 canvasPos	= ImGui::GetCursorScreenPos();
		const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		if ( canvasSize.x < 64.0f || canvasSize.y < 64.0f )
		{
			ImGui::End();
			return;
		}

		ImGui::InvisibleButton( "gizmo_canvas", canvasSize );
		const ImVec2 canvasEnd( canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y );
		ImGui::GetWindowDrawList()->AddRectFilled( canvasPos, canvasEnd, IM_COL32( 28, 28, 32, 255 ) );

		const float aspect = canvasSize.x / canvasSize.y;
		perspective( _proj, 45.0f, aspect, 0.1f, 100.0f );

		ImGuizmo::SetDrawlist( ImGui::GetWindowDrawList() );
		ImGuizmo::SetRect( canvasPos.x, canvasPos.y, canvasSize.x, canvasSize.y );
		ImGuizmo::SetOrthographic( false );

		ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
		if ( _operation == 1 )
			op = ImGuizmo::ROTATE;
		else if ( _operation == 2 )
			op = ImGuizmo::SCALE;

		ImGuizmo::Manipulate( _view, _proj, op, ImGuizmo::LOCAL, _matrix );

		ImGui::End();
	}
} // namespace sw
