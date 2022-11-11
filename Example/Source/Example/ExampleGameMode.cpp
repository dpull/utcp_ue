// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleGameMode.h"
#include "ExampleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AExampleGameMode::AExampleGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
