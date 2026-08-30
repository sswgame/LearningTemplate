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
			Scene
		};

		struct TypeRow
		{
			EditorAssetKind	   _kind;
			MatchMode		   _mode;
			const string_view* _pSuffix;
			uint32			   _suffixCount;
		};

		struct KindTitleRow
		{
			EditorAssetKind _kind;
			const utf8*		_pTitle;
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
		constexpr string_view kArrTileMapSuffix[]	= { ".tilemap.xml", ".tilemap" };

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
			{		  EditorAssetKind::TileMap,	MatchMode::EndsWith,	 kArrTileMapSuffix,		static_cast<uint32>( sizeof( kArrTileMapSuffix ) / sizeof( kArrTileMapSuffix[0] ) )},
			{		  EditorAssetKind::Scene,	  MatchMode::Scene,				nullptr,																					   0},
		};

		constexpr KindTitleRow kArrKindTitle[] = {
			{		  EditorAssetKind::Prefab,   "Prefab Editor"},
			{EditorAssetKind::AnimationGraph, "Animation Graph"},
			{ EditorAssetKind::DialogueGraph,  "Dialogue Graph"},
			{	  EditorAssetKind::SpriteClip,	   "Sprite Clip"},
			{		  EditorAssetKind::TileMap,	"Tile Map Tool"},
			{	  EditorAssetKind::Sequence,		 "Sequencer"},
			{	  EditorAssetKind::Material,		 "Material"},
		};

		constexpr EditorAssetKind kArrToolPanelKind[] = {
			EditorAssetKind::AnimationGraph,
			EditorAssetKind::DialogueGraph,
			EditorAssetKind::Prefab,
			EditorAssetKind::SpriteClip,
			EditorAssetKind::TileMap,
			EditorAssetKind::Sequence,
			EditorAssetKind::Material,
		};

		constexpr EditorAssetPanelMapping kArrPanelMapping[] = {
			{EditorAssetKind::AnimationGraph,	   ".anim.json"},
			{EditorAssetKind::AnimationGraph,		   ".anim"},
			{ EditorAssetKind::DialogueGraph, ".dialogue.json"},
			{ EditorAssetKind::DialogueGraph,	  ".dialogue"},
			{		  EditorAssetKind::TileMap,	".tilemap.xml"},
			{		  EditorAssetKind::TileMap,		".tilemap"},
			{		  EditorAssetKind::Prefab,	   ".prefab.xml"},
			{		  EditorAssetKind::Prefab,   ".prefab.json"},
			{		  EditorAssetKind::Prefab,	   ".prefab.bin"},
			{		  EditorAssetKind::Prefab,		   ".prefab"},
			{		  EditorAssetKind::Prefab,		   ".pfb"},
			{	  EditorAssetKind::SpriteClip,   ".sprite.json"},
			{	  EditorAssetKind::SpriteClip,		   ".sprite"},
			{	  EditorAssetKind::Sequence,		 ".seq.json"},
			{	  EditorAssetKind::Sequence,			 ".seq"},
			{	  EditorAssetKind::Material,	 "._material"},
			{	  EditorAssetKind::Material,			 ".mat"},
			{	  EditorAssetKind::Material,		 ".material"},
			{	  EditorAssetKind::SpriteClip,		   ".png"},
			{	  EditorAssetKind::SpriteClip,		   ".jpg"},
			{	  EditorAssetKind::SpriteClip,		   ".jpeg"},
			{	  EditorAssetKind::SpriteClip,		   ".dds"},
			{	  EditorAssetKind::SpriteClip,		   ".tga"},
		};

		constexpr EditorAssetBrowserFilter kArrBrowserFilter[] = {
			{	  "All",		 EditorAssetKind::Unknown, false},
			{	  "Scenes",			EditorAssetKind::Scene, false},
			{  "Prefabs",		 EditorAssetKind::Prefab, false},
			{ "Textures",		  EditorAssetKind::Texture, false},
			{  "Shaders",		 EditorAssetKind::Shader, false},
			{"Materials",	   EditorAssetKind::Material, false},
			{	  "Audio",		   EditorAssetKind::Audio, false},
			{	  "Anim", EditorAssetKind::AnimationGraph, false},
			{ "Dialogue",  EditorAssetKind::DialogueGraph, false},
			{	  "Sprite",		EditorAssetKind::SpriteClip, false},
			{ "Tile Map",		  EditorAssetKind::TileMap, false},
			{	  "Seq",		 EditorAssetKind::Sequence, false},
			{	  "Data",			  EditorAssetKind::Data, false},
			{	  "Other",		   EditorAssetKind::Unknown,	 true},
		};

		constexpr EditorAssetKind kArrOtherExcludeKind[] = {
			EditorAssetKind::Scene,
			EditorAssetKind::Prefab,
			EditorAssetKind::Texture,
			EditorAssetKind::Shader,
			EditorAssetKind::Material,
			EditorAssetKind::Audio,
			EditorAssetKind::AnimationGraph,
			EditorAssetKind::DialogueGraph,
			EditorAssetKind::SpriteClip,
			EditorAssetKind::TileMap,
			EditorAssetKind::Sequence,
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

			static bool matchRow( const TypeRow& row, string_view path )
			{
				if ( row._mode == MatchMode::Scene )
					return matchScene( path );
				return matchSuffixList( path, row._pSuffix, row._suffixCount, row._mode );
			}

			static bool containsSuffix( const vector<string>& listSuffix, string_view suffix )
			{
				for ( const string& existing : listSuffix )
				{
					if ( FileUtil::endsWithIgnoreCase( existing, suffix ) && existing.size() == suffix.size() )
						return true;
				}
				return false;
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

	bool EditorAssetTypeRegistry::matchesAny( string_view path )
	{
		if ( path.empty() )
			return false;
		for ( const TypeRow& row : kArrType )
		{
			if ( EditorAssetTypeInternal::matchRow( row, path ) )
				return true;
		}
		return false;
	}

	bool EditorAssetTypeRegistry::matchesOther( string_view path )
	{
		if ( path.empty() )
			return false;
		for ( const EditorAssetKind kind : kArrOtherExcludeKind )
		{
			if ( matches( kind, path ) )
				return false;
		}
		return true;
	}

	const utf8* EditorAssetTypeRegistry::getPanelTitle( EditorAssetKind kind )
	{
		for ( const KindTitleRow& row : kArrKindTitle )
		{
			if ( row._kind == kind )
				return row._pTitle;
		}
		return "";
	}

	string_view EditorAssetTypeRegistry::findPanelTitleForPath( string_view assetPath )
	{
		if ( assetPath.empty() )
			return {};

		size_t			bestLen{ 0 };
		EditorAssetKind bestKind{ EditorAssetKind::Unknown };
		for ( const EditorAssetPanelMapping& mapping : kArrPanelMapping )
		{
			if ( FileUtil::endsWithIgnoreCase( assetPath, mapping._suffix ) == false )
				continue;
			if ( mapping._suffix.size() <= bestLen )
				continue;
			bestLen	 = mapping._suffix.size();
			bestKind = mapping._kind;
		}
		if ( bestKind == EditorAssetKind::Unknown )
			return {};
		return getPanelTitle( bestKind );
	}

	const EditorAssetPanelMapping* EditorAssetTypeRegistry::getPanelMappings( uint32& outCount )
	{
		outCount = static_cast<uint32>( sizeof( kArrPanelMapping ) / sizeof( kArrPanelMapping[0] ) );
		return kArrPanelMapping;
	}

	const EditorAssetKind* EditorAssetTypeRegistry::getToolPanelKinds( uint32& outCount )
	{
		outCount = static_cast<uint32>( sizeof( kArrToolPanelKind ) / sizeof( kArrToolPanelKind[0] ) );
		return kArrToolPanelKind;
	}

	const EditorAssetBrowserFilter* EditorAssetTypeRegistry::getBrowserFilters( uint32& outCount )
	{
		outCount = static_cast<uint32>( sizeof( kArrBrowserFilter ) / sizeof( kArrBrowserFilter[0] ) );
		return kArrBrowserFilter;
	}

	void EditorAssetTypeRegistry::appendImportExtensions( vector<string>& outListExtension )
	{
		for ( const TypeRow& row : kArrType )
		{
			if ( row._kind == EditorAssetKind::Data || row._kind == EditorAssetKind::Scene )
				continue;
			if ( row._pSuffix == nullptr )
				continue;
			for ( uint32 index = 0; index < row._suffixCount; ++index )
			{
				const string_view suffix = row._pSuffix[index];
				if ( EditorAssetTypeInternal::containsSuffix( outListExtension, suffix ) )
					continue;
				outListExtension.push_back( string{ suffix } );
			}
		}
		if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".json" ) == false )
			outListExtension.push_back( ".json" );
		if ( EditorAssetTypeInternal::containsSuffix( outListExtension, ".txt" ) == false )
			outListExtension.push_back( ".txt" );
	}
} // namespace sw::editor
