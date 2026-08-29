#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw::editor
{
	namespace
	{
		enum class MatchMode : uint8
		{
			EndsWith = 0,
			Extension,
			Scene,
			TileMap
		};

		struct TypeRow
		{
			EditorAssetKind	   _kind;
			MatchMode		   _mode;
			const string_view* _pSuffix;
			uint32			   _suffixCount;
		};

		constexpr string_view kArrPrefabSuffix[]	= { ".prefab.xml", ".prefab.json", ".prefab.bin", ".prefab", ".pfb" };
		constexpr string_view kArrTextureExt[]		= { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".hdr", ".bmp" };
		constexpr string_view kArrMaterialExt[]		= { "._material", ".mat", ".material" };
		constexpr string_view kArrShaderExt[]		= { ".hlsl", ".glsl", ".vert", ".frag", ".spv" };
		constexpr string_view kArrAudioExt[]		= { ".wav", ".mp3", ".ogg" };
		constexpr string_view kArrDataExt[]			= { ".xml", ".json", ".csv", ".ini", ".kv" };
		constexpr string_view kArrAnimSuffix[]		= { ".anim.json", ".anim" };
		constexpr string_view kArrDialogueSuffix[]	= { ".dialogue.json", ".dialogue" };
		constexpr string_view kArrSpriteDocSuffix[] = { ".sprite.json", ".sprite" };
		constexpr string_view kArrSpriteImageExt[]	= { ".png", ".jpg", ".jpeg", ".dds", ".tga" };
		constexpr string_view kArrSequenceSuffix[]	= { ".seq.json", ".seq" };

		const TypeRow kArrType[] = {
			{		  EditorAssetKind::Prefab,  MatchMode::EndsWith,	kArrPrefabSuffix,		  static_cast<uint32>( sizeof( kArrPrefabSuffix ) / sizeof( kArrPrefabSuffix[0] ) )},
			{		  EditorAssetKind::Texture, MatchMode::Extension,	  kArrTextureExt,			  static_cast<uint32>( sizeof( kArrTextureExt ) / sizeof( kArrTextureExt[0] ) )},
			{	  EditorAssetKind::Material, MatchMode::Extension,	   kArrMaterialExt,			static_cast<uint32>( sizeof( kArrMaterialExt ) / sizeof( kArrMaterialExt[0] ) )},
			{		  EditorAssetKind::Shader, MatchMode::Extension,		 kArrShaderExt,				static_cast<uint32>( sizeof( kArrShaderExt ) / sizeof( kArrShaderExt[0] ) )},
			{		  EditorAssetKind::Audio, MatchMode::Extension,		kArrAudioExt,				  static_cast<uint32>( sizeof( kArrAudioExt ) / sizeof( kArrAudioExt[0] ) )},
			{		  EditorAssetKind::Data, MatchMode::Extension,		   kArrDataExt,					static_cast<uint32>( sizeof( kArrDataExt ) / sizeof( kArrDataExt[0] ) )},
			{EditorAssetKind::AnimationGraph,  MatchMode::EndsWith,		kArrAnimSuffix,			static_cast<uint32>( sizeof( kArrAnimSuffix ) / sizeof( kArrAnimSuffix[0] ) )},
			{ EditorAssetKind::DialogueGraph,  MatchMode::EndsWith,  kArrDialogueSuffix,   static_cast<uint32>( sizeof( kArrDialogueSuffix ) / sizeof( kArrDialogueSuffix[0] ) )},
			{	  EditorAssetKind::SpriteClip,  MatchMode::EndsWith, kArrSpriteDocSuffix, static_cast<uint32>( sizeof( kArrSpriteDocSuffix ) / sizeof( kArrSpriteDocSuffix[0] ) )},
			{	  EditorAssetKind::SpriteClip, MatchMode::Extension,	 kArrSpriteImageExt,	 static_cast<uint32>( sizeof( kArrSpriteImageExt ) / sizeof( kArrSpriteImageExt[0] ) )},
			{	  EditorAssetKind::Sequence,	 MatchMode::EndsWith,  kArrSequenceSuffix,	  static_cast<uint32>( sizeof( kArrSequenceSuffix ) / sizeof( kArrSequenceSuffix[0] ) )},
			{		  EditorAssetKind::Scene,	  MatchMode::Scene,				nullptr,																					   0},
			{		  EditorAssetKind::TileMap,	MatchMode::TileMap,				nullptr,																					   0},
		};

		constexpr EditorAssetPanelMapping kArrPanelMapping[] = {
			{	  ".anim.json", "Animation Graph"},
			{		  ".anim", "Animation Graph"},
			{".dialogue.json",	"Dialogue Graph"},
			{	  ".dialogue",  "Dialogue Graph"},
			{  ".tilemap.xml",	  "Tile Map Tool"},
			{	  ".tilemap",	  "Tile Map Tool"},
			{	  ".prefab.xml",	 "Prefab Editor"},
			{  ".prefab.json",	  "Prefab Editor"},
			{	  ".prefab.bin",	 "Prefab Editor"},
			{		  ".prefab",	 "Prefab Editor"},
			{		  ".pfb",	  "Prefab Editor"},
			{  ".sprite.json",	  "Sprite Clip"},
			{		  ".sprite",	 "Sprite Clip"},
			{	  ".seq.json",	   "Sequencer"},
			{		  ".seq",		  "Sequencer"},
			{		  ".png",	  "Sprite Clip"},
			{		  ".jpg",	  "Sprite Clip"},
			{		  ".jpeg",	   "Sprite Clip"},
			{		  ".dds",	  "Sprite Clip"},
			{		  ".tga",	  "Sprite Clip"},
		};

		struct EditorAssetTypeInternal
		{
			static bool matchSuffixList( string_view path, const string_view* pSuffix, uint32 count, MatchMode mode )
			{
				if ( pSuffix == nullptr || count == 0 )
					return false;
				if ( mode == MatchMode::EndsWith )
				{
					for ( uint32 index = 0; index < count; ++index )
					{
						if ( FileUtil::endsWithIgnoreCase( path, pSuffix[index] ) )
							return true;
					}
					return false;
				}

				for ( uint32 index = 0; index < count; ++index )
				{
					if ( FileUtil::hasExtension( path, pSuffix[index] ) )
						return true;
				}
				return false;
			}

			static bool matchScene( string_view path )
			{
				if ( path.empty() )
					return false;
				if ( FileUtil::hasExtension( path, ".scene" ) )
					return true;
				if ( FileUtil::hasExtension( path, ".xml" ) )
				{
					const string pathStr{ path };
					if ( StringUtil::stristr( pathStr.c_str(), ".scene" ) != nullptr )
						return true;
				}
				return FileUtil::endsWithIgnoreCase( path, "_scene.xml" );
			}

			static bool matchTileMap( string_view path )
			{
				if ( FileUtil::endsWithAnyIgnoreCase( path, { ".tilemap.xml", ".tilemap" } ) )
					return true;
				if ( FileUtil::endsWithIgnoreCase( path, ".xml" ) == false )
					return false;
				if ( FileUtil::endsWithAnyIgnoreCase( path, { ".scene.xml", ".prefab.xml", ".preset.xml" } ) )
					return false;
				return true;
			}

			static bool matchRow( const TypeRow& row, string_view path )
			{
				if ( row._mode == MatchMode::Scene )
					return matchScene( path );
				if ( row._mode == MatchMode::TileMap )
					return matchTileMap( path );
				return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	bool EditorAssetTypeRegistry::matches( EditorAssetKind kind, string_view path )
	{
		if ( kind == EditorAssetKind::Unknown || path.empty() )
			return false;

		for ( const TypeRow& row : kArrType )
		{
			if ( row._kind != kind )
				continue;
			if ( EditorAssetTypeInternal::matchRow( row, path ) )
				return true;
		}
		return false;
	}

	bool EditorAssetTypeRegistry::matches( EditorAssetKind kind, const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return matches( kind, string_view{ pPath } );
	}

	const EditorAssetPanelMapping* EditorAssetTypeRegistry::getPanelMappings( uint32& outCount )
	{
		outCount = static_cast<uint32>( sizeof( kArrPanelMapping ) / sizeof( kArrPanelMapping[0] ) );
		return kArrPanelMapping;
	}
} // namespace sw::editor
