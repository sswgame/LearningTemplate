/**
 * @file Fracture2DComponent.h
 * @brief 2D 파괴 컴포넌트입니다. 다각형(또는 상자) 형태를 시작할 때 시드로 쪼개(`PolygonFractureUtil`) Box2D 바디와 스킨드 조각 메시로 부숩니다.
 *        구조, 피해, 런타임은 3D 와 같은 `FractureComponentBase` 를 쓰고, 다른 것은 데이터의 출처(쪼개기)와 물리 씬(2D)뿐입니다.
 * @details 오브젝트는 루트 `RigidBody2DComponent`(온전할 때의 충돌)와 이 컴포넌트로 구성한다.
 *          오브젝트에 그릴 메시가 없으면, 쉬는 조각을 베이크한 평평한 메시를 `MeshComponent` 로 추가해 온전한 모습으로 쓴다.
 *          같은 시드와 형태면 모든 기계에서 같은 조각이 나오므로 시드와 사건만 보낸다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Destruction/Fracture/FractureComponentBase.h"
#include "Engine/Destruction/Fracture/FractureSettings.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    REFLECT( Category = "Physics", DisplayName = "Fracture 2D", Tooltip = "Destructible 2D shape: polygon Voronoi-fractured at start, Box2D pieces", Meta = "Icon=Fracture" )
    class SW_API Fracture2DComponent : public FractureComponentBase
    {
    public:
        REFLECT_BODY();

        Fracture2DComponent();
        ~Fracture2DComponent() override = default;

        void onBeginPlay() override;

        /** @brief 형태를 다각형(오브젝트 로컬 XY, 반시계)으로 정합니다. 비어 있으면 `_size` 상자를 씁니다. 시작 전에 호출합니다. */
        void setBorder( const vector<float2>& listBorder ) { _listBorder = listBorder; }
        void setSize( const float2& size ) { _size = size; }
        void setPieceCount( uint32 pieceCount ) { _pieceCount = pieceCount; }
        void setFractureSeed( uint32 seed ) { _fractureSeed = seed; }
        /** @brief 쪼개기에 쓰는 형태입니다. 상자면 바닥 가운데가 원점인 사각형입니다. */
        void makeBorder( vector<float2>& outListBorder ) const;

    protected:
        bool                            uses2DPhysics() const override { return true; }
        shared_ptr<const FractureAsset> acquireFracture() override;

    private:
        /** @brief 설정대로 한 번 쪼갭니다. */
        void ensureFracture();

        PROPERTY( Category = "Fracture 2D", DisplayName = "Outline", Tooltip = "Polygon in object XY (counter-clockwise); empty uses Size" )
        vector<float2> _listBorder;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Cluster Levels", Tooltip = "Clusters per level, top to bottom (big chunks break first)" )
        vector<uint32> _listLevelCount;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Size", Tooltip = "Box size when there is no outline (bottom centre at the origin)", Units = m )
        float2 _size;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Pieces", Min = 1.0, Tooltip = "Voronoi sites (uniform / clustered)" )
        uint32 _pieceCount;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Fracture Seed", Tooltip = "Same seed and shape give the same pieces on every machine" )
        uint32 _fractureSeed;
        PROPERTY( Category = "Fracture 2D", DisplayName = "Pattern", Tooltip = "Where the Voronoi sites go" )
        FracturePattern                 _pattern;
        shared_ptr<const FractureAsset> _asset2D;
    };
} // namespace sw
