#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "dvars.hpp"
#include "command.hpp"
#include "console.hpp"
#include "scheduler.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

#include "game/dvars.hpp"

namespace dedicated
{
	namespace
	{
		void sync_gpu_stub()
		{
			std::this_thread::sleep_for(1ms);
		}

		void init_dedicated_server()
		{
			static bool initialized = false;
			if (initialized) return;
			initialized = true;

			// R_RegisterDvars
			//utils::hook::invoke<void>(0xDF62C0_b);

			// R_RegisterCmds
			//utils::hook::invoke<void>(0xDD7E50_b);

			// R_LoadGraphicsAssets
			utils::hook::invoke<void>(0xE06220_b);
		}

		utils::hook::detour dvar_set_float_from_source_hook;
		void dvar_set_float_from_source_stub(const game::dvar_t* dvar, double value, game::DvarSetSource source)
		{
			if (dvar)
			{
				printf("%s\n", dvars::dvar_get_name(dvar).data());
			}
			else
			{
				printf("fuck\n");
				return;
			}
			dvar_set_float_from_source_hook.invoke<void>(dvar, value, source);
		}

		utils::hook::detour cmd_add_client_command_list_hook;
		void cmd_add_client_command_list_stub(game::SvCommandInfo* cmds, unsigned int size)
		{
			printf("add: %p, %d\n", cmds, size);

			for (auto i = 0u; i < size; i++)
			{
				/*printf("cmd[%d]: name: %s, func: %p, svvar.name: %s, svvar.func: %p, svvar.next: %p\n",
					i, 
					cmds[i].name, 
					cmds[i].function,
					cmds[i].svvar.name,
					cmds[i].svvar.function, 
					cmds[i].svvar.next);*/

				//memset(&cmds[i].svvar, 0, sizeof(game::cmd_function_s));
			}

			cmd_add_client_command_list_hook.invoke<void>(cmds, size);
		}
	}

	DWORD __stdcall wait_for_single_object_stub(HANDLE handle, DWORD ms)
	{
		if (handle == *reinterpret_cast<HANDLE*>(0x8B1BC98_b))
		{
			//printf("not waiting for mutex\n");
			return 0;
		}

		return WaitForSingleObject(handle, ms);
	}

	void initialize()
	{
		//command::execute("exec default_xboxlive.cfg", true);
		command::execute("onlinegame 1", true);
		command::execute("xblive_privatematch 1", true);
	}

	class component final : public component_interface
	{
	public:
		void* load_import(const std::string& library, const std::string& function) override
		{
			if (game::environment::is_dedi())
			{
				if (function == "WaitForSingleObject")
				{
					return wait_for_single_object_stub;
				}
			}

			return nullptr;
		}

		void post_unpack() override
		{
			if (!game::environment::is_dedi())
			{
				return;
			}

#ifdef DEBUG
			printf("Starting dedicated server\n");
#endif

			dvar_set_float_from_source_hook.create(0xCECD00_b, dvar_set_float_from_source_stub);
			cmd_add_client_command_list_hook.create(0xB7C840_b, cmd_add_client_command_list_stub);
			//utils::hook::set<uint8_t>(0xB7C840_b, 0xC3);

			// Register dedicated dvar
			game::Dvar_RegisterBool("dedicated", true, game::DVAR_FLAG_READ, "Dedicated server");

			// Add lanonly mode
			game::Dvar_RegisterBool("sv_lanOnly", false, game::DVAR_FLAG_NONE, "Don't send heartbeat");

			// Disable frontend
			dvars::override::register_bool("frontEndSceneEnabled", false, game::DVAR_FLAG_READ);

			// Disable shader preload
			dvars::override::register_bool("r_preloadShaders", false, game::DVAR_FLAG_READ);

			// Disable renderer
			dvars::override::register_bool("r_loadForRenderer", false, game::DVAR_FLAG_READ);

			// Preload game mode fastfiles on launch
			dvars::override::register_bool("fastfilePreloadGamemode", true, game::DVAR_FLAG_NONE);

			dvars::override::register_bool("intro", false, game::DVAR_FLAG_READ);

			// Hook R_SyncGpu
			utils::hook::jump(0xE08AE0_b, sync_gpu_stub, true);

			utils::hook::jump(0x341B60_b, init_dedicated_server, true);

			utils::hook::nop(0xCDD5D3_b, 5); // don't load config file
			utils::hook::nop(0xB7CE46_b, 5); // ^
			utils::hook::set<uint8_t>(0xBB0930_b, 0xC3); // don't save config file

			utils::hook::set<uint8_t>(0x9D49C0_b, 0xC3); // disable self-registration // done
			//utils::hook::set<uint8_t>(0xD597C0_b, 0xC3); // init sound system (1) // done Voice_Init
			//utils::hook::set<uint8_t>(0x701820_b, 0xC3); // init sound system (2) // can't find ( arxan'd ) SND_Init?
			utils::hook::set<uint8_t>(0xE574E0_b, 0xC3); // render thread // done RB_RenderThread
			utils::hook::set<uint8_t>(0x3471A0_b, 0xC3); // called from Com_Frame, seems to do renderer stuff // done CL_Screen_Update
			utils::hook::set<uint8_t>(0x9AA9A0_b, 0xC3); // CL_CheckForResend, which tries to connect to the local server constantly // done CL_MainMP_CheckForResend
			//utils::hook::set<uint8_t>(0x67ADCE_b, 0x00); // r_loadForRenderer default to 0 // done via dvar override
			utils::hook::set<uint8_t>(0xD2EBB0_b, 0xC3); // recommended settings check // done
			//utils::hook::set<uint8_t>(0x5BE850_b, 0xC3); // some mixer-related function called on shutdown // not needed, only called from Voice_Init
			//utils::hook::set<uint8_t>(0x4DEA50_b, 0xC3); // dont load ui gametype stuff // don't add this for now

			utils::hook::nop(0xC5007B_b, 6); // unknown check in SV_ExecuteClientMessage // done
			utils::hook::nop(0xC4F407_b, 3); // allow first slot to be occupied // done
			utils::hook::nop(0x3429A7_b, 2); // properly shut down dedicated servers // done
			utils::hook::nop(0x34296F_b, 2); // ^ // done
			utils::hook::nop(0x3429CD_b, 5); // don't shutdown renderer // done ( maybe need to add R_ShutdownWorld to this too? )

			//utils::hook::set<uint8_t>(0xAA290_b, 0xC3); // something to do with blendShapeVertsView // not a thing in iw7
			//utils::hook::nop(0x70465D_b, 8); // sound thing // dunno if needed

			//utils::hook::set<uint8_t>(0x1D8A20_b, 0xC3); // cpu detection stuff? // can't find
			//utils::hook::set<uint8_t>(0x690F30_b, 0xC3); // gfx stuff during fastfile loading // not there
			//utils::hook::set<uint8_t>(0x690E00_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x690ED0_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x39B980_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x690E50_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0xDD26E0_b, 0xC3); // directx stuff // done
			//utils::hook::set<uint8_t>(0xE00FC0_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x6CE390_b, 0xC3); // ^ - mutex // not done
			//utils::hook::set<uint8_t>(0x681ED0_b, 0xC3); // ^

			//utils::hook::set<uint8_t>(0x0A3CD0_b, 0xC3); // rendering stuff // not done
			//utils::hook::set<uint8_t>(0x682150_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x682260_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x6829C0_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x6834A0_b, 0xC3); // ^
			//utils::hook::set<uint8_t>(0x683B40_b, 0xC3); // ^ 

			// shaders
			//utils::hook::set<uint8_t>(0x5F0610_b, 0xC3); // ^ // done
			//utils::hook::set<uint8_t>(0x5F0580_b, 0xC3); // ^ // done
			//utils::hook::set<uint8_t>(0xE51020_b, 0xC3); // ^ - mutex // done

			//utils::hook::set<uint8_t>(0x5BFD10_b, 0xC3); // idk // not done
			//utils::hook::set<uint8_t>(0xDD4430_b, 0xC3); // ^ // R_ReleaseBuffer

			//utils::hook::set<uint8_t>(0xE08360_b, 0xC3); // R_Shutdown
			//utils::hook::set<uint8_t>(0x652BA0_b, 0xC3); // shutdown stuff // not done
			//utils::hook::set<uint8_t>(0x687DF0_b, 0xC3); // ^ // not done
			//utils::hook::set<uint8_t>(0x686DE0_b, 0xC3); // ^ // not done

			// utils::hook::set<uint8_t>(0x1404B67E0, 0xC3); // sound crashes (H1 - questionable, function looks way different)

			utils::hook::set<uint8_t>(0xC5A200_b, 0xC3); // disable host migration // done SV_MigrationStart

			//utils::hook::set<uint8_t>(0xBB66B0_b, 0xC3); // render synchronization lock // done
			//utils::hook::set<uint8_t>(0xBB64A0_b, 0xC3); // render synchronization unlock // done

			//utils::hook::set<uint8_t>(0x615359_b, 0xEB); // LUI: Unable to start the LUI system due to errors in main.lua // done
			//utils::hook::set<uint8_t>(0x27AAC5_b, 0xEB); // LUI: Unable to start the LUI system due to errors in depot.lua // no
			//utils::hook::set<uint8_t>(0x27AADC_b, 0xEB); // ^

			//utils::hook::nop(0xCFDA7E_b, 5); // Disable sound pak file loading // done
			//utils::hook::nop(0xCFDA97_b, 2); // ^ // done
			//utils::hook::set<uint8_t>(0x3A0BA0_b, 0xC3); // Disable image pak file loading // not done

			// Reduce min required memory
			//utils::hook::set<uint64_t>(0x5B7F37_b, 0x80000000); // not done

			//utils::hook::set<uint8_t>(0x399E10_b, 0xC3); // some loop // not done
			//utils::hook::set<uint8_t>(0x1D48B0_b, 0xC3); // related to shader caching / techsets / fastfilesc // not done
			//utils::hook::set<uint8_t>(0x3A1940_b, 0xC3); // DB_ReadPackedLoadedSounds // not done

			// iw7 patches
			utils::hook::set<uint8_t>(0xE06060_b, 0xC3); // directx
			utils::hook::set<uint8_t>(0xE05B80_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0xDD2760_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0xE05E20_b, 0xC3); // ^ buffer
			utils::hook::set<uint8_t>(0xE11270_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0xDD3C50_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0x0C1210_b, 0xC3); // ^ idk
			utils::hook::set<uint8_t>(0x0C12B0_b, 0xC3); // ^ idk
			utils::hook::set<uint8_t>(0xE423A0_b, 0xC3); // directx
			utils::hook::set<uint8_t>(0xE04680_b, 0xC3); // ^

			utils::hook::set<uint8_t>(0xE00ED0_b, 0xC3); // Image_Create1DTexture_PC
			utils::hook::set<uint8_t>(0xE00FC0_b, 0xC3); // Image_Create2DTexture_PC
			utils::hook::set<uint8_t>(0xE011A0_b, 0xC3); // Image_Create3DTexture_PC
			utils::hook::set<uint8_t>(0xE015C0_b, 0xC3); // Image_CreateCubeTexture_PC
			utils::hook::set<uint8_t>(0xE01300_b, 0xC3); // Image_CreateArrayTexture_PC

			utils::hook::set<uint8_t>(0x5F1EA0_b, 0xC3); // renderer
			utils::hook::set<uint8_t>(0x0C1370_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0xDD26E0_b, 0xC3); // directx
			utils::hook::set<uint8_t>(0x5F0610_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0x5F0580_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0x5F0820_b, 0xC3); // ^
			utils::hook::set<uint8_t>(0x5F0790_b, 0xC3); // ^

			utils::hook::set<uint8_t>(0x3B9E72_b, 0xEB); // skip R_GetFrameIndex check in DB_LoadLevelXAssets

			// release buffer
			utils::hook::set<uint8_t>(0xDD4430_b, 0xEB);

			// R_LoadWorld
			utils::hook::set<uint8_t>(0xDD14C0_b, 0xC3);

			scheduler::loop([]() // maybe not needed
			{
				// snd_enabled
				*reinterpret_cast<DWORD*>(0x7201A88_b) = 0;


			}, scheduler::pipeline::async);

			command::add("startserver", []()
			{
				initialize();

				console::info("==================================\n");
				console::info("Server started!\n");
				console::info("==================================\n");
			});
		}
	};
}

REGISTER_COMPONENT(dedicated::component)