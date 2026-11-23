#include "EnginePCH.h"
#include "PhysicsAsset.h"

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    Super::Serialize(Record);

    int32 Version = Ar.IsSaving() ? 1 : 0;
    Record << SA_VALUE("Version", Version);

    if (Version != 1)
    {
        Ar.SetError();
        return;
    }

    int32 Num = Ar.IsSaving() ? BodySetup.Num() : 0;
    FStructuredArchive::FArray Bodies = Record.EnterArray("Bodies", Num);

    if (Ar.IsLoading())
    {
        ClearAllBodies();
    }

    for (int32 i = 0; i < Num; ++i)
    {
        FStructuredArchive::FRecord BodyRecord = Bodies.EnterElement().EnterRecord();

        if (Ar.IsLoading())
        {
            UBodySetup* Setup = NewObject<UBodySetup>(this);
            Setup->Serialize(BodyRecord);
            BodySetup.Add(Setup);
        }
        else
        {
            BodySetup[i]->Serialize(BodyRecord);
        }
    }

}

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
