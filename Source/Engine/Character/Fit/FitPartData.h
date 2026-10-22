/**
 * @file FitPartData.h
 * @brief 장비(부품) 하나의 피팅 데이터(`*.partfit.xml`)입니다. 겹, 프로필, 숨김 영역, 조임 고리, 보정 조각을 담습니다. 메시와 따로 둔 원본 에셋입니다.
 * @details 소켓처럼 재임포트가 지우지 않는다. 겹, 프로필, 영역 이름은 피팅 테이블(`FitTables`)과 만나는 곳에서 대조한다(`validateNames`, 모르면 오류다).
 *          보정 조각의 `vertex` 는 메시를 위치로 이은 형상의 정점 번호다(`CharacterMeshUtil::makeWeldedGeometry` — 피팅이 푸는 형상).
 *
 *          XML 예:
 *          @code
 *          <PartFit layer="Bracelet" profile="Rigid">
 *            <Hide region="Forearm_L"/>
 *            <Ring bone="lowerarm_l" offset="0 0.2 0" axis="0 1 0" radius="0.045" width="0.03"/>
 *            <Corrective morph="Heavy">
 *              <Delta vertex="12" offset="0 0 0.01"/>
 *            </Corrective>
 *          </PartFit>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    class CharacterDataReader;
    class FitTables;
    class XMLNode;

    /**
     * @brief 손으로 적은 조임 고리입니다(본, 축, 반지름, 폭). 메시의 안면 대신 이 고리로 안쪽 겹을 조입니다.
     * @details 중심은 본의 바인드 모델 변환으로 옮긴 `_offset`, 축은 같은 변환으로 돌린 `_axis` 입니다. 폭 안은 반지름까지, 폭 밖은 프로필의
     *          감쇠 폭을 따라 부드럽게 원래 반지름으로 돌아갑니다.
     */
    struct SW_API FitRingDef
    {
        hashed_string _bone{};
        float3        _offset{};
        float3        _axis{ 0.0f, 1.0f, 0.0f };
        float32       _radius{ 0.05f };
        float32       _width{ 0.02f };
    };
} // namespace sw

namespace sw
{
    /** @brief 보정 조각의 정점 하나입니다. */
    struct SW_API FitCorrectiveDelta
    {
        float3 _offset{};
        uint32 _vertex{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 손 수정 델타 조각 하나입니다. 모프가 비면 항상 적용하고, 있으면 그 체형 모프의 가중치만큼 적용합니다(체형별 보정). */
    struct SW_API FitCorrectiveDef
    {
        hashed_string              _morph{};
        vector<FitCorrectiveDelta> _listDelta{};
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 하나의 피팅 데이터입니다. */
    class SW_API FitPartData
    {
    public:
        /** @brief XML 텍스트에서 읽습니다. 모르는 속성 · 원소와 빠진 값은 오류입니다. 겹 · 프로필 · 영역 이름은 피팅 표와 만나는 곳에서 `validateNames` 로 대조합니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /**
         * @brief 겹 · 프로필 · 숨김 영역 이름이 @p tables 에 있는지 봅니다.
         * @details 부품 피팅 데이터는 표와 따로 읽는 에셋이다(같은 장비를 표가 다른 캐릭터가 입는다, 공유 캐시가 경로만으로 나눠 준다). 그래서 대조는 읽을 때가 아니라
         *          둘이 만나는 조립(외형 메시 조립)과 데이터 검사에서 한다.
         * @return 없는 이름이 있으면 오류 글을 더하고 false 입니다.
         */
        bool validateNames( const FitTables& tables, string* pOutError ) const;
        /**
         * @brief 고리의 본이 @p bones 에, 보정 정점이 @p geometry 안에 있는지 봅니다(널이면 그쪽은 건너뜀).
         * @return 아니면 오류 글을 더하고 false 입니다.
         */
        bool validate( const CharacterBoneArray* pBones, const AppearanceGeometry* pGeometry, string* pOutError ) const;

        /** @brief 겹 이름입니다. */
        const hashed_string& getLayer() const { return _layer; }
        /** @brief 프로필 이름입니다. */
        const hashed_string& getProfile() const { return _profile; }
        /** @brief 이 부품이 입혀지면 안쪽 겹에서 숨길 몸 영역들입니다. */
        const vector<hashed_string>& getHiddenRegions() const { return _listHiddenRegion; }
        /** @brief 조임 고리들입니다. */
        const vector<FitRingDef>& getRings() const { return _listRing; }
        /** @brief 보정 조각들입니다. */
        const vector<FitCorrectiveDef>& getCorrectives() const { return _listCorrective; }

        /** @brief 겹과 프로필을 정합니다(코드로 지을 때). */
        void setLayerAndProfile( const hashed_string& layer, const hashed_string& profile );
        /** @brief 숨길 영역을 더합니다. */
        void addHiddenRegion( const hashed_string& region ) { _listHiddenRegion.push_back( region ); }
        /** @brief 조임 고리를 더합니다. */
        void addRing( const FitRingDef& ring ) { _listRing.push_back( ring ); }
        /** @brief 보정 조각을 더합니다. */
        void addCorrective( const FitCorrectiveDef& corrective ) { _listCorrective.push_back( corrective ); }

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );

    private:
        vector<hashed_string>    _listHiddenRegion;
        vector<FitRingDef>       _listRing;
        vector<FitCorrectiveDef> _listCorrective;
        hashed_string            _layer;
        hashed_string            _profile;
    };
} // namespace sw
