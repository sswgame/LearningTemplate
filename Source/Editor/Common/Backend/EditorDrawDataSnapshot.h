/**
 * @file EditorDrawDataSnapshot.h
 * @brief ImGui DrawData와 멀티 뷰포트 그리기 명령을 클론해 렌더 스레드가 쓰게 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

struct ImDrawData;

namespace sw::editor
{
	/**
	 * @class EditorDrawDataSnapshot
	 * @brief EndFrame 이후 DrawData를 복사합니다. 원본은 다음 NewFrame에서 무효가 됩니다.
	 */
	class EditorDrawDataSnapshot
	{
	public:
		/** @brief 빈 스냅샷을 만듭니다. */
		EditorDrawDataSnapshot();
		/** @brief 클론한 DrawList를 해제합니다. */
		~EditorDrawDataSnapshot();

		EditorDrawDataSnapshot( const EditorDrawDataSnapshot& )			   = delete;
		EditorDrawDataSnapshot& operator=( const EditorDrawDataSnapshot& ) = delete;

		/** @brief 현재 ImGui 컨텍스트의 메인/보조 뷰포트 DrawData를 복사합니다. */
		void capture();
		/** @brief 소유한 클론을 모두 지웁니다. */
		void clear();
		/** @brief 캡처된 메인 DrawData가 있으면 true입니다. */
		bool isValid() const;
		/** @brief 렌더 스레드가 그릴 메인 DrawData입니다. 없으면 nullptr입니다. */
		ImDrawData* getMainDrawData();
		/** @brief 캡처 시점의 플랫폼 콜백으로 보조 뷰포트를 그립니다. */
		void presentExtraViewports();

	private:
		struct Impl;
		unique_ptr<Impl> _pImpl;
	};
} // namespace sw::editor
