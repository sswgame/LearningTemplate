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
	// SceneDocument — 씬 메타데이터 및 엔티티 노드 목록
	// ------------------------------------------------------------------------------
	struct SceneDocument
	{
		/** @brief 씬 문서 내 단일 엔티티 (프리팹 참조 또는 임베디드 GameObject XML) */
		struct EntityNode
		{
			string _name;
			string _prefab;
			string _prefabGuid;
			string _embeddedXml;
		};

		string			   _name;
		string			   _sourcePath;
		vector<EntityNode> _listEntityNode;
		bool			   _bValid{ false };

		/** @brief 환경(Shipping vs Dev) 및 파일 존재 여부에 따라 최적 포맷(Binary 우선)으로 로드합니다. */
		SW_API bool load( string_view path );
		/** @brief 리소스 상대/절대 경로에서 XML을 로드합니다. */
		SW_API bool loadXml( string_view path );
		/** @brief XML을 리소스 상대/절대 경로에 저장합니다. */
		SW_API bool saveXml( string_view path ) const;
		/** @brief 리소스 상대/절대 경로에서 바이너리(SCN1)를 로드합니다. */
		SW_API bool loadBinary( string_view path );
		/** @brief 바이너리(SCN1)를 리소스 상대/절대 경로에 저장합니다. */
		SW_API bool saveBinary( string_view path ) const;
	};

} // namespace sw
