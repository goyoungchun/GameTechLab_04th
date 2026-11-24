#pragma once

#include "UObject/Object.h"
#include "ConstraintInstance.h"
#include "PhysicsConstraintTemplate.generated.h"

UCLASS()
class UPhysicsConstraintTemplate : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FConstraintInstance DefaultInstance;
};