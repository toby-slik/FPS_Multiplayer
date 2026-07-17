#pragma once

#include "ShooterTypes.generated.h"

UENUM()
enum class ETurnInPlace : uint8
{
	Left UMETA(DisplayName = "TurningLeft"),
	Right UMETA(DisplayName = "TurningRight"),
	NotTurning UMETA(DisplayName = "NotTurning"),
};

