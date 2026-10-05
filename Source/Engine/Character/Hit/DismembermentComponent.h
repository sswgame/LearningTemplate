/**
 * @file DismembermentComponent.h
 * @brief 절단 런타임 — 치명적 맞음이 잘라 낼 수 있는 몸 영역에 들면 그 영역을 몸 메시에서 빼고(+ 자른 자리 캡), 떨어진 조각을 자기 강체(볼록 껍질)를 가진
 *        오브젝트로 띄우고, 그 영역의 표면 상태에 피를 올립니다.
 * @details 형상은 `DismembermentUtil::severRegions`(영역 → 잘린 삼각형 마스크 · 캡 둘)가 하고, 이 컴포넌트는 메시 · 포즈 · 물리와의 이음입니다:
 *          1. 유닛의 지금 스킨드 메시(인덱스 없는 삼각형 목록)를 자리로 이어(용접) 위상을 얻고, 몸 영역 표(`*.fit.xml` 의 `<Region>` — 본 가중치)로 정점마다 영역을 매깁니다.
 *          2. 잘린 삼각형은 유닛 메시에서 빼고(남은 삼각형은 원래 정점 그대로 — UV 이음매를 지킨다) 남은 몸 캡을 스킨 정점으로 붙입니다(보임 마스크 길).
 *          3. 떨어진 조각(삼각형 + 조각 캡)은 지금 포즈로 CPU 스키닝해 정적 메시로 만들고, 그 정점의 볼록 껍질을 셰이프로 한 동적 강체 오브젝트로 띄웁니다
 *             (같은 머티리얼, 레이어 `_pieceLayer`, 충격량 = 맞음 방향 × 충격량). 래그돌이 있으면 그 영역 뼈의 바디는 끕니다(보이지 않는 바디가 남지 않게).
 *          4. 표면 상태(`CharacterSurfaceState` — 엔진 기본 채널 표)의 그 영역에 `_bloodChannel` 을 1 로.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class DismembermentComponent
     * @brief 파일 머리말 참고. 같은 오브젝트의 `SkeletalMeshComponent`(스킨드 메시 · 스켈레톤)를 씁니다.
     */
    REFLECT( Category = "Character", DisplayName = "Dismemberment", Tooltip = "Severs body regions on fatal hits: hides the triangles, caps the cut and spawns a physics piece" )
    class SW_API DismembermentComponent : public Component
    {
    public:
        REFLECT_BODY();

        DismembermentComponent();
        virtual ~DismembermentComponent() override = default;

        void onBeginPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 치명적 맞음(`HitInfo::_bFatal`)이 잘라 낼 수 있는 영역의 뼈에 들면 그 영역을 자릅니다. */
        void onHitReceived( const HitInfo& hit ) override;

        /** @brief 몸 영역 표 경로를 바꾸고 다시 읽습니다. */
        void setRegionTablePath( string_view path );
        /** @brief 잘라 낼 수 있는 영역 이름들을 정합니다. */
        void setSeverableRegions( const vector<hashed_string>& listRegion ) { _listSeverableRegion = listRegion; }
        /**
         * @brief 영역 하나를 자릅니다(게임 스레드, 틱 밖). 떨어진 조각에 @p impulse 를 줍니다.
         * @return 자른 삼각형이 없거나(이미 잘림 · 영역이 비었다) 유닛 · 메시 · 표가 없으면 false 입니다.
         */
        bool severRegion( const hashed_string& region, const float3& impulse );
        /** @brief 뼈가 든 영역 이름입니다(표의 본 목록). 없으면 빈 이름입니다. */
        hashed_string findRegionOfBone( const hashed_string& bone ) const;
        /** @brief 잘린 영역들입니다. */
        const vector<hashed_string>& getSeveredRegions() const { return _listSeveredRegion; }
        /** @brief 마지막으로 띄운 조각 오브젝트입니다. */
        GameObjectHandle getLastPiece() const { return _lastPiece; }
        /** @brief 표면 상태(영역 × 채널)입니다 — 머티리얼 파라미터가 읽을 자리입니다. */
        const CharacterSurfaceState& getSurfaceState() const { return _surfaceState; }
        /** @brief 이름의 영역 번호입니다(표 순서). 없으면 -1 입니다. */
        int32 findRegionIndex( const hashed_string& region ) const;

    private:
        void loadRegionTable();

        PROPERTY( Category = "Dismemberment", DisplayName = "Region Table", AssetPath, Tooltip = "Body regions (*.fit.xml <Region> rows, bone weights)" )
        string _regionTablePath;
        PROPERTY( Category = "Dismemberment", DisplayName = "Severable Regions", Tooltip = "Regions a fatal hit may cut off" )
        vector<hashed_string> _listSeverableRegion;
        PROPERTY( Category = "Dismemberment", DisplayName = "Piece Layer", Tooltip = "Collision layer of the severed piece" )
        hashed_string _pieceLayer;
        PROPERTY( Category = "Dismemberment", DisplayName = "Piece Material", Tooltip = "Physics material of the severed piece" )
        hashed_string _pieceMaterial;
        PROPERTY( Category = "Dismemberment", DisplayName = "Blood Channel", Tooltip = "Surface channel raised on the severed region" )
        hashed_string _bloodChannel;
        PROPERTY( Category = "Dismemberment", DisplayName = "Piece Mass", Min = 0.1, Meta = "Units=kg" )
        float32 _pieceMass;

        FitTables             _regionTable;
        CharacterSurfaceState _surfaceState;
        vector<hashed_string> _listSeveredRegion;
        GameObjectHandle      _lastPiece;
        bool                  _bTableLoaded;
    };
} // namespace sw
