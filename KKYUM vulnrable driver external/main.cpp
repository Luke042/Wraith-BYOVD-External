#include <iostream>
#include <print>
#include <chrono>
#include <thread>
#include <numbers>
#include <locale>
#include <Windows.h>
#include "overlay.hpp"
#include "helper.hpp"
#include "vendor/makcu-cpp/include/makcu.h"

#pragma warning(disable : 4996)

struct head
{
	float x, y, z;
} entity_h, local_player_h;

template <typename... Args>
void PrintLine(std::format_string<Args...> fmt, Args&&... args)
{
	auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
	auto seconds = std::chrono::floor<std::chrono::seconds>(now);

	std::print("\033[95m[{:%d/%m/%Y %H:%M:%S}]\033[35m > \033[0m", seconds);
	std::print(fmt, std::forward<Args>(args)...);
	std::print("\n");
}

void Initialise()
{
	SetConsoleTitleA("Wraith AssaultCube External");
	HWND console = GetConsoleWindow();
	LONG style = GetWindowLong(console, GWL_STYLE);
	style &= ~(WS_SIZEBOX | WS_MAXIMIZEBOX);
	SetWindowLong(console, GWL_STYLE, style);
	SetWindowPos(console, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

	enableAnsi();
	logo::Render();
}

void TestBenchmarks(void* device)
{
	PrintLine("Performing test reads...");

	// example junk I made for testing driver read requests before I reversed the function in ida. I just kept it here because Im lazy
	int successful_reads{};
	int failed_reads{};

	int input_buffer[5] = { 0, 1, 2, 3, 4 };
	int output_buffer{};
	unsigned long bytes_returned;

	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < 1000; i++)
	{
		if (DeviceIoControl(device, ioctl_codes::mem_read, &input_buffer, sizeof(input_buffer), &output_buffer, sizeof(output_buffer), &bytes_returned, 0))
			successful_reads++;
		else
			failed_reads++;
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = end - start;

	PrintLine("Successful reads: {} ({:.2f}%)", successful_reads, (successful_reads / 1000.0) * 100.0);
	double seconds = std::chrono::duration<double>(duration).count();
	PrintLine("Aproximate reads per second: {:.2Lf}", successful_reads / seconds);
	PrintLine("Failed reads: {}", failed_reads);
	PrintLine("Took: {:.5f} seconds", std::chrono::duration<double>(duration).count());
}

static int FindMedius(MediusStatus s, const char* what)
{
	if (s == MEDIUS_STATUS_OK) return 1;
	char msg[256];
	medius_last_error_message(msg, sizeof msg);
	fprintf(stderr, "%s failed (%d): %s\n", what, (int)s, msg);
	return 0;
}

// connect to makcu & medius & perform a test mouse movment
void InitMakcu(makcu::Device& device)
{
	if (medius_abi_version() != MEDIUS_ABI_VERSION)
	{
		PrintLine("Medius ABI mismatch, rebuild against the matching header. Skipping medius.");
	}
	else if (FindMedius(medius_device_find(&mem::medius_device), "find"))
	{
		mem::medius = true;
	}
	else
	{
		mem::medius_device = nullptr;
		PrintLine("Failed to find medius device... Continuing on to makcu.");
	}

	if (!device.connect() && !mem::medius)
	{
		PrintLine("Failed to connect to makcu. Check Makcu connection.");
	}

	PrintLine("Successfully connected to Makcu.");

	if (device.isConnected()) device.mouseMove(100, 100);

	if (mem::medius)
		medius_device_move_rel_now(mem::medius_device, 200, 200);
}

static float NormAngle(float a)
{
	while (a > 180.f)  a -= 360.f;
	while (a < -180.f) a += 360.f;
	return a;
}

int main()
{
	Initialise();
	std::locale::global(std::locale("en-US"));

    mem::g_device = CreateFileW(L"\\\\.\\KKYUM", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (mem::g_device == INVALID_HANDLE_VALUE)
	{
		PrintLine("Failed to find vulnrable driver.");
		PrintLine("Exiting in 3 seconds.");

		std::this_thread::sleep_for(std::chrono::milliseconds(3000));
		return 1;
	}

	PrintLine("Found Vulnrable driver (KKYUM.sys).");

	mem::g_pid = mem::GetPid(L"ac_client.exe");
	if (mem::g_pid == 0)
	{
		PrintLine("Failed to find process.");
		PrintLine("Exiting in 3 seconds.");
		std::this_thread::sleep_for(std::chrono::milliseconds(3000));
		return 1;
	}

	PrintLine("Found process Id: 0x{}", mem::g_pid);

	TestBenchmarks(mem::g_device);

	PrintLine("Base Address: 0x{:X}", mem::g_base_address);

	std::thread(overlay::Initalize).detach();

	PrintLine("Press [F1] for memory aim | Press [F2] for makcu aim.");

	auto keyDown = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	makcu::Device device{};

	while (!settings::memory_aim && !settings::makcu_aim)
	{
		if (keyDown(VK_F1))
		{
			settings::memory_aim = true;
			PrintLine("Memory aim selected.");
		}
		else if (keyDown(VK_F2))
		{
			settings::makcu_aim = true;
			PrintLine("Makcu aim selected.");

			InitMakcu(device);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	std::vector<Vec2> frame_positions;
	std::vector<Vec2> frame_feet_positions;

	while (true)
	{
		std::uint32_t local_player = mem::read<std::uint32_t>(mem::g_base_address + offsets::local_player);
		int player_count = mem::read<int>(mem::g_base_address + offsets::player_count);
		std::uint32_t entity_list = mem::read<std::uint32_t>(mem::g_base_address + offsets::entity_list);

		local_player_h.x = mem::read<float>(local_player + offsets::head_x);
		local_player_h.y = mem::read<float>(local_player + offsets::head_y);
		local_player_h.z = mem::read<float>(local_player + offsets::head_z);

		float cur_yaw = mem::read<float>(local_player + offsets::yaw);
		float cur_pitch = mem::read<float>(local_player + offsets::pitch);

		bool found = false;
		float best_dist = FLT_MAX, best_yaw = 0.f, best_pitch = 0.f;
		const float fov = 30.f;

		float view_matrix[16];
		for (int i = 0; i < 16; i++)
			view_matrix[i] = mem::read<float>(mem::g_base_address + 0x17DFD0 + i * sizeof(float));

		for (int i = 0; i < player_count; i++)
		{
			std::uint32_t entity = mem::read<std::uint32_t>(entity_list + i * sizeof(std::uint32_t));
			if (!entity || entity == local_player) continue;

			int health = mem::read<int>(entity + offsets::health);
			if (health <= 0 || health > 100) continue;

			float enemy_x = mem::read<float>(entity + offsets::head_x);
			float enemy_y = mem::read<float>(entity + offsets::head_y);
			float enemy_z = mem::read<float>(entity + offsets::head_z);

			float feet_x = mem::read<float>(entity + offsets::feet_x);
			float feet_y = mem::read<float>(entity + offsets::feet_y);
			float feet_z = mem::read<float>(entity + offsets::feet_z);

			Vec2 screen_head{};
			Vec2 screen_feet{};

			bool head_on_screen = WorldToScreen({ enemy_x, enemy_y, enemy_z }, screen_head, view_matrix);
			bool feet_on_screen = WorldToScreen({ feet_x, feet_y, feet_z }, screen_feet, view_matrix); // -1.8 units down for feet

			if (head_on_screen && feet_on_screen)
			{
				frame_positions.push_back(screen_head);
				frame_feet_positions.push_back(screen_feet);
			}

			float delta_x = enemy_x - local_player_h.x;
			float delta_y = enemy_y - local_player_h.y;
			float delta_z = enemy_z - local_player_h.z;

			float yaw = atan2f(delta_y, delta_x) * (180 / std::numbers::pi) + 90;
			float pitch = atan2f(delta_z, sqrtf(delta_x * delta_x + delta_y * delta_y)) * (180 / std::numbers::pi);

			float delta_yaw = NormAngle(yaw - cur_yaw);
			float delta_pitch = pitch - cur_pitch;
			float dist = sqrtf(delta_yaw * delta_yaw + delta_pitch * delta_pitch);

			if (dist < fov && dist < best_dist)
			{
				best_dist = dist;
				best_yaw = yaw;
				best_pitch = pitch;
				
				found = true;
			}
		}

		// replace the shared vector in one go — overlay always sees one complete frame
		{
			std::lock_guard<std::mutex> lock(entity_positions_mutex);
			entity_positions = std::move(frame_positions);
			entity_feet_positions = std::move(frame_feet_positions);
		}

		if (found && GetAsyncKeyState(VK_RBUTTON))
		{
			if (settings::memory_aim)
			{
				mem::write<float>(local_player + offsets::yaw, best_yaw);
				mem::write<float>(local_player + offsets::pitch, best_pitch);
			}
			else if (settings::makcu_aim || settings::two_pc_makcu_aim)
			{
				static auto last = std::chrono::steady_clock::now();
				static float rem_x = 0.f, rem_y = 0.f;

				auto now = std::chrono::steady_clock::now();
				float delta_time = std::chrono::duration<float>(now - last).count();

				if (delta_time >= interval_s)
				{
					last = now;

					float yaw_error = NormAngle(best_yaw - cur_yaw);
					float pitch_error = best_pitch - cur_pitch;
					float angular_error = sqrtf(yaw_error * yaw_error + pitch_error * pitch_error);

					if (angular_error > deadzone)
					{
						float yaw_step = yaw_error * gain;
						float pitch_step = pitch_error * gain;

						float max_step = max_deg_per_s * delta_time;
						float magnitude = sqrtf(yaw_step * yaw_step + pitch_step * pitch_step);

						if (magnitude > max_step)
						{
							float scale_factor = max_step / magnitude;

							yaw_step *= scale_factor;
							pitch_step *= scale_factor;
						}

						rem_x += yaw_step / dpp;
						rem_y += -pitch_step / dpp;

						int mouse_x = static_cast<int>(rem_x * 0.8);
						int mouse_y = static_cast<int>(rem_y * 0.8);
						rem_x -= mouse_x;
						rem_y -= mouse_y;

						if (mouse_x || mouse_y)
						{
							if (device.isConnected()) device.mouseMove(mouse_x, mouse_y);
							if (mem::medius) medius_device_move_rel_now(mem::medius_device, mouse_x, mouse_y);
						}
					}
				}
			}
		}
	}

	return 0;
}