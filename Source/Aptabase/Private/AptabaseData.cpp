#include "AptabaseData.h"

#include <JsonObjectConverter.h>

TSharedPtr<FJsonObject> FAptabaseEventPayload::ToJsonObject() const
{
	const TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	for (const FExtendedAnalyticsEventAttribute& Attribute : EventAttributes)
	{
		const auto& AttributeValue = Attribute.Value;

		if (AttributeValue.IsType<double>())
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueNumber>(AttributeValue.Get<double>()));
		}
		else if (AttributeValue.IsType<float>())
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueNumber>(AttributeValue.Get<float>()));
		}
		else
		{
			Props->SetField(Attribute.Key, MakeShared<FJsonValueString>(AttributeValue.Get<FString>()));
		}
	}

	const TSharedPtr<FJsonObject> Payload = FJsonObjectConverter::UStructToJsonObject(*this);
	Payload->SetObjectField("props", Props);

	return Payload;
}