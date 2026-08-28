/**
 * @file SceneDescriptor.h
 * @brief 씬 디스크립터 XML (이름 + 엔티티 플레이스홀더 / 프리팹 / 임베디드 GO 상태)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) SceneEntityPlaceholder — 씬 XML의 엔티티 한 줄
	//    prefab 또는 ObjectStateSerializer 임베디드 XML
	// ------------------------------------------------------------------------------
	struct SceneEntityPlaceholder
	{
		string _name;
		string _prefab;
		string _embeddedXml; ///< 선택 ObjectStateSerializer XML (`GameObject` 문서)
	};

	// ------------------------------------------------------------------------------
	// 2) SceneDescriptor — 로드된 씬 메타 + 엔티티 목록
	// ------------------------------------------------------------------------------
	struct SceneDescriptor
	{
		string						   _name;
		string						   _sourcePath;
		vector<SceneEntityPlaceholder> _listEntities;
		bool						   _bValid{ false };
	};

	// ------------------------------------------------------------------------------
	// 3) I/O — 리소스 상대 또는 절대 경로 (XML / Binary)
	// ------------------------------------------------------------------------------
	/** @brief 환경(Shipping vs Dev) 및 파일 존재 여부에 따라 최적의 포맷(Binary 우선)으로 씬 디스크립터를 로드합니다. */
	SW_API bool loadSceneDescriptor( string_view path, SceneDescriptor& outDesc );

	/** @brief 리소스 상대/절대 경로에서 씬 디스크립터 XML을 로드합니다. */
	SW_API bool loadSceneDescriptorFromXml( string_view path, SceneDescriptor& outDesc );

	/** @brief 디스크립터 XML을 리소스 상대/절대 경로에 씁니다. */
	SW_API bool saveSceneDescriptorToXml( string_view path, const SceneDescriptor& desc );

	/** @brief 리소스 상대/절대 경로에서 씬 디스크립터 바이너리(SCN1)를 로드합니다. */
	SW_API bool loadSceneDescriptorFromBinary( string_view path, SceneDescriptor& outDesc );

	/** @brief 디스크립터 바이너리(SCN1)를 리소스 상대/절대 경로에 씁니다. */
	SW_API bool saveSceneDescriptorToBinary( string_view path, const SceneDescriptor& desc );
} // namespace sw
