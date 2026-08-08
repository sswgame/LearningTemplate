#pragma once
/**
 * @file SceneDescriptor.h
 * @brief Minimal XML scene descriptor (name + entity placeholders)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	struct SceneEntityPlaceholder
	{
		std::string _name;
		std::string _prefab;
	};

	struct SceneDescriptor
	{
		std::string							 _name;
		std::string							 _sourcePath;
		std::vector<SceneEntityPlaceholder>	 _entities;
		bool								 _bValid = false;
	};

	/** @brief Load a scene descriptor XML from a resource-relative or absolute path. */
	SW_API bool loadSceneDescriptorFromXml( const std::string& path, SceneDescriptor& outDesc );
} // namespace sw
