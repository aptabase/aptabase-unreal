#include "AptabaseSettings.h"

FString UAptabaseSettings::GetApiUrl() const
{
	switch (Host)
	{
	case EAptabaseHost::EU:
		return TEXT("https://eu.aptabase.com");
	case EAptabaseHost::US:
		return TEXT("https://us.aptabase.com");
	case EAptabaseHost::DEV:
		return TEXT("http://localhost:3000");
	case EAptabaseHost::SH:
		return CustomHost;
	default:
		return TEXT("");
	}
}

FName UAptabaseSettings::GetContainerName() const
{
	return TEXT("Project");
}

FName UAptabaseSettings::GetCategoryName() const
{
	return TEXT("Analytics");
}

FName UAptabaseSettings::GetSectionName() const
{
	return TEXT("Aptabase");
}

#if WITH_EDITOR
FText UAptabaseSettings::GetSectionText() const
{
	const FName DisplaySectionName = GetSectionName();
	return FText::FromName(DisplaySectionName);
}
void UAptabaseSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UAptabaseSettings, AppKey))
	{
		TArray<FString> Parts;
		AppKey.ParseIntoArray(Parts, TEXT("-"));

		int64 EnumValue = -1; // If the parts were not parsed as we expected, we default to (INVALID).
		if (Parts.Num() == 3)
		{
			EnumValue = StaticEnum<EAptabaseHost>()->GetValueByNameString(Parts[1]);
		}

		Host = static_cast<EAptabaseHost>(EnumValue);

		SaveConfig();
	}
}
#endif
