#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif

#include <cmath>

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <stdint.h>
#pragma comment(lib, "Dinput8.lib")
#pragma comment(lib, "Dxguid.lib")

#include "devopt.h"
#include "entity_animation_menu.h"
#include "game.h"
#include "input_mgr.h"
#include "resource_manager.h"
#include "forwards.h"
#include "variable.h"
#include "func_wrapper.h"
#include "fixedstring32.h"
#include "levelmenu.h"
#include "memory_menu.h"
#include "mission_manager.h"
#include "mission_table_container.h"
#include "mstring.h"
#include "region.h"
#include "debug_menu.h"
#include "os_developer_options.h"
#include "script_executable.h"
#include "script_library_class.h"
#include "script_object.h"
#include "spider_monkey.h"
#include "geometry_manager.h"
#include "entity.h"
#include "terrain.h"
#include "debug_menu_extra.h"

DWORD* ai_current_player = nullptr;
DWORD* fancy_player_ptr = nullptr;

/*
#undef IsEqualGUID
BOOL WINAPI IsEqualGUID(
	REFGUID rguid1,
	REFGUID rguid2)
{
	return !memcmp(rguid1, rguid2, sizeof(GUID));
}
*/

uint8_t color_ramp_function(float ratio, int period_duration, int cur_time) {

	if (cur_time <= 0 || 4 * period_duration <= cur_time)
		return 0;

	if (cur_time < period_duration) {

		float calc = ratio * cur_time;

		return std::min(calc, 1.0f) * 255;
	}


	if (period_duration <= cur_time && cur_time <= 3 * period_duration) {
		return 255;
	}


	if (cur_time <= 4 * period_duration) {

		int adjusted_time = cur_time - 3 * period_duration;
		float calc = 1.f - ratio * adjusted_time;

		return std::min(calc, 1.0f) * 255;
	}

    return 0;

}

static void *HookVTableFunction(void *pVTable, void *fnHookFunc, int nOffset) {
    intptr_t ptrVtable = *((intptr_t *) pVTable); // Pointer to our chosen vtable
    intptr_t ptrFunction = ptrVtable +
        sizeof(intptr_t) *
            nOffset; // The offset to the function (remember it's a zero indexed array with a size of four bytes)
    intptr_t ptrOriginal = *((intptr_t *) ptrFunction); // Save original address

    // Edit the memory protection so we can modify it
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery((LPCVOID) ptrFunction, &mbi, sizeof(mbi));
    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);

    // Overwrite the old function with our new one
    *((intptr_t *) ptrFunction) = (intptr_t) fnHookFunc;

    // Restore the protection
    VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &mbi.Protect);

    // Return the original function address incase we want to call it
    return (void *) ptrOriginal;
}

typedef struct _list{
	struct _list* next;
	struct _list* prev;
	void* data;
}list;

static constexpr DWORD MAX_ELEMENTS_PAGE = 18;

debug_menu* game_menu = nullptr;
debug_menu* missions_menu = nullptr;
debug_menu* options_menu = nullptr;
debug_menu* script_menu = nullptr;
debug_menu* progression_menu = nullptr;
debug_menu* level_select_menu = nullptr;

debug_menu** all_menus[] = {
	&debug_menu::root_menu,
    &game_menu,
	&missions_menu,
	&options_menu,
	&script_menu,
	&progression_menu,
    &level_select_menu
};

void unlock_region(region* cur_region) {
	cur_region->flags &= 0xFE;
}

void remove_debug_menu_entry(debug_menu_entry* entry) {
	
	DWORD to_be = (DWORD)entry;
	for (auto i = 0u; i < (sizeof(all_menus) / sizeof(void*)); ++i) {

		debug_menu *cur = *all_menus[i];

		DWORD start = (DWORD) cur->entries;
		DWORD end = start + cur->used_slots * sizeof(debug_menu_entry);

		if (start <= to_be && to_be < end) {

			int index = (to_be - start) / sizeof(debug_menu_entry);

			memcpy(&cur->entries[index], &cur->entries[index + 1], cur->used_slots - (index + 1));
			memset(&cur->entries[cur->used_slots - 1], 0, sizeof(debug_menu_entry));
			cur->used_slots--;
			return;
		}
		
	}

	printf("FAILED TO DEALLOCATE AN ENTRY :S %08X\n", entry);

}

int vm_debug_menu_entry_garbage_collection_id = -1;

typedef int (*script_manager_register_allocated_stuff_callback_ptr)(void* func);
script_manager_register_allocated_stuff_callback_ptr script_manager_register_allocated_stuff_callback = (script_manager_register_allocated_stuff_callback_ptr) 0x005AFE40;

typedef int (*construct_client_script_libs_ptr)();
construct_client_script_libs_ptr construct_client_script_libs = (construct_client_script_libs_ptr) 0x0058F9C0;

void vm_debug_menu_entry_garbage_collection_callback(void* a1, list* lst) {

	list* end = lst->prev;

	for (list* cur = end->next; cur != end; cur = cur->next) {

		debug_menu_entry* entry = ((debug_menu_entry*)cur->data);
		//printf("Will delete %s %08X\n", entry->text, entry);
		remove_debug_menu_entry(entry);
	}
}

int construct_client_script_libs_hook() {

	if (vm_debug_menu_entry_garbage_collection_id == -1) {
		int res = script_manager_register_allocated_stuff_callback((void *) vm_debug_menu_entry_garbage_collection_callback);
		vm_debug_menu_entry_garbage_collection_id = res;
	}

	return construct_client_script_libs();
}

region** all_regions = (region **) 0x0095C924;
DWORD* number_of_allocated_regions = (DWORD *) 0x0095C920;

typedef const char* (__fastcall* region_get_name_ptr)(void* );
region_get_name_ptr region_get_name = (region_get_name_ptr) 0x00519BB0;

typedef int (__fastcall *region_get_district_variant_ptr)(region* );
region_get_district_variant_ptr region_get_district_variant = (region_get_district_variant_ptr) 0x005503D0;

void set_text_writeable() {

	const DWORD text_end = 0x86F000;
	const DWORD text_start = 0x401000;

	DWORD old;
	VirtualProtect((void*)text_start, text_end - text_start, PAGE_EXECUTE_READWRITE, &old);
}

void set_rdata_writeable() {

	const DWORD end = 0x91B000;
	const DWORD start = 0x86F564;

	DWORD old;
	VirtualProtect((void*)start, end - start, PAGE_READWRITE, &old);
}

void HookFunc(DWORD callAdd, DWORD funcAdd, BOOLEAN jump, const char* reason) {

	//Only works for E8/E9 hooks	
	DWORD jmpOff = funcAdd - callAdd - 5;

	BYTE shellcode[] = { 0, 0, 0, 0, 0 };
	shellcode[0] = jump ? 0xE9 : 0xE8;

	memcpy(&shellcode[1], &jmpOff, sizeof(jmpOff));
	memcpy((void*)callAdd, shellcode, sizeof(shellcode));

	if (reason)
		printf("Hook: %08X -  %s\n", callAdd, reason);

}


void WriteDWORD(int address, DWORD newValue, const char* reason) {
	* ((DWORD *)address) = newValue;
	if (reason)
		printf("Wrote: %08X -  %s\n", address, reason);
}

typedef int (*nflSystemOpenFile_ptr)(HANDLE* hHandle, LPCSTR lpFileName, unsigned int a3, LARGE_INTEGER liDistanceToMove);
nflSystemOpenFile_ptr nflSystemOpenFile_orig = nullptr;

nflSystemOpenFile_ptr* nflSystemOpenFile_data = (nflSystemOpenFile_ptr *) 0x0094985C;

HANDLE USM_handle = INVALID_HANDLE_VALUE;

int nflSystemOpenFile(HANDLE* hHandle, LPCSTR lpFileName, unsigned int a3, LARGE_INTEGER liDistanceToMove) {

	//printf("Opening file %s\n", lpFileName);
	int ret = nflSystemOpenFile_orig(hHandle, lpFileName, a3, liDistanceToMove);


	if (strstr(lpFileName, "ultimate_spiderman.PCPACK")) {

	}
	return ret;
}

typedef int (*ReadOrWrite_ptr)(int a1, HANDLE* a2, int a3, DWORD a4, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite);
ReadOrWrite_ptr* ReadOrWrite_data = (ReadOrWrite_ptr *)0x0094986C;
ReadOrWrite_ptr ReadOrWrite_orig = nullptr;

int ReadOrWrite(int a1, HANDLE* a2, int a3, DWORD a4, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

	int ret = ReadOrWrite_orig(a1, a2, a3, a4, lpBuffer, nNumberOfBytesToWrite);

	if (USM_handle == *a2) {
		printf("USM buffer was read %08X\n", (DWORD)lpBuffer);


	}
	return ret;
}

typedef void (*aeps_RenderAll_ptr)();
aeps_RenderAll_ptr aeps_RenderAll_orig = (aeps_RenderAll_ptr)0x004D9310;


unsigned int nglColor(int r, int g, int b, int a)
{
    return ( (a << 24) |  (r << 16) | (g << 8) | (b & 255) );
}

typedef void (*nglListEndScene_ptr)();
nglListEndScene_ptr nglListEndScene = (nglListEndScene_ptr) 0x00742B50;

typedef void (*nglSetClearFlags_ptr)(int);
nglSetClearFlags_ptr nglSetClearFlags = (nglSetClearFlags_ptr) 0x00769DB0;

void aeps_RenderAll() {
	static int cur_time = 0;
	int period = 60;
	int duration = 6 * period;
	float ratio = 1.f / period;

	uint8_t red = color_ramp_function(ratio, period, cur_time + 2 * period) + color_ramp_function(ratio, period, cur_time - 4 * period);
	uint8_t green = color_ramp_function(ratio, period, cur_time);
	uint8_t blue = color_ramp_function(ratio, period, cur_time - 2 * period);

	nglListAddString(*nglSysFont, 0.1f, 0.2f, 0.2f, nglColor(red, green, blue, 255), 1.f, 1.f, "Krystalgamer's Debug menu");

	cur_time = (cur_time + 1) % duration;


	aeps_RenderAll_orig();
}

int debug_enabled = 0;
uint32_t keys[256];

void getStringDimensions(const char* str, int* width, int* height) {
	nglGetStringDimensions(*nglSysFont, str, width, height, 1.0, 1.0);
}

int getStringHeight(const char* str) {
	int height;
	nglGetStringDimensions(nglSysFont(), str, nullptr, &height, 1.0, 1.0);
	return height;
}

std::string getRealText(debug_menu_entry *entry) {
    assert(entry->render_callback != nullptr);

    auto v1 = entry->render_callback(entry);

    char a2a[256]{};
    if (v1.size() != 0) {
        auto *v7 = v1.c_str();
        auto *v4 = entry->text;
        snprintf(a2a, 255u, "%s: %s", v4, v7);
    } else {
        auto *v5 = entry->text;
        snprintf(a2a, 255u, "%s", v5);
    }

    return {a2a};
}

void render_current_debug_menu() {
    auto UP_ARROW {" ^ ^ ^ "};
    auto DOWN_ARROW {" v v v "};

	int num_elements = std::min((DWORD) MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
	int needs_down_arrow = ((current_menu->window_start + MAX_ELEMENTS_PAGE) < current_menu->used_slots) ? 1 : 0;

	int cur_width, cur_height;
	int debug_width = 0;
	int debug_height = 0;

    auto get_and_update = [&](auto *x) {\
        getStringDimensions(x, &cur_width, &cur_height); \
        debug_height += cur_height; \
        debug_width = std::max(debug_width, cur_width);\
	};

	//printf("new size: %s %d %d (%d %d)\n", x, debug_width, debug_height, cur_width, cur_height);

	get_and_update(current_menu->title);
	get_and_update(UP_ARROW);

	int total_elements_page = needs_down_arrow ? MAX_ELEMENTS_PAGE : current_menu->used_slots - current_menu->window_start;

	for (int i = 0; i < total_elements_page; ++i) {
		debug_menu_entry *entry = &current_menu->entries[current_menu->window_start + i];
		auto cur = getRealText(entry);
		get_and_update(cur.c_str());
	}

	if (needs_down_arrow) {
		get_and_update(DOWN_ARROW);
	}

	nglQuad quad;

	int menu_x_start = 20, menu_y_start = 40;
	int menu_x_pad = 24, menu_y_pad = 18;

	nglInitQuad(&quad);
	nglSetQuadRect(&quad, menu_x_start, menu_y_start, menu_x_start + debug_width + menu_x_pad, menu_y_start + debug_height + menu_y_pad);
	nglSetQuadColor(&quad, 0xBE0A0A0A);
	nglSetQuadZ(&quad, 0.5f);
	nglListAddQuad(&quad);

	int white_color = nglColor(255, 255, 255, 255);
	int yellow_color = nglColor(255, 255, 0, 255);
	int green_color = nglColor(0, 255, 0, 255);
	int pink_color = nglColor(255, 0, 255, 255);

	int render_height = menu_y_start;
	render_height += 12;
	int render_x = menu_x_start;
	render_x += 8;
	nglListAddString(*nglSysFont, render_x, render_height, 0.2f, green_color, 1.f, 1.f, current_menu->title);
	render_height += getStringHeight(current_menu->title);

	if (current_menu->window_start) {
		nglListAddString(*nglSysFont, render_x, render_height, 0.2f, pink_color, 1.f, 1.f, UP_ARROW);
	}

	render_height += getStringHeight(UP_ARROW);

	for (int i = 0; i < total_elements_page; i++) {

		int current_color = current_menu->cur_index == i ? yellow_color : white_color;

		debug_menu_entry* entry = &current_menu->entries[current_menu->window_start + i];
		auto cur = getRealText(entry);
		nglListAddString(*nglSysFont, render_x, render_height, 0.2f, current_color, 1.f, 1.f, cur.c_str());
		render_height += getStringHeight(cur.c_str());
	}

	if (needs_down_arrow) {
		nglListAddString(*nglSysFont, render_x, render_height, 0.2f, pink_color, 1.f, 1.f, DOWN_ARROW);
		render_height += getStringHeight(DOWN_ARROW);
	}
}

void myDebugMenu() {
	if (debug_enabled) {
		render_current_debug_menu();
	}

	nglListEndScene();
}

typedef int (*wndHandler_ptr)(HWND, UINT, WPARAM, LPARAM);
wndHandler_ptr orig_WindowHandler = (wndHandler_ptr) 0x005941A0;

int WindowHandler(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam) {

	switch (Msg) {
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_KEYUP:
	case WM_KEYDOWN:
	case WM_INPUT:
		printf("swallowed keypress\n");
		return 0;
	}

	return orig_WindowHandler(hwnd, Msg, wParam, lParam);

}

/*
	STDMETHOD(GetDeviceState)(THIS_ DWORD,LPVOID) PURE;
	STDMETHOD(GetDeviceData)(THIS_ DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD) PURE;

*/

typedef int (__stdcall* GetDeviceState_ptr)(IDirectInputDevice8*, DWORD, LPVOID);
GetDeviceState_ptr GetDeviceStateOriginal = nullptr;

typedef int (__fastcall* game_pause_unpause_ptr)(void* );
game_pause_unpause_ptr game_pause = (game_pause_unpause_ptr) 0x0054FBE0;
game_pause_unpause_ptr game_unpause = (game_pause_unpause_ptr) 0x0053A880;

typedef int (__fastcall* game_get_cur_state_ptr)(void* );
game_get_cur_state_ptr game_get_cur_state = (game_get_cur_state_ptr) 0x005363D0;

typedef int (__fastcall* world_dynamics_system_remove_player_ptr)(void* , void* edx, int number);
world_dynamics_system_remove_player_ptr world_dynamics_system_remove_player = (world_dynamics_system_remove_player_ptr) 0x00558550;

typedef int (__fastcall* world_dynamics_system_add_player_ptr)(void* , void* edx, mString* str);
world_dynamics_system_add_player_ptr world_dynamics_system_add_player = (world_dynamics_system_add_player_ptr) 0x0055B400;

typedef int (*entity_teleport_abs_po_ptr)(DWORD, float*, int one);
entity_teleport_abs_po_ptr entity_teleport_abs_po = (entity_teleport_abs_po_ptr) 0x004F3890;

typedef DWORD* (__fastcall* ai_ai_core_get_info_node_ptr)(DWORD* , void* edx, int a2, char a3);
ai_ai_core_get_info_node_ptr ai_ai_core_get_info_node = (ai_ai_core_get_info_node_ptr) 0x006A3390;


struct vm_executable;

typedef struct {
	uint8_t unk[0x2C];
	script_object* object;
} script_instance;


struct vm_executable {
	struct {
        DWORD unk;
        script_executable *scriptexecutable;
    } *unk_struct;
	DWORD field_4;
	string_hash field_8;
	DWORD params;
};

typedef struct {
	uint8_t unk[0xC];
	script_instance* instance;
	vm_executable* vmexecutable;
} vm_thread;

struct vm_stack {
	uint8_t unk[0x184];
	DWORD stack_ptr;
	vm_thread* thread;

    void push(void* data, DWORD size) {
        memcpy((void *)this->stack_ptr, data, size);
        this->stack_ptr += size;
    }

    void pop(DWORD size) {
        this->stack_ptr -= size;
    }

};


uint8_t __stdcall slf__debug_menu_entry__set_handler__str(vm_stack *stack, void* unk) {

	stack->pop(8);

	void** params = (void**)stack->stack_ptr;

	debug_menu_entry* entry = static_cast<decltype(entry)>(params[0]);
	char* scrpttext = static_cast<char *>(params[1]);

	string_hash strhash {scrpttext};

	script_instance* instance = stack->thread->instance;
	int functionid = instance->object->find_func(strhash);
	entry->data = instance;
	entry->data1 = (void *) functionid;
	
	return 1;
}

uint8_t __stdcall slf__destroy_debug_menu_entry__debug_menu_entry(vm_stack* function, void* unk) {

	function->pop(4);

	debug_menu_entry** entry = (decltype(entry)) function->stack_ptr;

	remove_debug_menu_entry(*entry);

	return 1;
}

void handle_progression_select_entry(debug_menu_entry* entry);

void sub_65BB36(script_library_class::function *func, vm_stack *stack, char *a3, int a4)
{
    for ( auto i = 0; i < a4; ++i )
    {
        if ( *bit_cast<DWORD *>(&a3[4 * i]) == 0x7BAD05CF )
        {
            printf("uninitialized parameters in call to 0x%08X", func->m_vtbl);

            //v5 = j_vm_stack::get_thread(stack);
            //vm_thread::slf_error(v5, v6);

            assert(0 && "uninitialized parameters in call to script library function");
        }
    }
}

uint8_t __fastcall slf__create_progression_menu_entry(script_library_class::function *func, void *, vm_stack *stack, void *unk) {

	stack->pop(8);

    auto *stack_ptr = bit_cast<char *>(stack->stack_ptr);
    sub_65BB36(func, stack, stack_ptr, 2);

	char** strs = (char **)stack->stack_ptr;

	printf("Entry: %s -> %s\n", strs[0], strs[1]);


	string_hash strhash {strs[1]};

	script_instance* instance = stack->thread->instance;
	int functionid = instance->object->find_func(strhash);

	debug_menu_entry entry {};
	entry.entry_type = UNDEFINED;
	entry.data = instance;
	entry.data1 = (void *) functionid;

	strcpy(entry.text, strs[0]);
    entry.set_game_flags_handler(handle_progression_select_entry);

	add_debug_menu_entry(progression_menu, &entry);

	/*
	if(function->thread->instance->object->vmexecutable[functionid]->params != 4)
	*/
	
	int push = 0;
	stack->push(&push, sizeof(push));
	return true;
}

bool __fastcall slf__create_debug_menu_entry(script_library_class::function *func, void *, vm_stack* stack, void* unk) {
	stack->pop(4);

    auto *stack_ptr = bit_cast<char *>(stack->stack_ptr);
    sub_65BB36(func, stack, stack_ptr, 1);
	char** strs = bit_cast<char **>(stack->stack_ptr);

	//printf("Entry: %s ", strs[0]);

	debug_menu_entry entry {};
	entry.entry_type = UNDEFINED;
	strcpy(entry.text, strs[0]);

    printf("entry.text = %s\n", entry.text);

	script_instance* instance = stack->thread->instance;
    printf("Total funcs: %d\n", instance->object->total_funcs);

	void *res = add_debug_menu_entry(script_menu, &entry);

	script_executable* se = stack->thread->vmexecutable->unk_struct->scriptexecutable;
    printf("total_script_objects = %d\n", se->total_script_objects);
    for (auto i = 0; i < se->total_script_objects; ++i) {
        auto *so = se->field_28[i];
        printf("Name of script_object = %s\n", so->field_0.to_string());

        for (auto i = 0; i < so->total_funcs; ++i) {
            printf("Func name: %s\n", so->funcs[i]->field_8.to_string());
        }

        printf("\n");
    }

	se->add_allocated_stuff(vm_debug_menu_entry_garbage_collection_id, (int) res, 0);

	//printf("%08X\n", res);

	int push = (int) res;
	stack->push(&push, sizeof(push));
	return 1;
}

DWORD modulo(int num, DWORD mod) {
	if (num >= 0) {
		return num % mod;
	}

	int absolute = abs(num);
	if (absolute % mod == 0)
		return 0;

	return mod - absolute % mod;
}


void menu_go_down() {


	if ((current_menu->window_start + MAX_ELEMENTS_PAGE) < current_menu->used_slots) {

		if (current_menu->cur_index < MAX_ELEMENTS_PAGE / 2)
			++current_menu->cur_index;
		else
			++current_menu->window_start;
	}
	else {

		int num_elements = std::min((DWORD) MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
		current_menu->cur_index = modulo(current_menu->cur_index + 1, num_elements);
		if (current_menu->cur_index == 0)
			current_menu->window_start = 0;
	}
}

void menu_go_up() {

	int num_elements = std::min( (DWORD) MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
	if (current_menu->window_start) {


		if (current_menu->cur_index > MAX_ELEMENTS_PAGE / 2)
			current_menu->cur_index--;
		else
			current_menu->window_start--;

	}
	else {

		int num_elements = std::min(MAX_ELEMENTS_PAGE, current_menu->used_slots - current_menu->window_start);
		current_menu->cur_index = modulo(current_menu->cur_index - 1, num_elements);
		if (current_menu->cur_index == (num_elements - 1))
			current_menu->window_start = current_menu->used_slots - num_elements;

	}

}

typedef enum {
	MENU_TOGGLE,
	MENU_ACCEPT,
	MENU_BACK,

	MENU_UP,
	MENU_DOWN,
	MENU_LEFT,
	MENU_RIGHT,


	MENU_KEY_MAX
} MenuKey;

uint32_t controllerKeys[MENU_KEY_MAX];

int get_menu_key_value(MenuKey key, int keyboard) {
	if (keyboard) {

		int i = 0;
		switch (key) {
			case MENU_TOGGLE:
				i = DIK_INSERT;
				break;
			case MENU_ACCEPT:
				i = DIK_RETURN;
				break;
			case MENU_BACK:
				i = DIK_ESCAPE;
				break;

			case MENU_UP:
				i = DIK_UPARROW;
				break;
			case MENU_DOWN:
				i = DIK_DOWNARROW;
				break;
			case MENU_LEFT:
				i = DIK_LEFTARROW;
				break;
			case MENU_RIGHT:
				i = DIK_RIGHTARROW;
				break;
		}
		return keys[i];
	}



	return controllerKeys[key];
}


int is_menu_key_pressed(MenuKey key, int keyboard) {
	return (get_menu_key_value(key, keyboard) == 2);
}

int is_menu_key_clicked(MenuKey key, int keyboard) {
	return get_menu_key_value(key, keyboard);
}

void GetDeviceStateHandleKeyboardInput(LPVOID lpvData) {
	BYTE* keysCurrent = (BYTE *) lpvData;

	for (int i = 0; i < 256; i++) {

		if (keysCurrent[i]) {
			keys[i]++;
		}
		else {
			keys[i] = 0;
		}
	}

	
}

void read_and_update_controller_key_button(LPDIJOYSTATE2 joy, int index, MenuKey key) {
	int res = 0;
	if (joy->rgbButtons[index]) {
		++controllerKeys[key];
	}
	else {
		controllerKeys[key] = 0;
	}
}


void read_and_update_controller_key_dpad(LPDIJOYSTATE2 joy, int angle, MenuKey key) {
	
	if (joy->rgdwPOV[0] == 0xFFFFFFFF)
		controllerKeys[key] = 0;
	else
		controllerKeys[key] = (joy->rgdwPOV[0] == angle) ? controllerKeys[key] + 1 : 0;
}


void GetDeviceStateHandleControllerInput(LPVOID lpvData) {
	LPDIJOYSTATE2 joy = (decltype(joy)) lpvData;

	read_and_update_controller_key_button(joy, 1, MENU_ACCEPT);
	read_and_update_controller_key_button(joy, 2, MENU_BACK);
	read_and_update_controller_key_button(joy, 12, MENU_TOGGLE);

	read_and_update_controller_key_dpad(joy, 0, MENU_UP);
	read_and_update_controller_key_dpad(joy, 9000, MENU_RIGHT);
	read_and_update_controller_key_dpad(joy, 18000, MENU_DOWN);
	read_and_update_controller_key_dpad(joy, 27000, MENU_LEFT);
}

typedef int (*resource_manager_can_reload_amalgapak_ptr)(void);
resource_manager_can_reload_amalgapak_ptr resource_manager_can_reload_amalgapak = (resource_manager_can_reload_amalgapak_ptr) 0x0053DE90;

typedef void (*resource_manager_reload_amalgapak_ptr)(void);
resource_manager_reload_amalgapak_ptr resource_manager_reload_amalgapak = (resource_manager_reload_amalgapak_ptr) 0x0054C2E0;

struct mission_t
{
    std::string field_0;
    const char *field_C;
    int field_10;
    int field_14;
};

std::vector<mission_t> menu_missions; 

void mission_unload_handler(debug_menu_entry *a1)
{
    auto *v1 = mission_manager::s_inst();
    v1->prepare_unload_script();

	close_debug();
}

void mission_select_handler(debug_menu_entry *entry)
{
    auto v1 = (int) entry->data1;
    auto v7 = &menu_missions.at(v1);
    auto v6 = v7->field_C;
    auto v5 = v7->field_14;
    auto *v4 = v7->field_0.c_str();
    auto v3 = v7->field_10;
    auto *v2 = mission_manager::s_inst();
    v2->force_mission(v3, v4, v5, v6);
    close_debug();
}

void create_game_flags_menu(debug_menu *parent);

void populate_missions_menu(debug_menu *missions_menu)
{
    if (missions_menu->used_slots == 0)
    {
        menu_missions = {};
        if ( resource_manager_can_reload_amalgapak() )
        {
            resource_manager_reload_amalgapak();
        }

        auto *head_menu = missions_menu;

        debug_menu_entry mission_unload_entry {"UNLOAD CURRENT MISSION"};

        mission_unload_entry.set_game_flags_handler(mission_unload_handler);
        add_debug_menu_entry(head_menu, &mission_unload_entry);

        mission_manager *v2 = mission_manager::s_inst();
        int v58 = v2->m_district_table_count;
        for ( int i = -1; i < v58; ++i )
        {
            fixedstring32 v53{};
            int v52;
            mission_table_container *table = nullptr;
            if ( i == -1 )
            {
                table = v2->m_global_table_container;
                fixedstring32 a1{"global"};
                v53 = a1;
                v52 = 0;
            }
            else
            {
                table = v2->m_district_table_containers[i];
                auto *reg = table->field_44;
                auto &v6 = reg->get_name();
                v53 = v6.to_string();

                auto v52 = reg->get_district_id();

                auto *v25 = create_menu(v53.to_string(), nullptr, 10);

                debug_menu_entry v26 {v25};

                add_debug_menu_entry(head_menu, &v26);
            }

            std::vector<mission_table_container::script_info> script_infos;

            if ( table != nullptr )
            {
                table->append_script_info(&script_infos);
            }

            for ( auto &info : script_infos)
            {
                auto v50 = menu_missions.size();
                std::string a2{"pk_"};
                auto v19 = a2 + info.field_0;
                auto *v11 = v19.c_str();
                auto key = create_resource_key_from_path(v11, 25);
                if ( resource_manager::get_pack_file_stats(key, nullptr, nullptr, nullptr) )
                {
                    mission_t mission{};
                    mission.field_0 = info.field_0;
                    mission.field_10 = v52;
                    mission.field_14 = info.field_8;

                    mission.field_C = info.field_4->get_script_data_name();
                    menu_missions.push_back(mission);

                    std::string v47{};

                    char buff[1024];
                    if ( mission.field_C != nullptr )
                    {
                        auto *v17 = mission.field_C;
                        auto *v14 = mission.field_0.c_str();

                        snprintf(buff, sizeof(buff), "%s (%s)", v14, v17);
                        v47 = buff;
                    }
                    else
                    {
                        auto v18 = mission.field_14;
                        auto *v15 = mission.field_0.c_str();

                        snprintf(buff, sizeof(buff), "%s (%d)", v15, v18);
                        v47 = buff;
                    }

                    debug_menu_entry v27 {v47.c_str()};

                    //v27->set_id(v50);
                    v27.data1 = (void *) v50;

                    v27.set_game_flags_handler(mission_select_handler);
                    add_debug_menu_entry(head_menu, &v27);
                }
            }
        }
    
	}
}

void menu_setup(int game_state, int keyboard) {

	//debug menu stuff
	if (is_menu_key_pressed(MENU_TOGGLE, keyboard) && (game_state == 6 || game_state == 7)) {

		if (debug_enabled && game_state == 7) {
			game_unpause(g_game_ptr());
			debug_enabled = !debug_enabled;
		}
		else if (!debug_enabled && game_state == 6) {
			game_pause(g_game_ptr());
			debug_enabled = !debug_enabled;
			current_menu = debug_menu::root_menu;
		}

		populate_missions_menu(missions_menu);

        create_game_flags_menu(game_menu);

        if (level_select_menu->used_slots == 0)
        {
            create_level_select_menu(level_select_menu);
        }

		if (options_menu->used_slots == 2) {
			BYTE* arr = (BYTE *) *(DWORD*)0x96858C;
			debug_menu_entry render_fe = { "Render FE UI ", POINTER_BOOL, &arr[4 + 0x90] };
			add_debug_menu_entry(options_menu, &render_fe);


			BYTE* flags = *(BYTE**)0x0096858C;
			debug_menu_entry live_in_glass_house = { "Live in Glass House ", POINTER_BOOL,  &flags[4 + 0x7A] };
			add_debug_menu_entry(options_menu, &live_in_glass_house);


			debug_menu_entry god_mode_entry = { "God Mode ", INTEGER, (void *) 0 };
            god_mode_entry.m_id = 5;

            add_debug_menu_entry(options_menu, &god_mode_entry);
		}
	}
}

void menu_input_handler(int keyboard, int SCROLL_SPEED) {
	if (is_menu_key_clicked(MENU_DOWN, keyboard)) {

		int key_val = get_menu_key_value(MENU_DOWN, keyboard);
		if (key_val == 1) {
			menu_go_down();
		}
		else if ((key_val >= SCROLL_SPEED) && (key_val % SCROLL_SPEED == 0)) {
			menu_go_down();
		}
	}
	else if (is_menu_key_clicked(MENU_UP, keyboard)) {

		int key_val = get_menu_key_value(MENU_UP, keyboard);
		if (key_val == 1) {
			menu_go_up();
		}
		else if ((key_val >= SCROLL_SPEED) && (key_val % SCROLL_SPEED == 0)) {
			menu_go_up();
		}
	}
	else if (is_menu_key_pressed(MENU_ACCEPT, keyboard))
    {
        auto *entry = &current_menu->entries[current_menu->window_start + current_menu->cur_index];
        assert(entry != nullptr);
        entry->on_select(1.0);

		//current_menu->handler(entry, ENTER);
	}
	else if (is_menu_key_pressed(MENU_BACK, keyboard)) {
		current_menu->go_back();
	}
	else if (is_menu_key_pressed(MENU_LEFT, keyboard) || is_menu_key_pressed(MENU_RIGHT, keyboard)) {

		debug_menu_entry* cur = &current_menu->entries[current_menu->window_start + current_menu->cur_index];
		if (cur->entry_type == POINTER_BOOL ||
                cur->entry_type == POINTER_INT ||
                cur->entry_type == INTEGER
                ) {
			//current_menu->handler(cur, (is_menu_key_pressed(MENU_LEFT, keyboard) ? LEFT : RIGHT));

            if (is_menu_key_pressed(MENU_LEFT, keyboard)) {
                cur->on_change(-1.0, false);
            } else {
                cur->on_change(1.0, true);
            }
        }
	}

	debug_menu_entry *highlighted = &current_menu->entries[current_menu->window_start + current_menu->cur_index];
    assert(highlighted->frame_advance_callback != nullptr);
    highlighted->frame_advance_callback(highlighted);
}


HRESULT __stdcall GetDeviceStateHook(IDirectInputDevice8* self, DWORD cbData, LPVOID lpvData) {

	HRESULT res = GetDeviceStateOriginal(self, cbData, lpvData);

	//printf("cbData %d %d %d\n", cbData, sizeof(DIJOYSTATE), sizeof(DIJOYSTATE2));


	
	//keyboard time babyyy
	if (cbData == 256 || cbData == sizeof(DIJOYSTATE2)) {

		
		if (cbData == 256)
			GetDeviceStateHandleKeyboardInput(lpvData);
		else if (cbData == sizeof(DIJOYSTATE2))
			GetDeviceStateHandleControllerInput(lpvData);

		int game_state = 0;
		if (g_game_ptr())
        {
			game_state = game_get_cur_state(g_game_ptr());
        }

		//printf("INSERT %d %d %c\n", keys[DIK_INSERT], game_state, debug_enabled ? 'y' : 'n');

		int keyboard = cbData == 256;
		menu_setup(game_state, keyboard);

		if (debug_enabled) {
			menu_input_handler(keyboard, 5);
		}

	}


	if (debug_enabled) {
		memset(lpvData, 0, cbData);
	}



	//printf("Device State called %08X %d\n", this, cbData);

	return res;
}

typedef HRESULT(__stdcall* GetDeviceData_ptr)(IDirectInputDevice8*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
GetDeviceData_ptr GetDeviceDataOriginal = nullptr;

HRESULT __stdcall GetDeviceDataHook(IDirectInputDevice8* self, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) {

	HRESULT res = GetDeviceDataOriginal(self, cbObjectData, rgdod, pdwInOut, dwFlags);

	printf("data\n");
	if (res == DI_OK) {

		printf("All gud\n");
		for (int i = 0; i < *pdwInOut; i++) {


			if (LOBYTE(rgdod[i].dwData) > 0) {

				if (rgdod[i].dwOfs == DIK_ESCAPE) {

					printf("Pressed escaped\n");
					__debugbreak();
				}
			}
		}
	}
	//printf("Device Data called %08X\n", this);

	return res;
}



typedef HRESULT(__stdcall* IDirectInput8CreateDevice_ptr)(IDirectInput8W*, const GUID*, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
IDirectInput8CreateDevice_ptr createDeviceOriginal = nullptr;

HRESULT  __stdcall IDirectInput8CreateDeviceHook(IDirectInput8W* self, const GUID* guid, LPDIRECTINPUTDEVICE8W* device, LPUNKNOWN unk) {

	//printf("CreateDevice %d %d %d %d %d %d %d\n", *guid, GUID_SysMouse, GUID_SysKeyboard, GUID_SysKeyboardEm, GUID_SysKeyboardEm2, GUID_SysMouseEm, GUID_SysMouseEm2);
	printf("Guid = {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
		guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);

	HRESULT res = createDeviceOriginal(self, guid, device, unk);


	if (IsEqualGUID(GUID_SysMouse, *guid))
		return res; // ignore mouse

	if (IsEqualGUID(GUID_SysKeyboard, *guid))
		puts("Found the keyboard");
	else
		puts("Hooking something different...maybe a controller");

#if 0
	DWORD* vtbl = (*device)->lpVtbl;
	if (!GetDeviceStateOriginal) {
		GetDeviceStateOriginal = vtbl[9];
		vtbl[9] = GetDeviceStateHook;
	}

	if (!GetDeviceDataOriginal) {
		GetDeviceDataOriginal = vtbl[10];
		vtbl[10] = GetDeviceDataHook;
	}
#else
    if (GetDeviceStateOriginal == nullptr) {
        GetDeviceStateOriginal = (GetDeviceState_ptr)
            HookVTableFunction((void *) *device, (void *) GetDeviceStateHook, 9);
    }

    if (GetDeviceDataOriginal == nullptr) {
        GetDeviceDataOriginal = (GetDeviceData_ptr) HookVTableFunction((void *) *device,
                                                                       (void *) GetDeviceDataHook,
                                                                       10);
    }
#endif

	return res;
}

typedef HRESULT(__stdcall* IDirectInput8Release_ptr)(IDirectInput8W*);
IDirectInput8Release_ptr releaseDeviceOriginal = nullptr;

HRESULT  __stdcall IDirectInput8ReleaseHook(IDirectInput8W* self) {

	printf("Release\n");

	return releaseDeviceOriginal(self);
}

/*
BOOL CALLBACK EnumDevices(LPCDIDEVICEINSTANCE lpddi, LPVOID buffer) {

	wchar_t wGUID[40] = { 0 };
	char cGUID[40] = { 0 };

	//printf("%d\n", lpddi->guidProduct);
}
*/

typedef HRESULT(__stdcall* DirectInput8Create_ptr)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
HRESULT __stdcall HookDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
	DirectInput8Create_ptr caller = (decltype(caller)) *(void**)0x00987944;
	HRESULT res = caller(hinst, dwVersion, riidltf, ppvOut, punkOuter);

	IDirectInput8* iDir = (IDirectInput8 *) (*ppvOut);
	//printf("it's me mario %08X %08X\n", ppvOut, (*iDir)->lpVtbl);

#if 0
	DWORD* vtbl = (DWORD*)(*iDir)->lpVtbl;
	if (!createDeviceOriginal) {
		createDeviceOriginal = vtbl[3];
		vtbl[3] = IDirectInput8CreateDeviceHook;
	}
#else
    if (createDeviceOriginal == nullptr) {
        createDeviceOriginal = (IDirectInput8CreateDevice_ptr)
            HookVTableFunction((void *) iDir, (void *) IDirectInput8CreateDeviceHook, 3);
    }
#endif

	//(*iDir)->lpVtbl->EnumDevices(*iDir, DI8DEVCLASS_ALL, EnumDevices, NULL, DIEDFL_ALLDEVICES);
	return res;
}

DWORD hookDirectInputAddress = (DWORD) HookDirectInput8Create;

/*
void update_state() {

	while (1) {
		OutputDebugStringA("PILA %d", 6);
	}
}
*/

typedef int(__fastcall* game_handle_game_states_ptr)(game* , void* edx, void* a2);
game_handle_game_states_ptr game_handle_game_states_original = (game_handle_game_states_ptr) 0x0055D510;

int __fastcall game_handle_game_states(game* self, void* edx, void* a2) {

	if (!g_game_ptr()) {
		g_game_ptr() = self;
	}

	/*
	if (game_get_cur_state(this) == 14)
		__debugbreak();
		*/


		//printf("Current state %d %08X\n", game_get_cur_state(this), g_game_ptr);

	return game_handle_game_states_original(self, edx, a2);
}


typedef DWORD(__fastcall* ai_hero_base_state_check_transition_ptr)(DWORD* , void* edx, DWORD* a2, int a3);
ai_hero_base_state_check_transition_ptr ai_hero_base_state_check_transition = (ai_hero_base_state_check_transition_ptr) 0x00478D80;

DWORD __fastcall ai_hero_base_state_check_transition_hook(DWORD* self, void* edx, DWORD* a2, int a3) {
	ai_current_player = self;
	return ai_hero_base_state_check_transition(self, edx, a2, a3);
}

typedef DWORD* (__fastcall* get_info_node_ptr)(void*, void* edx, int a2, char a3);
get_info_node_ptr get_info_node = (get_info_node_ptr) 0x006A3390;

DWORD* __fastcall get_info_node_hook(void* self, void* edx, int a2, char a3) {

	DWORD* res = get_info_node(self, edx, a2, a3);

	fancy_player_ptr = res;
	return res;
}

void init_shadow_targets()
{
    debug_menu::init();

    CDECL_CALL(0x00592E80);
}


typedef void (__fastcall* resource_pack_streamer_load_internal_ptr)(void* self, void* edx, char* str, int a3, int a4, int a5);
resource_pack_streamer_load_internal_ptr resource_pack_streamer_load_internal = (resource_pack_streamer_load_internal_ptr) 0x0054C580;

void __fastcall resource_pack_streamer_load_internal_hook(void* self, void* edx, char* str, int a3, int a4, int a5) {

	resource_pack_streamer_load_internal(self, edx, str, a3, a4, a5);
}

uint8_t __fastcall os_developer_options(BYTE *self, void *edx, int flag) {

	char** flag_list = (decltype(flag_list)) 0x936420;
	char* flag_text = flag_list[flag];
		
	uint8_t res = self[flag + 4];

	if (flag == 0x90) {
		printf("Game wants to know about: %d (%s) -> %d\n", flag, flag_text, res);
		__debugbreak();
	}
	
	
	//this[5 + 4] = 1;
	
	return res;
}

unsigned int hook_controlfp(unsigned int, unsigned int) {
    return {};
}

static constexpr uint32_t NOP = 0x90;

void set_nop(ptrdiff_t address, size_t num_bytes) {
    for (size_t i = 0u; i < num_bytes; ++i) {
        *bit_cast<uint8_t *>(static_cast<size_t>(address) + i) = NOP;
    }
}

void install_patches() {


    //fix invalid float operation
    {
        set_nop(0x005AC34C, 6);

        HookFunc(0x005AC347, (DWORD) hook_controlfp, 0, "Patching call to controlfp");
    }

    REDIRECT(0x005E10EE, init_shadow_targets);

    ngl_patch();

    game_patch();

    slab_allocator_patch();

    HookFunc(0x0052B4BF, (DWORD) spider_monkey::render, 0, "Patching call to spider_monkey::render");

	HookFunc(0x004EACF0, (DWORD)aeps_RenderAll, 0, "Patching call to aeps::RenderAll");

	HookFunc(0x0052B5D7, (DWORD)myDebugMenu, 0, "Hooking nglListEndScene to inject debug menu");
	//save orig ptr
	nflSystemOpenFile_orig = *nflSystemOpenFile_data;
	*nflSystemOpenFile_data = &nflSystemOpenFile;
	printf("Replaced nflSystemOpenFile %08X -> %08X\n", (DWORD)nflSystemOpenFile_orig, (DWORD)&nflSystemOpenFile);


	ReadOrWrite_orig = *ReadOrWrite_data;
	*ReadOrWrite_data = &ReadOrWrite;
	printf("Replaced ReadOrWrite %08X -> %08X\n", (DWORD)ReadOrWrite_orig, (DWORD)&ReadOrWrite);

	*(DWORD*)0x008218B2 = (DWORD) &hookDirectInputAddress;
	printf("Patching the DirectInput8Create call\n");


	HookFunc(0x0055D742, (DWORD)game_handle_game_states, 0, "Hooking handle_game_states");

	/*
	WriteDWORD(0x00877524, ai_hero_base_state_check_transition_hook, "Hooking check_transition for peter hooded");
	WriteDWORD(0x00877560, ai_hero_base_state_check_transition_hook, "Hooking check_transition for spider-man");
	WriteDWORD(0x0087759C, ai_hero_base_state_check_transition_hook, "Hooking check_transition for venom");
	*/

	HookFunc(0x00478DBF, (DWORD) get_info_node_hook, 0, "Hook get_info_node to get player ptr");


	WriteDWORD(0x0089C710, (DWORD) slf__create_progression_menu_entry, "Hooking first ocurrence of create_progession_menu_entry");
	WriteDWORD(0x0089C718, (DWORD) slf__create_progression_menu_entry, "Hooking second  ocurrence of create_progession_menu_entry");


	WriteDWORD(0x0089AF70, (DWORD) slf__create_debug_menu_entry, "Hooking first ocurrence of create_debug_menu_entry");
	WriteDWORD(0x0089C708, (DWORD) slf__create_debug_menu_entry, "Hooking second  ocurrence of create_debug_menu_entry");


	HookFunc(0x005AD77D, (DWORD) construct_client_script_libs_hook, 0, "Hooking construct_client_script_libs to inject my vm");

	WriteDWORD(0x0089C720, (DWORD) slf__destroy_debug_menu_entry__debug_menu_entry, "Hooking destroy_debug_menu_entry");
	WriteDWORD(0x0089C750, (DWORD) slf__debug_menu_entry__set_handler__str, "Hooking set_handler");

	//HookFunc(0x0054C89C, resource_pack_streamer_load_internal_hook, 0, "Hooking resource_pack_streamer::load_internal to inject interior loading");

	//HookFunc(0x005B87E0, os_developer_options, 1, "Hooking os_developer_options::get_flag");

	/*

	DWORD* windowHandler = 0x005AC48B;
	*windowHandler = WindowHandler;

	DWORD* otherHandler = 0x0076D6D1;
	*otherHandler = 0;

	*/
}

void close_debug() {
	debug_enabled = 0;
	game_unpause(g_game_ptr());
}

void handle_debug_entry(debug_menu_entry* entry, custom_key_type) {
	current_menu = static_cast<decltype(current_menu)>(entry->data);
}

typedef bool (__fastcall *entity_tracker_manager_get_the_arrow_target_pos_ptr)(void *, void *, vector3d *);
entity_tracker_manager_get_the_arrow_target_pos_ptr entity_tracker_manager_get_the_arrow_target_pos = (entity_tracker_manager_get_the_arrow_target_pos_ptr) 0x0062EE10;

void set_god_mode(int a1)
{
    CDECL_CALL(0x004BC040, a1);
}

void handle_options_select_entry(debug_menu_entry* entry, custom_key_type key_type) {

    if (entry->m_id == 5)
    {
        auto val = (int) entry->data;
        if (key_type == LEFT)
        {
            --val;
        }
        else if (key_type == RIGHT)
        {
            ++val;
        }

        val = std::clamp(val, 0, 5);

        entry->data = (void *) val;

        set_god_mode(val);

        return;
    }

	BYTE* val = (BYTE *) entry->data;
	*val = !*val;
}

typedef void* (__fastcall* script_instance_add_thread_ptr)(script_instance* , void* edx, vm_executable* a1, void* a2);
script_instance_add_thread_ptr script_instance_add_thread = (script_instance_add_thread_ptr) 0x005AAD00;

void handle_progression_select_entry(debug_menu_entry* entry) {

	script_instance* instance = static_cast<script_instance *>(entry->data);
	int functionid = (int) entry->data1;

	DWORD addr = (DWORD) entry;

	script_instance_add_thread(instance, nullptr, instance->object->funcs[functionid], &addr);

	close_debug();
}

void handle_script_select_entry(debug_menu_entry* entry) {
	handle_progression_select_entry(entry);
}

void create_devopt_menu(debug_menu *parent)
{
    assert(parent != nullptr);

    auto *v22 = create_menu("Devopts", handle_game_entry, 300);

    for ( auto idx = 0u; idx < NUM_OPTIONS; ++idx )
    {
        auto *v21 = get_option(idx);
        switch ( v21->m_type )
        {
        case game_option_t::INT_OPTION:
        {
            auto v20 = debug_menu_entry(mString{v21->m_name});
            v20.set_p_ival(v21->m_value.p_ival);
            v20.set_min_value(-1000.0);
            v20.set_max_value(1000.0);
            v22->add_entry(&v20);
            break;
        }
        case game_option_t::FLAG_OPTION:
        {
            auto v19 = debug_menu_entry(mString{v21->m_name});
            v19.set_pt_bval((bool *) v21->m_value.p_bval);
            v22->add_entry(&v19);
            break;
        }
        case game_option_t::FLOAT_OPTION:
        {
            auto v18 = debug_menu_entry(mString{v21->m_name});
            v18.set_pt_fval(v21->m_value.p_fval);
            v18.set_min_value(-1000.0);
            v18.set_max_value(1000.0);
            v22->add_entry(&v18);
            break;
        }
        default:
            break;
        }
    }

    auto v5 = debug_menu_entry(v22);
    parent->add_entry(&v5);
}

namespace spider_monkey {
    bool is_running()
    {
        return (bool) CDECL_CALL(0x004B3B60);
    }
}

void game_flags_handler(debug_menu_entry *a1)
{
    switch ( a1->get_id() )
    {
    case 0u: //Physics Enabled
    {
        auto v1 = a1->get_bval();
        g_game_ptr()->enable_physics(v1);
        debug_menu::physics_state_on_exit = a1->get_bval();
        break;
    }
    case 1u: //Single Step
    {
        g_game_ptr()->flag.single_step = true;
        break;
    }
    case 2u: //Slow Motion Enabled
    {
        static int old_frame_lock = 0;
        int v27;
        if ( a1->get_bval() )
        {
            old_frame_lock = os_developer_options::instance()->get_int(mString{"FRAME_LOCK"});
            v27 = 120;
        }
        else
        {
            v27 = old_frame_lock;
        }

        os_developer_options::instance()->set_int(mString{"FRAME_LOCK"}, v27);
        debug_menu::hide();
        break;
    }
    case 3u: //Monkey Enabled
    {
        if ( a1->get_bval() )
        {
            spider_monkey::start();
            spider_monkey::on_level_load();
            auto *v2 = input_mgr::instance();
            auto *rumble_device = v2->rumble_ptr;

            assert(rumble_device != nullptr);
            rumble_device->disable_vibration();
        }
        else
        {
            spider_monkey::on_level_unload();
            spider_monkey::stop();
        }

        debug_menu::hide();
        break;
    }
    case 4u:
    {
        auto *v3 = input_mgr::instance();
        auto *rumble_device = v3->rumble_ptr;
        assert(rumble_device != nullptr);

        if ( a1->get_bval() )
            rumble_device->enable_vibration();
        else
            rumble_device->disable_vibration();

        break;
    }
    case 5u: //God Mode
    {
        auto v4 = a1->get_ival();
        set_god_mode(v4);
        debug_menu::hide();
        break;
    }
    case 6u: //Show Districts
    {
        debug_menu::hide();
        os_developer_options::instance()->set_flag(mString{"SHOW_STREAMER_INFO"}, a1->get_bval());

        if ( a1->get_bval() )
        {
            os_developer_options::instance()->set_flag(mString{"SHOW_DEBUG_TEXT"}, true);
        }

        //TODO
        //sub_66C242(&g_game_ptr->field_4C);
        break;
    }
    case 7u: //Show Hero Position
    {
        debug_menu::hide();
        os_developer_options::instance()->set_flag(mString{"SHOW_DEBUG_INFO"}, a1->get_bval());
        break;
    }
    case 8u:
    {
        debug_menu::hide();
        os_developer_options::instance()->set_flag(mString{"SHOW_FPS"}, a1->get_bval());
        break;
    }
    case 9u:
    {
        auto v24 = a1->get_bval();
        auto *v5 = input_mgr::instance();
        if ( !v5->field_30[1] )
        {
            v24 = false;
        }


        os_developer_options::instance()->set_flag(mString{"USERCAM_ON_CONTROLLER2"}, v24);
        auto *v6 = input_mgr::instance();
        auto *v23 = v6->field_30[1];

        //TODO
        /*
        if ( !(*(unsigned __int8 (__thiscall **)(int))(*(_DWORD *)v23 + 44))(v23) )
        {
            j_debug_print_va("Controller 2 is not connected!");
            ->set_bval(a1, 0, 1);
            v24 = 0;
        }
        if ( v24 )
        {
            j_debug_print_va("User cam (theoretically) enabled on controller 2");
            v7 = (*(int (__thiscall **)(int))(*(_DWORD *)v23 + 8))(v23);
            sub_676E45(g_mouselook_controller, v7);
        }
        else
        {
            sub_676E45(g_mouselook_controller, -1);
        }
        */

        auto *v8 = g_world_ptr()->get_hero_ptr(0);
        if ( v8 != nullptr && g_game_ptr()->field_172 )
        {
            if ( a1->get_bval() )
            {
                auto *v14 = g_world_ptr()->get_hero_ptr(0);
                v14->unsuspend(1);
            }
            else
            {
                auto *v15 = g_world_ptr()->get_hero_ptr(0);
                v15->suspend(1);
            }
        }
        break;
    }
    case 11u: //Hires Screenshot
    {
        debug_menu::hide();
        auto a2 = os_developer_options::instance()->get_int(mString{"HIRES_SCREENSHOT_X"});
        auto a3 = os_developer_options::instance()->get_int(mString{"HIRES_SCREENSHOT_Y"});
        assert(a2 != 0 && a3 != 0 && "HIRES_SCREENSHOT_X and HIRES_SCREENSHOT_Y must be not equal 0");
        g_game_ptr()->begin_hires_screenshot(a2, a3);
        break;
    }
    case 12u: //Lores Screenshot
    {
        g_game_ptr()->push_lores();
        break;
    }
    case 13u:
    {
        static auto load_districts = TRUE;
        if ( load_districts )
        {
            auto *v11 = g_world_ptr()->the_terrain;
            v11->unload_all_districts_immediate();
            resource_manager::set_active_district(false);
        }
        else
        {
            resource_manager::set_active_district(true);
        }

        load_districts = !load_districts;
        debug_menu::hide();
        break;
    }
    case 14u:
    {
        //TODO
        //sub_66FBE0();
        debug_menu::hide();
        break;
    }
    case 15u:
    {
        //sub_697DB1();
        debug_menu::hide();
        break;
    }
    case 16u:
    {
        //TODO
        //sub_698D33();
        debug_menu::hide();
        break;
    }
    case 17u:
    {
        [[maybe_unused]]auto v12 = a1->get_bval();

        //TODO
        //sub_6A88A5(g_game_ptr, v12);
        break;
    }
    case 18u:
    {
        auto v13 = a1->get_ival();
        a1->set_ival(v13, 0);
        auto v16 = a1->get_ival();
        if ( v16 )
        {
            if ( v16 == 1 )
            {
                if ( geometry_manager::is_scene_analyzer_enabled() )
                {
                    geometry_manager::enable_scene_analyzer(false);
                }

                g_game_ptr()->field_172 = true;

            }
            else if ( v16 == 2 )
            {
                g_game_ptr()->field_172 = false;
                geometry_manager::enable_scene_analyzer(true);
            }
        }
        else
        {
            if ( geometry_manager::is_scene_analyzer_enabled() )
            {
                geometry_manager::enable_scene_analyzer(false);
            }

            g_game_ptr()->field_172 = false;
        }
    break;
    }
    default:
    return;
    }
}

void create_game_flags_menu(debug_menu *parent)
{
    if (parent->used_slots != 0)
    {
        return;
    }

    assert(parent != nullptr);

    auto *v92 = parent;

    auto v89 = debug_menu_entry(mString{"Report SLF Recall Timeouts"});
    static bool byte_1597BC0 = false;
    v89.set_pt_bval(&byte_1597BC0);
    //v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Physics Enabled"});
    v89.set_bval(true);
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(0);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Single Step"});
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(1);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Slow Motion Enabled"});
    v89.set_bval(false);
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(2);
    v92->add_entry(&v89);

    v89 = debug_menu_entry{mString{"Monkey Enabled"}};

    auto v1 = spider_monkey::is_running();
    v89.set_bval(v1);
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(3);
    v92->add_entry(&v89);

    v89 = debug_menu_entry{mString{"Rumble Enabled"}};
    v89.set_bval(true);
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(4);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"God Mode"});
    v89.set_ival(os_developer_options::instance()->get_int(mString{"GOD_MODE"}));

    const float v2[4] = {0, 5, 1, 1};
    v89.set_fl_values(v2);
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(5);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Show Districts"});
    v89.set_bval(os_developer_options::instance()->get_flag(mString{"SHOW_STREAMER_INFO"}));
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(6);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Show Hero Position"});
    v89.set_bval(os_developer_options::instance()->get_flag(mString{"SHOW_DEBUG_INFO"}));
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(7);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Show FPS"});
    v89.set_bval(os_developer_options::instance()->get_flag(mString{"SHOW_FPS"}));
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(8);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"User Camera on Controller 2"});
    v89.set_bval(os_developer_options::instance()->get_flag(mString{"USERCAM_ON_CONTROLLER2"}));
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(9);
    v92->add_entry(&v89);

    v89 = debug_menu_entry(mString{"Toggle Unload All Districts"});
    v89.set_game_flags_handler(game_flags_handler);
    v89.set_id(13);
    v92->add_entry(&v89);

    {
        auto *v88 = create_menu("Save/Load", handle_game_entry, 10);
        auto v18 = debug_menu_entry(v88);
        v92->add_entry(&v18);

        v89 = debug_menu_entry(mString{"Save Game"});
        v89.set_game_flags_handler(game_flags_handler);
        v89.set_id(14);
        v88->add_entry(&v89);

        v89 = debug_menu_entry(mString{"Load Game"});
        v89.set_game_flags_handler(game_flags_handler);
        v89.set_id(15);
        v88->add_entry(&v89);

        v89 = debug_menu_entry(mString{"Attemp Auto Load"});
        v89.set_game_flags_handler(game_flags_handler);
        v89.set_id(16);
        v88->add_entry(&v89);
    }

    {
        auto *v87 = create_menu("Screenshot", handle_game_entry, 10);
        auto v23 = debug_menu_entry(v87);
        v92->add_entry(&v23);

        v89 = debug_menu_entry(mString{"Hires Screenshot"});
        v89.set_game_flags_handler(game_flags_handler);
        v89.set_id(11);
        v87->add_entry(&v89);

        v89 = debug_menu_entry(mString{"Lores Screenshot"});
        v89.set_game_flags_handler(game_flags_handler);
        v89.set_id(12);
        v87->add_entry(&v89);
    }

    create_devopt_menu(v92);
    create_gamefile_menu(v92);
}

void debug_menu::init() {

	root_menu = create_menu("Debug Menu", handle_debug_entry, 10);
	game_menu = create_menu("Game", handle_game_entry, 300);
	missions_menu = create_menu("Missions");
	options_menu = create_menu("Options", handle_options_select_entry, 2);
	script_menu = create_menu("Script", (menu_handler_function) handle_script_select_entry, 50);
	progression_menu = create_menu("Progression");
    level_select_menu = create_menu("Level Select");

	debug_menu_entry game_entry { game_menu };
	debug_menu_entry missions_entry { missions_menu };
	debug_menu_entry options_entry { options_menu };
	debug_menu_entry script_entry { script_menu };
	debug_menu_entry progression_entry { progression_menu };
	debug_menu_entry level_select_entry { level_select_menu };

    create_warp_menu(root_menu);
	add_debug_menu_entry(root_menu, &game_entry);
	add_debug_menu_entry(root_menu, &missions_entry);

    create_debug_district_variants_menu(root_menu);

	add_debug_menu_entry(root_menu, &options_entry);
	add_debug_menu_entry(root_menu, &script_entry);
	add_debug_menu_entry(root_menu, &progression_entry);
	add_debug_menu_entry(root_menu, &level_select_entry);

    create_debug_render_menu(root_menu);
    create_memory_menu(root_menu);
    create_entity_animation_menu(root_menu);

	debug_menu_entry show_fps = { "Show FPS", POINTER_BOOL, (void *) 0x975848 };
	debug_menu_entry memory_info = { "Memory Info", POINTER_BOOL, (void *) 0x975849 };
	
	add_debug_menu_entry(options_menu, &show_fps);
	add_debug_menu_entry(options_menu, &memory_info);

	/*
	for (int i = 0; i < 5; i++) {

		debug_menu_entry asdf;
		sprintf(asdf.text, "entry %d", i);
		printf("AQUI %s\n", asdf.text);

		add_debug_menu_entry(debug_menu::root_menu, &asdf);
	}
	add_debug_menu_entry(debug_menu::root_menu, &teste);
	*/
}

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD fdwReason, LPVOID reserverd) {

	if (sizeof(region) != 0x134) {
		__debugbreak();
	}

	memset(keys, 0, sizeof(keys));
	if (fdwReason == DLL_PROCESS_ATTACH) {
#if 0
		AllocConsole();

		if (!freopen("CONOUT$", "w", stdout)) {
			MessageBoxA(NULL, "Error", "Couldn't allocate console...Closing", 0);
			return FALSE;
		}
#endif

		set_text_writeable();
		set_rdata_writeable();
		install_patches();

	}
	else if (fdwReason == DLL_PROCESS_DETACH)
		FreeConsole();

	return TRUE;
}

int main() {
	return 0;
}
