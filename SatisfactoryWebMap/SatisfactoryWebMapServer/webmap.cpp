#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <httplib.h>
#include <psapi.h>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>

#include "FactoryGameSDK.h"
#include "Config.h"

extern httplib::Server s;
extern Config config;

const uint8_t *BaseAddr = nullptr;
const TNameEntryArray *Names_0 = nullptr;
const FUObjectArray *GUObjectArray = nullptr;
const FGMapManager *MapManager = nullptr;

bool FindMapManager()
{
	if (BaseAddr == nullptr) {
		return false;
	}

	const auto &ObjObjects = GUObjectArray->ObjObjects;
	for (int Index = 0; Index < ObjObjects.NumElements; ++Index) {
		const FUObjectItem *Object = &ObjObjects[Index];
		if (Object->Object == nullptr) {
			continue;
		}

		if (Object->Object->NamePrivate == config.MapManagerName) {
			MapManager = (FGMapManager *)(Object->Object);
			return true;
		}
	}

	return false;
}

bool setup()
{
	using namespace httplib;
	using json = nlohmann::json;

	MODULEINFO moduleInfo{ 0 };
	if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &moduleInfo, sizeof(moduleInfo))) {
		return false;
	}

	BaseAddr = (uint8_t *)moduleInfo.lpBaseOfDll;
	if (BaseAddr == nullptr) {
		return false;
	}

	Names_0 = *(TNameEntryArray **)(BaseAddr + config.TNameEntryArrayOffset);
	GUObjectArray = (FUObjectArray *)(BaseAddr + config.GUObjectArrayOffset);

	FindMapManager();
	if (!MapManager || !MapManager->mActorRepresentationManager) {
		OutputDebugStringA("Unable to find MapManager during setup.");
	}

	s.Get("/api/dump", [&](const Request &req, Response &res) {
		std::ofstream ofs("dump.txt", std::ofstream::out);

		ofs << "BaseAddr = " << (void *)BaseAddr << std::endl
			<< "TNameEntryArray *Names = " << (void *)Names_0 << std::endl
			<< "NumElements = " << Names_0->NumElements << " "
			<< "NumChunks = " << Names_0->NumChunks << std::endl;

		ofs << "FUObjectArray *GUObjectArray = " << (void *)GUObjectArray << std::endl
			<< "NumElements = " << GUObjectArray->ObjObjects.NumElements << " "
			<< "NumChunks = " << GUObjectArray->ObjObjects.NumChunks << std::endl;

		ofs << std::endl;

		int count = 0;

		const auto &ObjObjects = GUObjectArray->ObjObjects;
		for (int Index = 0; Index < ObjObjects.NumElements; ++Index) {
			const FUObjectItem *Object = &ObjObjects[Index];
			if (Object->Object == nullptr) {
				continue;
			}

			++count;
			ofs << "[" << Index << "]" 
				<< " SerialNumber: " << Object->SerialNumber 
				<< " Flags: " << Object->Flags << " Object: " << Object->Object 
				<< std::endl << "\t"
				<< "NamePrivate: Number=" << Object->Object->NamePrivate.Number
				<< " ComparisonIndex=" << Object->Object->NamePrivate.ComparisonIndex
				<< " String=\"" << Object->Object->NamePrivate << "\""
				<< std::endl;
		}

		ofs << "Object dump finished. Total " << count << " Objects" << std::endl;
		ofs.close();

		std::string path;
		path.resize(MAX_PATH);
		GetFullPathNameA("dump.txt", path.size(), &path[0], nullptr);

		res.set_content(json({ {"status", "ok"}, { "path", path } }), "application/json");
	});

	s.Get("/api/actors", [&](const Request &req, Response &res) {
		if (!MapManager || !MapManager->mActorRepresentationManager) {
			if (!FindMapManager() || !MapManager || !MapManager->mActorRepresentationManager) {
				res.set_content(R"({"status": "err", "msg": "invalid obj"})", "application/json");
				return;
			}
		}

		auto respMgr = MapManager->mActorRepresentationManager;
		auto size = respMgr->mReplicatedRepresentations.ArrayNum;

		std::vector<json> features;
		features.reserve(size);

		for (int i = 0; i < size; ++i) {
			auto actorResp = respMgr->mReplicatedRepresentations.Data[i];
			if (actorResp == nullptr) {
				continue;
			}

			json j;

			j["type"] = "Feature";
			j["properties"] =
			{
				{  "type", actorResp->mRepresentationType          },
				{ "index", actorResp->InternalIndex                },
				{ "color", actorResp->mRepresentationColor.ToVec() },
			};

			j["geometry"]["type"] = "Point";

			AActor *realActor = actorResp->mRealActor;
			if (realActor == nullptr) {
				j["geometry"]["coordinates"] = actorResp->mActorLocation.ToVec();
				j["properties"]["ang"] = actorResp->mActorRotation.ToVec();
			} else {
				j["geometry"]["coordinates"] = realActor->RootComponent->ComponentToWorld.Translation.ToVector3().ToVec();

				j["properties"]["ang"] = realActor->RootComponent->ComponentToWorld.Rotation.ToVector3().ToVec();
				j["properties"]["vel"] = realActor->RootComponent->ComponentVelocity.ToVec();
			}

			features.push_back(j);
		}

		// return GeoJSON object
		res.set_content(json({ 
			{ "status", "ok" }, 
			{ "type", "FeatureCollection" },
			{ "features", features },
		}).dump(), "application/json");
	});

	return true;
}
