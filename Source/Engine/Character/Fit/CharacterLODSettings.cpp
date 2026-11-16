#include "pch.h"

#include "Engine/Character/Fit/CharacterLODSettings.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/formatString.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include <meshoptimizer.h>

namespace sw
{
    bool CharacterLODSettings::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        XMLDocument document;
        XMLNode     root;
        return loadFromRoot( XMLReadUtil::parseRoot( document, xml, sourceLabel, "CharacterLODSettings", root ), root, sourceLabel );
    }

    bool CharacterLODSettings::loadFromRoot( XMLRootStatus status, const XMLNode& root, string_view sourceLabel )
    {
        *this = CharacterLODSettings{};
        if ( status != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "CharacterMeshLOD.SettingsMalformed", "Character LOD settings '%#': malformed XML (expected a <CharacterLODSettings> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
            return true;
        *this = CharacterLODSettings{};
        return false;
    }

    bool CharacterLODSettings::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "CharacterMeshLOD.SettingsCouldNotRead", "Character LOD settings '%#' could not be read", assetID.view() );
            *this = CharacterLODSettings{};
            return false;
        }
        XMLDocument document;
        XMLNode     root;
        return loadFromRoot( XMLReadUtil::parseResourceRoot( document, text, assetID, "CharacterLODSettings", root ), root, assetID.view() );
    }

    const CharacterLODSettings& CharacterLODSettings::getDefault()
    {
        // 한 번 읽는다 — 표는 엔진 기본 데이터이고 합친 메시의 캐시 키에 들지 않는다(표를 고치면 다시 시작한다).
        static const CharacterLODSettings s_settings = []()
        {
            CharacterLODSettings settings;
            (void)settings.loadFromAsset( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._characterLODSettings.get() } ); // 실패는 읽기가 알렸다 — 빈 표(LOD 0 만)
            return settings;
        }();
        return s_settings;
    }

    bool CharacterLODSettings::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( root, {}, { "Level" }, sourceLabel ) == false )
            return false;
        float32 previousRatio  = 1.0f;
        float32 previousScreen = 1.0f;
        for ( XMLNode level = root.findChild( "Level" ); level.isValid(); level = level.findNextSibling( "Level" ) )
        {
            CharacterLODLevelRule rule;
            if ( XMLFields::readProperties( level, rule, sourceLabel ) == false )
                return false;
            // 단계가 낮아질수록 비율 · 임계값이 작아져야 한다(`Mesh::setLODs` 와 같은 규칙) — 칸 · 범위는 PROPERTY 가 보고 여기서는 앞 단계와의 관계만 본다.
            const bool bValid = 0.0f < rule._triangleRatio && rule._triangleRatio < previousRatio && 0.0f < rule._screenSize && rule._screenSize < previousScreen;
            if ( bValid == false )
            {
                SW_LOG_ERROR_ID( "CharacterMeshLOD.SettingsLevelInvalid", "Character LOD settings '%#': level %# needs triangle_ratio and screen_size in (0, previous level)", sourceLabel,
                                 static_cast<uint32>( _listLevel.size() ) + 1u );
                return false;
            }
            _listLevel.push_back( rule );
            previousRatio  = rule._triangleRatio;
            previousScreen = rule._screenSize;
        }
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct CharacterMeshLODInternal
        {
            /** @brief 인덱스 범위 하나(구간 또는 메시 전체)를 단순화해 @p outListIndex 끝에 붙입니다(번호는 LOD 0 정점). @return 붙인 인덱스 수입니다. */
            static size_t simplifyRange( const Mesh& mesh, uint32 firstIndex, uint32 indexCount, const CharacterLODLevelRule& rule, vector<uint32>& outListIndex )
            {
                if ( indexCount == 0 )
                    return 0;
                const vector<RHIVertex>& listVertex  = mesh.getVertices();
                const uint32*            pSource     = mesh.getIndices().data() + firstIndex;
                const size_t             targetCount = MathUtil::max( static_cast<size_t>( static_cast<float32>( indexCount / 3u ) * rule._triangleRatio ), size_t( 1 ) ) * 3u;
                const size_t             start       = outListIndex.size();
                outListIndex.resize( start + indexCount );
                // 언리얼 스켈레탈 메시 감소처럼 가까운 단계는 경계 · 이음매를 지키고 먼 단계는 풀어 준다(표가 단계마다 정한다). 법선을 오차에 넣어 이음매를 넘어도 음영이 덜 망가진다.
                const unsigned int options = ( rule._bLockBorder ? meshopt_SimplifyLockBorder : 0u ) | ( rule._bPermissive ? meshopt_SimplifyPermissive : 0u ) |
                                             ( rule._bPrune ? meshopt_SimplifyPrune : 0u );
                constexpr float32 arrNormalWeight[3] = { 0.5f, 0.5f, 0.5f };
                const size_t      kept               = meshopt_simplifyWithAttributes( outListIndex.data() + start, pSource, indexCount, &listVertex[0]._arrPosition[0], listVertex.size(), sizeof( RHIVertex ),
                                                                                       &listVertex[0]._arrNormal[0], sizeof( RHIVertex ), arrNormalWeight, 3, nullptr, targetCount, rule._maxError, options, nullptr );
                // 단순화는 삼각형 순서를 정점 캐시에 맞추지 않는다 — 다시 맞춘다(meshoptimizer 권장 순서, 임포터 `ModelImportGeometry` 와 같다).
                meshopt_optimizeVertexCache( outListIndex.data() + start, outListIndex.data() + start, kept, listVertex.size() );
                outListIndex.resize( start + kept );
                return kept;
            }
        };
    } // namespace

    bool CharacterLODSettings::buildLODs( Mesh& inoutMesh, const CharacterLODSettings& settings, const vector<CharacterLODSectionRule>& listRule )
    {
        using Internal = CharacterMeshLODInternal;
        if ( settings._listLevel.empty() || inoutMesh.getIndexCount() == 0 )
            return true;
        const vector<MeshSection>&    listSection = inoutMesh.getSections();
        const vector<RHIVertex>&      listVertex  = inoutMesh.getVertices();
        const vector<MeshSkinVertex>& listSkin    = inoutMesh.getSkinVertices();
        vector<MeshLODLevel>          listLevel;
        vector<uint32>                listLevelIndex;
        vector<uint32>                listNewIndex;
        for ( size_t levelIndex = 0; levelIndex < settings._listLevel.size(); ++levelIndex )
        {
            const CharacterLODLevelRule& rule = settings._listLevel[levelIndex];
            MeshLODLevel                 level{};
            level._screenSize = rule._screenSize;
            listLevelIndex.clear();
            // 구간마다 따로 줄인다 — 머티리얼 경계가 섞이지 않고 구간 범위가 그대로 이어 붙는다.
            if ( listSection.empty() )
                (void)Internal::simplifyRange( inoutMesh, 0, inoutMesh.getIndexCount(), rule, listLevelIndex );
            for ( const MeshSection& section : listSection )
            {
                const uint32                   first        = static_cast<uint32>( listLevelIndex.size() );
                const CharacterLODSectionRule* pSectionRule = VectorUtil::findBy<&CharacterLODSectionRule::_section>( listRule, section._name );
                const bool                     bDropped     = pSectionRule != nullptr && pSectionRule->_dropLevel > 0 && levelIndex + 1u >= pSectionRule->_dropLevel;
                if ( bDropped )
                {
                    level._listSectionRange.push_back( MeshLODSectionRange{ first, 0u } ); // 빈 범위 — 이 단계부터 그 구간을 그리지 않는다
                    continue;
                }
                CharacterLODLevelRule sectionLevelRule = rule;
                if ( pSectionRule != nullptr )
                {
                    sectionLevelRule._triangleRatio = MathUtil::min( rule._triangleRatio * pSectionRule->_detail, 1.0f );
                    sectionLevelRule._maxError      = rule._maxError / pSectionRule->_detail;
                }
                const uint32 kept = static_cast<uint32>( Internal::simplifyRange( inoutMesh, section._firstIndex, section._indexCount, sectionLevelRule, listLevelIndex ) );
                level._listSectionRange.push_back( MeshLODSectionRange{ first, kept } );
            }
            if ( listLevelIndex.empty() || listLevelIndex.size() >= inoutMesh.getIndexCount() )
            {
                SW_LOG_ERROR_ID( "CharacterMeshLOD.LevelNotReduced", "Character LOD %# kept %# of %# indices - adjust triangle_ratio / max_error (engine/character/characterlod.xml), the mesh keeps LOD 0 only",
                                 static_cast<uint32>( levelIndex ) + 1u, static_cast<uint32>( listLevelIndex.size() ), inoutMesh.getIndexCount() );
                return false;
            }
            // 이 단계가 쓰는 정점만 처음 쓰인 순서로 모은다 — 고른 단계 하나만 스키닝하므로 정점이 적을수록 싸다.
            listNewIndex.assign( listVertex.size(), constant::invalid_index::kUint32 );
            for ( uint32& index : listLevelIndex )
            {
                if ( listNewIndex[index] == constant::invalid_index::kUint32 )
                {
                    listNewIndex[index] = static_cast<uint32>( level._listVertex.size() );
                    level._listVertex.push_back( listVertex[index] );
                    if ( listSkin.empty() == false )
                        level._listSkinVertex.push_back( listSkin[index] );
                }
                index = listNewIndex[index];
            }
            level._listIndex = listLevelIndex;
            listLevel.push_back( std::move( level ) );
        }
        utf8 arrSummary[constant::buffer::kMax256]{};
        for ( const MeshLODLevel& level : listLevel )
        {
            formatstring( arrSummary + std::strlen( arrSummary ), constant::buffer::kMax256 - std::strlen( arrSummary ), " %#/%#", static_cast<uint32>( level._listIndex.size() / 3u ),
                          static_cast<uint32>( level._listVertex.size() ) );
        }
        if ( inoutMesh.setLODs( std::move( listLevel ) ) == false )
            return false; // setLODs 가 까닭을 남겼다
        // 단계마다 남은 삼각형 / 정점 — 표(characterlod.xml)를 맞출 때 본다. 차림마다 한 번이다.
        SW_LOG_INFO_ID( "CharacterMeshLOD.LevelsBuilt", "Character LODs built from %# triangles / %# vertices:%#", inoutMesh.getIndexCount() / 3u, inoutMesh.getVertexCount(), arrSummary );
        return true;
    }

    /** @brief 캐릭터 LOD 표 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( CharacterLOD, AssetTypeDef::makeDataFile( "characterlod", AssetPathRule{ {}, {}, "/character/characterlod.xml" }, AssetTypeLoader::of<CharacterLODSettings>() ) );
} // namespace sw
