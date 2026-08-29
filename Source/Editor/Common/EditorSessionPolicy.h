/**
 * @file EditorSessionPolicy.h
 * @brief 미저장 확인·플레이 중 편집 허용 여부 (UI 없이 테스트 가능)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
	/** @brief 미저장 모달에서 고른 항목 */
	enum class EditorUnsavedChoice : uint8
	{
		None = 0,
		Save,
		Discard,
		Cancel
	};

	/** @brief 미저장 확인 뒤에 이어서 할 씬·세션 동작 */
	enum class EditorPendingSceneAction : uint8
	{
		None = 0,
		Load,
		New,
		Quit
	};

	/** @brief 문서 전/후 텍스트의 공통 접두·접미를 뺀 변경 구간 */
	struct EditorDocumentTextSpan
	{
		uint32 prefixLength{ 0 };
		uint32 suffixLength{ 0 };
		string removed;
		string added;
	};

	/**
	 * @class EditorSessionPolicy
	 * @brief 씬 dirty / 플레이 세션에 대한 가드 결정
	 */
	class EditorSessionPolicy
	{
	public:
		/** @brief 저장하지 않은 변경이 있으면 확인이 필요합니다. */
		static bool needsUnsavedPrompt( bool bDirty ) { return bDirty == true; }
		/** @brief 씬 또는 도구 문서가 dirty면 종료 확인이 필요합니다. */
		static bool needsQuitPrompt( bool bSceneDirty, uint32 dirtyDocumentCount )
		{
			return bSceneDirty == true || dirtyDocumentCount > 0;
		}
		/** @brief Save를 고르면 동작을 실행하기 전에 저장합니다. */
		static bool shouldSaveBeforeAction( EditorUnsavedChoice choice ) { return choice == EditorUnsavedChoice::Save; }
		/** @brief Cancel이 아니면 대기 중인 씬 동작을 실행합니다. */
		static bool shouldProceedWithAction( EditorUnsavedChoice choice )
		{
			return choice == EditorUnsavedChoice::Save || choice == EditorUnsavedChoice::Discard;
		}
		/** @brief Don't Save면 저장하지 않고 dirty를 지웁니다. */
		static bool shouldClearDirtyWithoutSave( EditorUnsavedChoice choice )
		{
			return choice == EditorUnsavedChoice::Discard;
		}
		/** @brief Stopped일 때만 씬 오브젝트 편집이 허용됩니다. */
		static bool areSceneEditsAllowed( bool bPlayStopped ) { return bPlayStopped == true; }
		/** @brief Isolation은 활성 씬을 유지하므로 dirty 씬에서도 들어갈 수 있습니다. */
		static bool requiresCleanSceneForPrefabIsolation() { return false; }
		/** @brief 레이아웃이 한 번 동기된 뒤에만 노드 이동을 dirty로 칩니다. */
		static bool shouldMarkDocumentDirtyOnNodeMove( bool bLayoutReady, bool bPositionChanged )
		{
			return bLayoutReady == true && bPositionChanged == true;
		}
		/** @brief 로컬라이즈·게임 데이터·세션 GV 중 하나라도 dirty면 도구 세션이 dirty입니다. */
		static bool isToolSessionDirty( bool bLocalizationDirty, bool bGameDataDirty, bool bGlobalVariableDirty )
		{
			return bLocalizationDirty == true || bGameDataDirty == true || bGlobalVariableDirty == true;
		}
		/** @brief 같은 coalesce 키만 연속 편집으로 합칩니다. 빈 키는 합치지 않습니다. */
		static bool shouldCoalesceDocumentEdits( string_view previousKey, string_view nextKey )
		{
			if ( previousKey.empty() || nextKey.empty() )
				return false;
			return previousKey == nextKey;
		}
		/** @brief Undo 복원 텍스트가 마지막 저장본과 같으면 dirty를 지웁니다. */
		static bool shouldClearDocumentDirtyOnRestore( bool bMatchesLastSaved )
		{
			return bMatchesLastSaved == true;
		}

		/** @brief 전/후 텍스트의 공통 접두·접미를 빼고 중간만 남깁니다. */
		static EditorDocumentTextSpan makeDocumentTextSpan( string_view before, string_view after )
		{
			EditorDocumentTextSpan span{};
			const size_t		   beforeSize = before.size();
			const size_t		   afterSize  = after.size();
			const size_t		   minSize	  = ( beforeSize < afterSize ) ? beforeSize : afterSize;
			size_t				   prefix	  = 0;
			while ( prefix < minSize && before[prefix] == after[prefix] )
				++prefix;

			size_t suffix = 0;
			while ( suffix < ( beforeSize - prefix ) && suffix < ( afterSize - prefix ) &&
					before[beforeSize - 1 - suffix] == after[afterSize - 1 - suffix] )
				++suffix;

			span.prefixLength = static_cast<uint32>( prefix );
			span.suffixLength = static_cast<uint32>( suffix );
			span.removed	  = string{ before.substr( prefix, beforeSize - prefix - suffix ) };
			span.added		  = string{ after.substr( prefix, afterSize - prefix - suffix ) };
			return span;
		}

		/** @brief after 텍스트와 스팬으로 편집 전 본문을 만듭니다. */
		static string reconstructDocumentTextFromAfter( const EditorDocumentTextSpan& span, string_view afterText )
		{
			const size_t prefixLength = static_cast<size_t>( span.prefixLength );
			const size_t suffixLength = static_cast<size_t>( span.suffixLength );
			if ( afterText.size() < prefixLength + suffixLength )
				return string{ afterText };
			string result;
			result.reserve( prefixLength + span.removed.size() + suffixLength );
			result.append( afterText.data(), prefixLength );
			result.append( span.removed );
			if ( suffixLength > 0 )
				result.append( afterText.data() + ( afterText.size() - suffixLength ), suffixLength );
			return result;
		}

		/** @brief before 텍스트와 스팬으로 편집 후 본문을 만듭니다. */
		static string reconstructDocumentTextFromBefore( const EditorDocumentTextSpan& span, string_view beforeText )
		{
			const size_t prefixLength = static_cast<size_t>( span.prefixLength );
			const size_t suffixLength = static_cast<size_t>( span.suffixLength );
			if ( beforeText.size() < prefixLength + suffixLength )
				return string{ beforeText };
			string result;
			result.reserve( prefixLength + span.added.size() + suffixLength );
			result.append( beforeText.data(), prefixLength );
			result.append( span.added );
			if ( suffixLength > 0 )
				result.append( beforeText.data() + ( beforeText.size() - suffixLength ), suffixLength );
			return result;
		}
	};
} // namespace sw::editor
