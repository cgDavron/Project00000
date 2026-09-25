// BP Text Exporter
// Content Browser > Blueprint'e sag tik > "Copy BP for AI"
// Secili Blueprint'lerin component'lerini, degiskenlerini ve tum grafiklerini
// panoya "#BPAI" formatinda koyar. bp_optimizer.pyw bunu Ozet formatina cevirir.

#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "BPTextExporter"

namespace BPTextExporter
{
	// Cok uzun degerleri kisalt (token tasarrufu)
	static FString Shorten(const FString& In, int32 MaxLen = 200)
	{
		return In.Len() > MaxLen ? In.Left(MaxLen) + TEXT("...") : In;
	}

	// Bir nesnenin, sinifinin varsayilanindan FARKLI olan duzenlenebilir ozelliklerini yaz
	static void AppendOverrides(FString& Out, UObject* Obj, const FString& Indent)
	{
		if (!Obj) return;
		UObject* Def = Obj->GetClass()->GetDefaultObject();
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
			{
				continue;
			}
			const void* A = Prop->ContainerPtrToValuePtr<void>(Obj);
			const void* B = Prop->ContainerPtrToValuePtr<void>(Def);
			if (Prop->Identical(A, B))
			{
				continue;
			}
			FString Value;
			Prop->ExportTextItem_Direct(Value, A, nullptr, Obj, PPF_None);
			Out += FString::Printf(TEXT("%s.%s = %s\n"), *Indent, *Prop->GetName(), *Shorten(Value));
		}
	}

	static void AppendScsNode(FString& Out, USCS_Node* Node, int32 Depth)
	{
		if (!Node) return;
		const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
		FString Parent;
		if (Depth == 0 && Node->ParentComponentOrVariableName != NAME_None)
		{
			Parent = FString::Printf(TEXT(" (parent: %s)"), *Node->ParentComponentOrVariableName.ToString());
		}
		Out += FString::Printf(TEXT("%s%s : %s%s\n"), *Indent, *Node->GetVariableName().ToString(),
			Node->ComponentClass ? *Node->ComponentClass->GetName() : TEXT("?"), *Parent);

		AppendOverrides(Out, Node->ComponentTemplate, Indent + TEXT("    "));

		for (USCS_Node* Child : Node->GetChildNodes())
		{
			AppendScsNode(Out, Child, Depth + 1);
		}
	}

	static void AppendGraph(FString& Out, UEdGraph* Graph, const TCHAR* Kind)
	{
		if (!Graph) return;
		TSet<UObject*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node) Nodes.Add(Node);
		}
		if (Nodes.Num() == 0) return;

		// Fonksiyonun local degiskenleri (GRAPH satirindan once yazilir)
		TArray<UK2Node_FunctionEntry*> Entries;
		Graph->GetNodesOfClass(Entries);
		for (UK2Node_FunctionEntry* Entry : Entries)
		{
			for (const FBPVariableDescription& Local : Entry->LocalVariables)
			{
				Out += FString::Printf(TEXT("#BPAI LOCAL %s: %s = %s\n"), *Local.VarName.ToString(),
					*UEdGraphSchema_K2::TypeToText(Local.VarType).ToString(), *Shorten(Local.DefaultValue, 300));
			}
		}

		FString Text;
		FEdGraphUtilities::ExportNodesToText(Nodes, Text);
		Out += FString::Printf(TEXT("#BPAI GRAPH %s %s\n"), Kind, *Graph->GetName());
		Out += Text;
		Out += TEXT("\n");
	}

	static FString ExportBlueprint(UBlueprint* BP)
	{
		FString Out;
		Out += FString::Printf(TEXT("#BPAI BEGIN %s Parent=%s\n"), *BP->GetPathName(),
			BP->ParentClass ? *BP->ParentClass->GetName() : TEXT("None"));

		// --- Component hiyerarsisi ---
		if (BP->SimpleConstructionScript)
		{
			Out += TEXT("#BPAI COMPONENTS\n");
			for (USCS_Node* Root : BP->SimpleConstructionScript->GetRootNodes())
			{
				AppendScsNode(Out, Root, 0);
			}
		}

		// --- Degiskenler (varsayilan degerler CDO'dan okunur, yani derlenmis hal) ---
		Out += TEXT("#BPAI VARS\n");
		UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject(false) : nullptr;
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			const FString Type = UEdGraphSchema_K2::TypeToText(Var.VarType).ToString();
			FString Value = Var.DefaultValue;
			if (CDO)
			{
				if (FProperty* Prop = FindFProperty<FProperty>(BP->GeneratedClass, Var.VarName))
				{
					Value.Reset();
					Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(CDO), nullptr, CDO, PPF_None);
				}
			}
			FString Flags;
			if ((Var.PropertyFlags & CPF_DisableEditOnInstance) == 0) Flags += TEXT(" [Editable]");
			if (Var.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn)) Flags += TEXT(" [ExposeOnSpawn]");
			const FString Category = Var.Category.IsEmpty() ? FString() : FString::Printf(TEXT(" {%s}"), *Var.Category.ToString());
			Out += FString::Printf(TEXT("%s: %s = %s%s%s\n"), *Var.VarName.ToString(), *Type,
				*Shorten(Value, 300), *Flags, *Category);
		}

		// --- Grafikler ---
		for (UEdGraph* Graph : BP->UbergraphPages)   AppendGraph(Out, Graph, TEXT("Event"));
		for (UEdGraph* Graph : BP->FunctionGraphs)   AppendGraph(Out, Graph, TEXT("Function"));
		for (UEdGraph* Graph : BP->MacroGraphs)      AppendGraph(Out, Graph, TEXT("Macro"));
		for (const FBPInterfaceDescription& Iface : BP->ImplementedInterfaces)
		{
			for (UEdGraph* Graph : Iface.Graphs)     AppendGraph(Out, Graph, TEXT("Interface"));
		}

		Out += TEXT("#BPAI END\n");
		return Out;
	}

	static void ExportAssets(const TArray<FAssetData>& Assets)
	{
		FString All;
		int32 Count = 0;
		for (const FAssetData& Asset : Assets)
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset()))
			{
				All += ExportBlueprint(BP);
				++Count;
			}
		}
		if (Count == 0) return;

		FPlatformApplicationMisc::ClipboardCopy(*All);

		FNotificationInfo Info(FText::Format(LOCTEXT("Done", "{0} Blueprint panoya kopyalandi ({1} karakter)"),
			FText::AsNumber(Count), FText::AsNumber(All.Len())));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

class FBPTextExporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBPTextExporterModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		// Tum asset sag tik menulerinin atasi; asagida yalnizca Blueprint'ler icin gosteriyoruz
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu");
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

		Section.AddDynamicEntry("CopyBPForAI", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
		{
			const UContentBrowserAssetContextMenuContext* Context =
				InSection.FindContext<UContentBrowserAssetContextMenuContext>();
			if (!Context) return;

			TArray<FAssetData> Blueprints;
			for (const FAssetData& Asset : Context->SelectedAssets)
			{
				UClass* AssetClass = Asset.GetClass();
				if (AssetClass && AssetClass->IsChildOf(UBlueprint::StaticClass()))
				{
					Blueprints.Add(Asset);
				}
			}
			if (Blueprints.Num() == 0) return;

			InSection.AddMenuEntry(
				"CopyBPForAI",
				LOCTEXT("CopyLabel", "Copy BP for AI"),
				LOCTEXT("CopyTip", "Secili Blueprint'lerin grafiklerini, degiskenlerini ve component'lerini metin olarak panoya kopyalar"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([Blueprints]()
				{
					BPTextExporter::ExportAssets(Blueprints);
				})));
		}));
	}
};

IMPLEMENT_MODULE(FBPTextExporterModule, BPTextExporter)

#undef LOCTEXT_NAMESPACE
