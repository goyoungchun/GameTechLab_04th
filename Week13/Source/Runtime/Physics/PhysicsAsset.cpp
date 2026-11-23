#include "EnginePCH.h"
#include "PhysicsAsset.h"

int32 UPhysicsAsset::FindBodyIndex(FName BoneName) const
{
    for (int32 i = 0; i < BodySetup.Num(); ++i)
    {
        if (BodySetup[i] && BodySetup[i]->BoneName == BoneName)
        {
            return i;
        }
    }
    return -1;
}

UBodySetup* UPhysicsAsset::FindBodySetup(FName BoneName) const
{
    const int32 Index = FindBodyIndex(BoneName);
    return Index >= 0 ? BodySetup[Index] : nullptr;
}

UBodySetup* UPhysicsAsset::AddBodySetup(FName BoneName)
{
    if (UBodySetup* Existing = FindBodySetup(BoneName))
    {
        return Existing;
    }

    UBodySetup* NewSetup = NewObject<UBodySetup>(this);
    NewSetup->BoneName = BoneName;
    BodySetup.Add(NewSetup);
    return NewSetup;
}

void UPhysicsAsset::RemoveBodySetup(FName BoneName)
{
    const int32 Index = FindBodyIndex(BoneName);
    if (Index >= 0)
    {
        BodySetup.RemoveAt(Index);
    }
}

void UPhysicsAsset::ClearAllBodies()
{
    BodySetup.Empty();
}
