/**
 * @file SceneDocument.h
 * @brief 씬 문서 모델 및 XML/바이너리 직렬화 (씬 이름, 엔티티 노드 목록, 프리팹/임베디드 상태)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) SceneEntityNode — 씬 문서 내 단일 엔티티 정의 노드
	//    prefab 참조 또는 임베디드 GameObject 직렬화 XML 상태
	// ------------------------------------------------------------------------------
	struct SceneEntityNode
	{
		string _name;
		string _prefab;
		string _embeddedXml; ///< 선택적 ObjectStateSerializer XML (`GameObject` 문서)
	};

	// ------------------------------------------------------------------------------
	// 2) SceneDocument — 씬 메타데이터 및 엔티티 노드 목록 데이터 모델
	// ------------------------------------------------------------------------------
	struct SceneDocument
	{
		string					_name;
		string					_sourcePath;
		vector<SceneEntityNode> _listEntityNode;
		bool					_bValid{ false };
	};

	// ------------------------------------------------------------------------------
	// 3) I/O — 리소스 상대 또는 절대 경로 파일 입출력 (XML / Binary)
	// ------------------------------------------------------------------------------
	/** @brief 환경(Shipping vs Dev) 및 파일 존재 여부에 따라 최적의 포맷(Binary 우선)으로 씬 문서를 로드합니다. */
	SW_API bool loadSceneDocument( string_view path, SceneDocument& outDoc );

	/** @brief 리소스 상대/절대 경로에서 씬 문서 XML을 로드합니다. */
	SW_API bool loadSceneDocumentFromXml( string_view path, SceneDocument& outDoc );

	/** @brief 씬 문서 XML을 리소스 상대/절대 경로에 저장합니다. */
	SW_API bool saveSceneDocumentToXml( string_view path, const SceneDocument& doc );

	/** @brief 리소스 상대/절대 경로에서 씬 문서 바이너리(SCN1)를 로드합니다. */
	SW_API bool loadSceneDocumentFromBinary( string_view path, SceneDocument& outDoc );

	/** @brief 씬 문서 바이너리(SCN1)를 리소스 상대/절대 경로에 저장합니다. */
	SW_API bool saveSceneDocumentToBinary( string_view path, const SceneDocument& doc );

} // namespace sw
