/**
 * @file EditorViewportPreview.h
 * @brief 도구 패널 미리보기를 선택 오브젝트·씬에 적용합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	class Material;
	class SequenceAsset;
} // namespace sw

namespace sw::editor
{
	/**
	 * @class EditorViewportPreview
	 * @brief Anim / Dialogue / Sequencer / Material 미리보기를 Game View에 반영합니다.
	 */
	class EditorViewportPreview
	{
	public:
		/** @brief 선택 오브젝트의 스프라이트 애니메이터에 노드 이름을 재생합니다. */
		static void applyAnimationNode( string_view nodeName );
		/** @brief 시퀀스 프레임의 활성 클립을 이름 대상 오브젝트에 적용합니다. */
		static void applySequenceFrame( const sw::SequenceAsset& asset, int32 frame );
		/** @brief 화자 이름의 오브젝트를 선택하고 대사를 로그합니다. */
		static void applyDialogueLine( string_view speaker, string_view text );
		/** @brief 선택 메시/스프라이트에 머티리얼을 붙이고 캐시를 갱신합니다. */
		static void applyMaterial( sw::Material* pMaterial, string_view assetPath );
	};
} // namespace sw::editor
